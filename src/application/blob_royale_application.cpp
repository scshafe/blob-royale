#include "blob_royale_application.hpp"

#include "application_lifecycle_error.hpp"
#include "command_mailbox.hpp"
#include "game_server_state.hpp"
#include "simulation_runtime_state.hpp"
#include "structured_logger.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace blob_royale::application {
namespace {

using namespace std::chrono_literals;

constexpr auto kApplicationControlPollInterval = 25ms;
constexpr auto kServerStartupDeadline = 5s;

enum class ControlWakeReason {
  kSignal,
  kServerTerminated,
  kRuntimeFailed,
  kControlWaitFailed,
};

// Registers process signals and runs its event loop on the BlobRoyaleApplication::run caller.
// The timer observes lifecycle state only; simulation cadence remains owned by SimulationRuntime.
//
// It is also where a dropped command becomes visible. `blob_runtime` links no logger by contract
// (`docs/architecture/0002-simulation-architecture.md` § "Ownership and lifecycle":
// StructuredLogger "never enters simulation, runtime, or protocol values"), so the runtime counts
// overflow drops and this composition root -- which already polls runtime state on a fixed interval
// and already owns the logger -- turns a rising count into a structured line. A drop is therefore
// never silent even when the submitting session ignored its `CommandSubmissionResult`.
class ApplicationControlWait final {
public:
  ApplicationControlWait(const runtime::SimulationRuntime& simulation_runtime,
                         const server::GameServer& game_server,
                         observability::StructuredLogger& logger)
      : simulation_runtime_(simulation_runtime), game_server_(game_server), logger_(logger),
        process_signals_(control_context_, SIGINT, SIGTERM), status_poll_(control_context_) {}

  [[nodiscard]] ControlWakeReason wait() {
    process_signals_.async_wait(
        [this](const boost::system::error_code& error, const int signal_number) noexcept {
          static_cast<void>(signal_number);
          if (!error) {
            finish(ControlWakeReason::kSignal);
          } else if (error != boost::asio::error::operation_aborted) {
            finish(ControlWakeReason::kControlWaitFailed, error);
          }
        });
    observe_component_state();
    if (!wake_reason_.has_value()) {
      schedule_status_poll();
    }
    control_context_.run();

    if (!wake_reason_.has_value()) {
      throw ApplicationLifecycleError{
          ApplicationLifecycleErrorCode::kControlWaitFailed, "application.control_wait",
          "control event loop exhausted without a signal or component terminal state"};
    }
    if (*wake_reason_ == ControlWakeReason::kControlWaitFailed) {
      throw ApplicationLifecycleError{ApplicationLifecycleErrorCode::kControlWaitFailed,
                                      "application.control_wait", control_error_.message()};
    }
    return *wake_reason_;
  }

private:
  void schedule_status_poll() {
    status_poll_.expires_after(kApplicationControlPollInterval);
    status_poll_.async_wait([this](const boost::system::error_code& error) {
      if (error == boost::asio::error::operation_aborted) {
        return;
      }
      if (error) {
        finish(ControlWakeReason::kControlWaitFailed, error);
        return;
      }
      observe_component_state();
      if (!wake_reason_.has_value()) {
        schedule_status_poll();
      }
    });
  }

  // Reports every command the mailbox refused since the previous observation. Losing a spawn or a
  // despawn is reported at error severity because the roster itself lost a change -- a disconnected
  // player's body stays in the arena, or a connected one never gets a body -- while a lost thrust
  // is one missed 2.5 ms of steering.
  void observe_dropped_commands() noexcept {
    const runtime::CommandMailbox::Statistics statistics =
        simulation_runtime_.command_mailbox_statistics();
    if (statistics.dropped_command_count == reported_dropped_command_count_) {
      return;
    }
    const std::uint64_t newly_dropped =
        statistics.dropped_command_count - reported_dropped_command_count_;
    const std::uint64_t newly_dropped_lifecycle =
        statistics.dropped_entity_lifecycle_command_count -
        reported_dropped_entity_lifecycle_command_count_;
    reported_dropped_command_count_ = statistics.dropped_command_count;
    reported_dropped_entity_lifecycle_command_count_ =
        statistics.dropped_entity_lifecycle_command_count;

    const std::string detail =
        "dropped_command_count=" + std::to_string(newly_dropped) +
        " dropped_entity_lifecycle_command_count=" + std::to_string(newly_dropped_lifecycle) +
        " pending_command_count=" + std::to_string(statistics.pending_command_count);
    logger_.write({.severity = newly_dropped_lifecycle > 0 ? observability::LogSeverity::kError
                                                           : observability::LogSeverity::kWarning,
                   .event = "runtime.command_dropped",
                   .error_code = "RUNTIME.COMMAND_MAILBOX_OVERFLOW",
                   .detail = detail});
  }

  void observe_component_state() noexcept {
    observe_dropped_commands();
    const server::GameServerState server_state = game_server_.state();
    if (server_state == server::GameServerState::kStopped ||
        server_state == server::GameServerState::kFailed) {
      finish(ControlWakeReason::kServerTerminated);
      return;
    }
    if (simulation_runtime_.state() == runtime::SimulationRuntimeState::kFailed) {
      finish(ControlWakeReason::kRuntimeFailed);
    }
  }

  void finish(const ControlWakeReason wake_reason,
              const boost::system::error_code& error = {}) noexcept {
    if (wake_reason_.has_value()) {
      return;
    }
    wake_reason_ = wake_reason;
    control_error_ = error;
    boost::system::error_code ignored;
    process_signals_.cancel(ignored);
    status_poll_.cancel(ignored);
    control_context_.stop();
  }

  const runtime::SimulationRuntime& simulation_runtime_;
  const server::GameServer& game_server_;
  observability::StructuredLogger& logger_;
  std::uint64_t reported_dropped_command_count_{0};
  std::uint64_t reported_dropped_entity_lifecycle_command_count_{0};
  boost::asio::io_context control_context_{1};
  boost::asio::signal_set process_signals_;
  boost::asio::steady_timer status_poll_;
  std::optional<ControlWakeReason> wake_reason_;
  boost::system::error_code control_error_;
};

} // namespace

BlobRoyaleApplication BlobRoyaleApplication::create(ApplicationConfig application_config,
                                                    simulation::GameWorld initial_world,
                                                    observability::StructuredLogger& logger) {
  simulation::GameSimulation game_simulation = simulation::GameSimulation::create(
      application_config.simulation_config(), std::move(initial_world));
  return BlobRoyaleApplication{std::move(application_config), std::move(game_simulation), logger};
}

BlobRoyaleApplication::BlobRoyaleApplication(ApplicationConfig application_config,
                                             simulation::GameSimulation game_simulation,
                                             observability::StructuredLogger& logger)
    : logger_(logger), application_config_(std::move(application_config)),
      simulation_runtime_(std::move(game_simulation)),
      game_server_(application_config_.server_config(), simulation_runtime_.snapshot_publication(),
                   logger_) {}

BlobRoyaleApplication::~BlobRoyaleApplication() noexcept { stop_owned_components(); }

void BlobRoyaleApplication::run() {
  if (run_invoked_.exchange(true, std::memory_order_acq_rel)) {
    throw ApplicationLifecycleError{ApplicationLifecycleErrorCode::kRunAlreadyInvoked,
                                    "application.run", "run may be invoked exactly once"};
  }
  logger_.write({.severity = observability::LogSeverity::kInfo,
                 .event = "application.starting",
                 .lifecycle_state = "starting"});

  ControlWakeReason wake_reason = ControlWakeReason::kControlWaitFailed;
  std::optional<ApplicationControlWait> control_wait;
  try {
    control_wait.emplace(simulation_runtime_, game_server_, logger_);
    simulation_runtime_.start();
    start_server_thread();
    if (!game_server_.wait_for_startup_resolution(kServerStartupDeadline)) {
      throw ApplicationLifecycleError{ApplicationLifecycleErrorCode::kServerStartupTimedOut,
                                      "application.server_startup",
                                      "server listener startup exceeded its fixed deadline"};
    }
    game_server_.rethrow_if_failed();
    if (game_server_.state() != server::GameServerState::kRunning) {
      throw ApplicationLifecycleError{ApplicationLifecycleErrorCode::kServerTerminatedUnexpectedly,
                                      "application.server_startup",
                                      "server stopped while listener startup was resolving"};
    }
    logger_.write({.severity = observability::LogSeverity::kInfo,
                   .event = "application.running",
                   .lifecycle_state = "running"});
    wake_reason = control_wait->wait();
  } catch (...) {
    const std::exception_ptr control_failure = std::current_exception();
    logger_.write({.severity = observability::LogSeverity::kError,
                   .event = "application.failed",
                   .lifecycle_state = "failed"});
    stop_owned_components();
    rethrow_retained_component_failure();
    std::rethrow_exception(control_failure);
  }

  logger_.write({.severity = observability::LogSeverity::kInfo,
                 .event = "application.stopping",
                 .lifecycle_state = "stopping"});
  stop_owned_components();
  try {
    rethrow_retained_component_failure();
  } catch (...) {
    logger_.write({.severity = observability::LogSeverity::kError,
                   .event = "application.failed",
                   .lifecycle_state = "failed"});
    throw;
  }
  if (wake_reason == ControlWakeReason::kServerTerminated) {
    logger_.write({.severity = observability::LogSeverity::kError,
                   .event = "application.failed",
                   .lifecycle_state = "failed"});
    throw ApplicationLifecycleError{ApplicationLifecycleErrorCode::kServerTerminatedUnexpectedly,
                                    "application.run",
                                    "server event loop terminated without SIGINT or SIGTERM"};
  }
  logger_.write({.severity = observability::LogSeverity::kInfo,
                 .event = "application.stopped",
                 .lifecycle_state = "stopped"});
}

void BlobRoyaleApplication::start_server_thread() {
  server_thread_ = std::jthread([this]() noexcept {
    try {
      game_server_.run();
    } catch (...) {
      // GameServer retains and exposes the exact original failure to the application thread.
    }
  });
}

void BlobRoyaleApplication::stop_owned_components() noexcept {
  game_server_.stop();
  if (server_thread_.joinable()) {
    try {
      server_thread_.join();
    } catch (...) {
      // A join failure violates the composition root's exclusive thread-ownership invariant.
      std::terminate();
    }
  }
  simulation_runtime_.stop();
}

void BlobRoyaleApplication::rethrow_retained_component_failure() const {
  game_server_.rethrow_if_failed();
  simulation_runtime_.rethrow_if_failed();
}

} // namespace blob_royale::application
