#include "blob_royale_application.hpp"

#include "application_lifecycle_error.hpp"
#include "bot_reconciliation.hpp"
#include "command_mailbox.hpp"
#include "controller_host.hpp"
#include "controller_registry.hpp"
#include "game_mode_registry.hpp"
#include "game_server_state.hpp"
#include "game_simulation_setup.hpp"
#include "match_startup_validation.hpp"
#include "seat_roster.hpp"
#include "simulation_runtime_state.hpp"
#include "structured_logger.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::application {
namespace {

using namespace std::chrono_literals;

constexpr auto kApplicationControlPollInterval = 25ms;
constexpr auto kServerStartupDeadline = 5s;

// The presentation cadence a hosted bot decides at, derived from the same configured
// `snapshots_per_second` a browser session is pushed at. A bot reads the published snapshot exactly
// as the encoder does and at the same rate, which is the human/bot symmetry ADR 0004 requires: a
// faster cadence would give bots more decisions per committed tick than any player can have.
[[nodiscard]] std::chrono::steady_clock::duration
presentation_interval_of(const std::uint64_t snapshots_per_second) noexcept {
  return std::chrono::nanoseconds{std::chrono::nanoseconds::rep{1'000'000'000} /
                                  static_cast<std::chrono::nanoseconds::rep>(snapshots_per_second)};
}

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
                         controllers::ControllerHost& controller_host,
                         SeatBotReconciler* const bot_reconciler,
                         const std::chrono::steady_clock::duration presentation_interval,
                         observability::StructuredLogger& logger)
      : simulation_runtime_(simulation_runtime), game_server_(game_server),
        controller_host_(controller_host), bot_reconciler_(bot_reconciler),
        presentation_interval_(presentation_interval), logger_(logger),
        process_signals_(control_context_, SIGINT, SIGTERM), status_poll_(control_context_),
        controller_pass_(control_context_) {}

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
      schedule_controller_pass();
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

  // Drives every hosted bot one decision pass, at presentation cadence, on this same thread.
  //
  // **A bot is not seated when there are none.** `decide_once` on an empty host is a snapshot
  // acquisition and nothing else, so the timer is armed unconditionally and the cost of a roster of
  // zero is one cheap call per presentation frame.
  //
  // `decide_once` contains every controller failure and every sink refusal by contract
  // (`controller_host.hpp`), so nothing a bot does can reach this loop; what reaches it is the
  // counters, which `observe_controller_host` turns into structured lines.
  void schedule_controller_pass() {
    controller_pass_.expires_after(presentation_interval_);
    controller_pass_.async_wait([this](const boost::system::error_code& error) {
      if (error == boost::asio::error::operation_aborted) {
        return;
      }
      if (error) {
        finish(ControlWakeReason::kControlWaitFailed, error);
        return;
      }
      static_cast<void>(controller_host_.decide_once());
      observe_controller_host();
      if (!wake_reason_.has_value()) {
        schedule_controller_pass();
      }
    });
  }

  // Reports every controller failure and every refused bot submission since the previous
  // observation. `blob_controllers` links no logger by contract, exactly as `blob_runtime` does
  // not, so the host counts and this composition root -- which already owns the logger and already
  // drives the host -- turns a rising count into a structured line. A bot that throws on every pass
  // is otherwise invisible: the match keeps running and its blob simply stops moving.
  void observe_controller_host() noexcept {
    const controllers::ControllerHost::Statistics statistics = controller_host_.statistics();
    if (statistics.failed_controller_count == reported_failed_controller_count_ &&
        statistics.refused_command_count == reported_refused_command_count_) {
      return;
    }
    const std::uint64_t newly_failed =
        statistics.failed_controller_count - reported_failed_controller_count_;
    const std::uint64_t newly_refused =
        statistics.refused_command_count - reported_refused_command_count_;
    reported_failed_controller_count_ = statistics.failed_controller_count;
    reported_refused_command_count_ = statistics.refused_command_count;

    std::string detail =
        "failed_controller_count=" + std::to_string(newly_failed) +
        " refused_command_count=" + std::to_string(newly_refused) +
        " pass_count=" + std::to_string(statistics.pass_count) +
        " decided_command_count=" + std::to_string(statistics.decided_command_count);
    if (const std::optional<controllers::ControllerFailure>& failure =
            controller_host_.last_failure();
        failure.has_value()) {
      detail.append(" last_failure_controller_id=" + std::to_string(failure->controller.value()));
      detail.append(" last_failure_controller_kind=" + failure->controller_kind);
      detail.append(" last_failure_observed_tick=" +
                    std::to_string(failure->observed_tick_sequence.value()));
      detail.append(" last_failure_message=" + failure->message);
    }
    logger_.write({.severity = newly_failed > 0 ? observability::LogSeverity::kError
                                                : observability::LogSeverity::kWarning,
                   .event = "controllers.pass_degraded",
                   .error_code = newly_failed > 0 ? "CONTROLLERS.CONTROLLER_FAILED"
                                                  : "CONTROLLERS.COMMAND_REFUSED",
                   .detail = detail});
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

  // Makes the live bots match the committed seat roster, on the same poll that watches everything
  // else. A mode without a lobby has no reconciler and nothing to match.
  void reconcile_bots() noexcept {
    if (bot_reconciler_ == nullptr || !simulation_runtime_.snapshot_publication().is_ready()) {
      return;
    }
    try {
      bot_reconciler_->reconcile(*simulation_runtime_.snapshot_publication().latest());
    } catch (...) {
      // `reconcile` contains every failure it can name; anything else is a process-level condition
      // the next poll will meet again, and a control poll may not propagate.
    }
  }

  // What the worker's clock did since the last poll, in the same shape as a dropped command: the
  // runtime counts, this loop logs the rise. An overrun is a tick that ended after the next was
  // due; a re-base is a stall long enough that the runtime chose slow motion over a catch-up burst
  // (`tick_deadline.hpp`). Both are warnings because both are the room asking for less load or more
  // CPU, and neither loses a tick.
  void observe_tick_statistics() noexcept {
    const runtime::TickStatistics statistics = simulation_runtime_.tick_statistics();
    if (statistics.tick_overrun_count != reported_tick_overrun_count_) {
      const std::uint64_t newly_overrun =
          statistics.tick_overrun_count - reported_tick_overrun_count_;
      reported_tick_overrun_count_ = statistics.tick_overrun_count;
      const std::string detail =
          "tick_overrun_count=" + std::to_string(newly_overrun) +
          " committed_tick_count=" + std::to_string(statistics.committed_tick_count) +
          " maximum_tick_duration_nanoseconds=" +
          std::to_string(statistics.maximum_tick_duration_nanoseconds) +
          " maximum_lateness_nanoseconds=" +
          std::to_string(statistics.maximum_lateness_nanoseconds);
      logger_.write({.severity = observability::LogSeverity::kWarning,
                     .event = "runtime.tick_overrun",
                     .error_code = "RUNTIME.TICK_OVERRUN",
                     .detail = detail});
    }
    if (statistics.clock_rebase_count != reported_clock_rebase_count_) {
      const std::uint64_t newly_rebased =
          statistics.clock_rebase_count - reported_clock_rebase_count_;
      const std::uint64_t newly_behind =
          statistics.rebased_ticks_behind_total - reported_rebased_ticks_behind_total_;
      reported_clock_rebase_count_ = statistics.clock_rebase_count;
      reported_rebased_ticks_behind_total_ = statistics.rebased_ticks_behind_total;
      const std::string detail =
          "clock_rebase_count=" + std::to_string(newly_rebased) +
          " ticks_behind=" + std::to_string(newly_behind) +
          " committed_tick_count=" + std::to_string(statistics.committed_tick_count);
      logger_.write({.severity = observability::LogSeverity::kWarning,
                     .event = "runtime.clock_rebased",
                     .error_code = "RUNTIME.CLOCK_REBASED",
                     .detail = detail});
    }
  }

  void observe_component_state() noexcept {
    observe_dropped_commands();
    observe_tick_statistics();
    reconcile_bots();
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
    controller_pass_.cancel(ignored);
    control_context_.stop();
  }

  const runtime::SimulationRuntime& simulation_runtime_;
  const server::GameServer& game_server_;
  controllers::ControllerHost& controller_host_;
  SeatBotReconciler* bot_reconciler_;
  std::chrono::steady_clock::duration presentation_interval_;
  observability::StructuredLogger& logger_;
  std::uint64_t reported_dropped_command_count_{0};
  std::uint64_t reported_dropped_entity_lifecycle_command_count_{0};
  std::uint64_t reported_tick_overrun_count_{0};
  std::uint64_t reported_clock_rebase_count_{0};
  std::uint64_t reported_rebased_ticks_behind_total_{0};
  std::uint64_t reported_failed_controller_count_{0};
  std::uint64_t reported_refused_command_count_{0};
  boost::asio::io_context control_context_{1};
  boost::asio::signal_set process_signals_;
  boost::asio::steady_timer status_poll_;
  boost::asio::steady_timer controller_pass_;
  std::optional<ControlWakeReason> wake_reason_;
  boost::system::error_code control_error_;
};

} // namespace

BlobRoyaleApplication BlobRoyaleApplication::create(ApplicationConfig application_config,
                                                    simulation::MapDefinition map,
                                                    simulation::GameWorld initial_world,
                                                    observability::StructuredLogger& logger) {
  // The two cross-value rules first, because both are arithmetic over already-validated values and
  // both describe a match that would only fail once it was being played.
  require_map_matches_published_world(application_config.simulation_config(), map);
  // The hazard table travels with the other two, because the standing hazard population is part of
  // the worst case and no one of the three values can see the other two on its own.
  require_match_fits_snapshot_bound(application_config.match_configuration(), map,
                                    application_config.game_mode_configuration().hazards);

  // The mode is resolved from the registry and handed the validated `[<mode>]` sections. The
  // engine reads its seven declarations once, validates the map through it, and destroys it.
  std::unique_ptr<const simulation::GameMode> mode =
      gameplay::GameModeRegistry::create(application_config.match_configuration().mode_name(),
                                         application_config.game_mode_configuration());

  // **The lobby is seeded here, into the world, before the engine ever sees it, and only for a
  // mode that has one.**
  //
  // `MatchState::seats` is engine state -- it is the input to the machine's first transition -- but
  // its *initial* size is a required configuration key that only a mode's section carries, so the
  // one place that has both the world and the parsed sections is this composition root. It is the
  // same relationship the seeded entities already have: the world arrives carrying the state a
  // match begins with, and `GameSimulation::create` neither invents nor overwrites it
  // (`src/simulation/seat_roster.hpp`).
  //
  // `[match] lobby_seat_count` is a fact about who plays this match, whatever mode plays it;
  // whether it is *applied* is the mode's declaration. A mode that accepts no `start_match` starts
  // with no roster, which is the empty array protocol v2 promises for a world that declared no
  // lobby, and for a mode that does the `[match] bots` roster is the declaration of the first seats
  // rather than a startup roster (`match_startup_validation.hpp`). A lobby the map could not seat
  // in full is refused first, naming both counts.
  require_lobby_fits_map(*mode, application_config.match_configuration().lobby_seat_count(), map);
  initial_world.mutable_match().seats =
      initial_seat_roster_for(*mode, application_config.match_configuration().lobby_seat_count(),
                              application_config.match_configuration().bot_roster());

  // The accepted command mask is copied out **before** the mode is moved into the engine, which
  // destroys it once it has read its seven declarations. It is the set a protocol v2 `welcome`
  // advertises and the set the session boundary enforces, and copying the mode's own declaration
  // is what keeps the advertised set and the enforced set from being two answers.
  const simulation::CommandKindMask accepted_command_kinds = mode->accepted_command_kinds();

  simulation::GameSimulation game_simulation = simulation::GameSimulation::create(
      application_config.simulation_config(), std::move(initial_world),
      simulation::GameSimulationSetup::of_mode(std::move(map), std::move(mode)));

  // A prvalue, because the composition root is deliberately neither copyable nor movable: the
  // controller host and the server hold references into the runtime this object owns.
  return BlobRoyaleApplication{std::move(application_config), std::move(game_simulation),
                               accepted_command_kinds, logger};
}

BlobRoyaleApplication::BlobRoyaleApplication(
    ApplicationConfig application_config, simulation::GameSimulation game_simulation,
    const simulation::CommandKindMask accepted_command_kinds,
    observability::StructuredLogger& logger)
    : logger_(logger), application_config_(std::move(application_config)),
      simulation_runtime_(std::move(game_simulation)),
      controller_host_(simulation_runtime_.snapshot_publication(),
                       simulation_runtime_.command_sink()),
      // The only new capability the network boundary receives, and it is named in full here: a
      // write-only command sink, a read-only presentation directory, and the map and accepted-kind
      // identities a `welcome` announces. The server still receives no simulation, no runtime, and
      // no lifecycle transition.
      game_server_(application_config_.server_config(), simulation_runtime_.snapshot_publication(),
                   server::MatchSessionContext::create(
                       simulation_runtime_.command_sink(),
                       simulation_runtime_.controller_directory(),
                       std::string{application_config_.match_configuration().map_name()},
                       accepted_command_kinds, registered_npc_controller_kinds()),
                   logger_) {
  // A mode with a lobby gets its bots from the reconciler, one per declared seat, on the control
  // loop's first poll and every poll after; the roster was declared into the seats in `create`. A
  // mode without one seats its roster here, in the constructor rather than in `create`, because
  // this class is non-movable and a factory that configured a local could not return it -- every
  // startup bot therefore exists before any caller can observe the object.
  if (accepted_command_kinds.contains(simulation::CommandKind::kStartMatch)) {
    bot_reconciler_.emplace(simulation_runtime_.command_sink(), controller_host_,
                            application_config_.match_configuration().seed(), logger_);
  } else {
    seat_configured_bots();
  }
}

std::vector<std::string> BlobRoyaleApplication::registered_npc_controller_kinds() {
  // **The one place a bot kind name leaves `blob_controllers`.** The `welcome` frame publishes this
  // list so a client can offer it behind an empty seat, and `decode_command_envelope` accepts a
  // `seat_npc` naming exactly these and nothing else -- both from this single read, which is what
  // makes registering a bot cost one row in `controller_registry.hpp` and no client change at all.
  //
  // Registry order is preserved rather than sorted: the table's order is somebody's deliberate
  // ordering of the bots and re-sorting it here would invent a different one for every client.
  //
  // It is a composition-root job because this is the only layer that links both libraries. The
  // server must not link `blob_controllers` -- it has no business constructing a bot -- and the
  // registry must not know a protocol exists.
  std::vector<std::string> npc_controller_kinds;
  const std::span<const controllers::ControllerRegistry::Registration> registrations =
      controllers::ControllerRegistry::registrations();
  npc_controller_kinds.reserve(registrations.size());
  for (const controllers::ControllerRegistry::Registration& registration : registrations) {
    npc_controller_kinds.emplace_back(registration.name);
  }
  return npc_controller_kinds;
}

void BlobRoyaleApplication::seat_configured_bots() {
  const MatchConfiguration& match = application_config_.match_configuration();
  for (const MatchConfiguration::BotRosterEntry& entry : match.bot_roster()) {
    for (std::uint64_t ordinal = 1; ordinal <= entry.count; ++ordinal) {
      // A bot opens a session exactly as a browser will, through the same `CommandSink`, and its
      // display name is derived from the roster rather than supplied by anyone: nothing about a bot
      // arrives from outside this process.
      const std::string display_name = entry.controller_kind + " " + std::to_string(ordinal);
      const simulation::ControllerId controller =
          simulation_runtime_.command_sink().open_session(entry.controller_kind, display_name);
      controller_host_.add(
          controllers::ControllerRegistry::create(entry.controller_kind, controller, match.seed()));
    }
  }
  if (!match.bot_roster().empty()) {
    logger_.write({.severity = observability::LogSeverity::kInfo,
                   .event = "controllers.roster_seated",
                   .detail = "hosted_controller_count=" + std::to_string(controller_host_.size()) +
                             " match_seed=" + std::to_string(match.seed())});
  }
}

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
    control_wait.emplace(
        simulation_runtime_, game_server_, controller_host_,
        bot_reconciler_.has_value() ? &*bot_reconciler_ : nullptr,
        presentation_interval_of(application_config_.server_config().snapshots_per_second()),
        logger_);
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
