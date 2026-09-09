#include "blob_royale_application.hpp"

#include "application_lifecycle_error.hpp"
#include "bot_reconciliation.hpp"
#include "command_mailbox.hpp"
#include "controller_host.hpp"
#include "controller_registry.hpp"
#include "game_mode_registry.hpp"
#include "game_server_state.hpp"
#include "game_simulation_setup.hpp"
#include "match_phase.hpp"
#include "match_startup_validation.hpp"
#include "seat_roster.hpp"
#include "simulation_runtime_state.hpp"
#include "structured_logger.hpp"
#include "world_snapshot.hpp"

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
  kControlWaitFailed,
};

// What the control loop has already reported about one room, so every line it writes is the rise
// since the last poll rather than a total, and a phase or a failure is announced exactly once.
struct RoomWatch final {
  std::uint64_t reported_dropped_command_count{0};
  std::uint64_t reported_dropped_entity_lifecycle_command_count{0};
  std::uint64_t reported_tick_overrun_count{0};
  std::uint64_t reported_clock_rebase_count{0};
  std::uint64_t reported_rebased_ticks_behind_total{0};
  std::uint64_t reported_failed_controller_count{0};
  std::uint64_t reported_refused_command_count{0};
  std::optional<simulation::MatchPhase> last_phase;
  bool failure_reported{false};
};

// Registers process signals and runs its event loop on the BlobRoyaleApplication::run caller.
// The timer observes lifecycle state only; simulation cadence remains owned by each room's
// SimulationRuntime.
//
// It is also where a room's counters become log lines. `blob_runtime` and `blob_controllers` link
// no logger by contract (`docs/architecture/0002-simulation-architecture.md` § "Ownership and
// lifecycle"), so the runtime counts drops, overruns, and re-bases, the host counts failures, and
// this composition root -- which already polls every room on a fixed interval and already owns
// the logger -- turns a rising count into a structured line carrying the room's `lobby_id`.
class ApplicationControlWait final {
public:
  ApplicationControlWait(const std::span<const std::unique_ptr<Room>> rooms,
                         const server::LobbyDirectory& lobbies,
                         const server::GameServer& game_server,
                         const std::chrono::steady_clock::duration presentation_interval,
                         observability::StructuredLogger& logger)
      : rooms_(rooms), lobbies_(lobbies), game_server_(game_server),
        presentation_interval_(presentation_interval), logger_(logger), watches_(rooms.size()),
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

  // Drives every room's hosted bots one decision pass, at presentation cadence, on this same
  // thread.
  //
  // **A bot is not seated when there are none.** `decide_once` on an empty host is a snapshot
  // acquisition and nothing else, so the timer is armed unconditionally and the cost of a roster of
  // zero is one cheap call per presentation frame per room. A failed room is skipped: its
  // publication is not ready and its bots have nothing to decide about.
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
      for (std::size_t index = 0; index < rooms_.size(); ++index) {
        Room& room = *rooms_[index];
        if (room.runtime().state() == runtime::SimulationRuntimeState::kFailed) {
          continue;
        }
        static_cast<void>(room.host().decide_once());
        observe_controller_host(room, watches_[index]);
      }
      if (!wake_reason_.has_value()) {
        schedule_controller_pass();
      }
    });
  }

  // Reports every controller failure and every refused bot submission since the previous
  // observation. A bot that throws on every pass is otherwise invisible: the match keeps running
  // and its blob simply stops moving.
  void observe_controller_host(Room& room, RoomWatch& watch) noexcept {
    const controllers::ControllerHost::Statistics statistics = room.host().statistics();
    if (statistics.failed_controller_count == watch.reported_failed_controller_count &&
        statistics.refused_command_count == watch.reported_refused_command_count) {
      return;
    }
    const std::uint64_t newly_failed =
        statistics.failed_controller_count - watch.reported_failed_controller_count;
    const std::uint64_t newly_refused =
        statistics.refused_command_count - watch.reported_refused_command_count;
    watch.reported_failed_controller_count = statistics.failed_controller_count;
    watch.reported_refused_command_count = statistics.refused_command_count;

    std::string detail =
        "failed_controller_count=" + std::to_string(newly_failed) +
        " refused_command_count=" + std::to_string(newly_refused) +
        " pass_count=" + std::to_string(statistics.pass_count) +
        " decided_command_count=" + std::to_string(statistics.decided_command_count);
    if (const std::optional<controllers::ControllerFailure>& failure = room.host().last_failure();
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
                   .lobby_id = room.lobby_id(),
                   .error_code = newly_failed > 0 ? "CONTROLLERS.CONTROLLER_FAILED"
                                                  : "CONTROLLERS.COMMAND_REFUSED",
                   .detail = detail});
  }

  // Reports every command the mailbox refused since the previous observation. Losing a spawn or a
  // despawn is reported at error severity because the roster itself lost a change -- a disconnected
  // player's body stays in the arena, or a connected one never gets a body -- while a lost thrust
  // is one missed 2.5 ms of steering.
  void observe_dropped_commands(Room& room, RoomWatch& watch) noexcept {
    const runtime::CommandMailbox::Statistics statistics =
        room.runtime().command_mailbox_statistics();
    if (statistics.dropped_command_count == watch.reported_dropped_command_count) {
      return;
    }
    const std::uint64_t newly_dropped =
        statistics.dropped_command_count - watch.reported_dropped_command_count;
    const std::uint64_t newly_dropped_lifecycle =
        statistics.dropped_entity_lifecycle_command_count -
        watch.reported_dropped_entity_lifecycle_command_count;
    watch.reported_dropped_command_count = statistics.dropped_command_count;
    watch.reported_dropped_entity_lifecycle_command_count =
        statistics.dropped_entity_lifecycle_command_count;

    const std::string detail =
        "dropped_command_count=" + std::to_string(newly_dropped) +
        " dropped_entity_lifecycle_command_count=" + std::to_string(newly_dropped_lifecycle) +
        " pending_command_count=" + std::to_string(statistics.pending_command_count);
    logger_.write({.severity = newly_dropped_lifecycle > 0 ? observability::LogSeverity::kError
                                                           : observability::LogSeverity::kWarning,
                   .event = "runtime.command_dropped",
                   .lobby_id = room.lobby_id(),
                   .error_code = "RUNTIME.COMMAND_MAILBOX_OVERFLOW",
                   .detail = detail});
  }

  // What the worker's clock did since the last poll, in the same shape as a dropped command: the
  // runtime counts, this loop logs the rise. An overrun is a tick that ended after the next was
  // due; a re-base is a stall long enough that the runtime chose slow motion over a catch-up burst
  // (`tick_deadline.hpp`). Both are warnings because both are the room asking for less load or more
  // CPU, and neither loses a tick.
  void observe_tick_statistics(Room& room, RoomWatch& watch) noexcept {
    const runtime::TickStatistics statistics = room.runtime().tick_statistics();
    if (statistics.tick_overrun_count != watch.reported_tick_overrun_count) {
      const std::uint64_t newly_overrun =
          statistics.tick_overrun_count - watch.reported_tick_overrun_count;
      watch.reported_tick_overrun_count = statistics.tick_overrun_count;
      const std::string detail =
          "tick_overrun_count=" + std::to_string(newly_overrun) +
          " committed_tick_count=" + std::to_string(statistics.committed_tick_count) +
          " maximum_tick_duration_nanoseconds=" +
          std::to_string(statistics.maximum_tick_duration_nanoseconds) +
          " maximum_lateness_nanoseconds=" +
          std::to_string(statistics.maximum_lateness_nanoseconds);
      logger_.write({.severity = observability::LogSeverity::kWarning,
                     .event = "runtime.tick_overrun",
                     .lobby_id = room.lobby_id(),
                     .error_code = "RUNTIME.TICK_OVERRUN",
                     .detail = detail});
    }
    if (statistics.clock_rebase_count != watch.reported_clock_rebase_count) {
      const std::uint64_t newly_rebased =
          statistics.clock_rebase_count - watch.reported_clock_rebase_count;
      const std::uint64_t newly_behind =
          statistics.rebased_ticks_behind_total - watch.reported_rebased_ticks_behind_total;
      watch.reported_clock_rebase_count = statistics.clock_rebase_count;
      watch.reported_rebased_ticks_behind_total = statistics.rebased_ticks_behind_total;
      const std::string detail =
          "clock_rebase_count=" + std::to_string(newly_rebased) +
          " ticks_behind=" + std::to_string(newly_behind) +
          " committed_tick_count=" + std::to_string(statistics.committed_tick_count);
      logger_.write({.severity = observability::LogSeverity::kWarning,
                     .event = "runtime.clock_rebased",
                     .lobby_id = room.lobby_id(),
                     .error_code = "RUNTIME.CLOCK_REBASED",
                     .detail = detail});
    }
  }

  // The committed world, read once per poll: a phase that differs from the last one seen is one
  // `match.phase_changed` line -- the whole story of a match in four of them -- and the bots are
  // made to match the seats. **Abandonment is decided here.** A room whose session count is zero
  // while its match is in `countdown` or `running` has its bots retired rather than reseated, so
  // the match ends by attrition and the machine walks back to `lobby`, where the next poll reseats
  // every declared NPC (ADR 0006 § "The lobby lifecycle"). The tick never learns the word "human":
  // the count is the server's, and the reconciliation only sees a flag.
  void observe_room_world(Room& room, RoomWatch& watch) noexcept {
    const runtime::SnapshotPublication& publication = room.runtime().snapshot_publication();
    if (!publication.is_ready()) {
      return;
    }
    const std::shared_ptr<const simulation::WorldSnapshot> latest = publication.latest();
    if (latest == nullptr) {
      return;
    }
    const simulation::MatchPhase phase = latest->match().phase();
    if (!watch.last_phase.has_value() || *watch.last_phase != phase) {
      const std::string detail = "phase=" + std::string(simulation::match_phase_name(phase)) +
                                 " previous=" +
                                 (watch.last_phase.has_value()
                                      ? std::string(simulation::match_phase_name(*watch.last_phase))
                                      : std::string("none"));
      logger_.write({.severity = observability::LogSeverity::kInfo,
                     .event = "match.phase_changed",
                     .lobby_id = room.lobby_id(),
                     .tick_sequence = latest->tick_sequence().value(),
                     .detail = detail});
      watch.last_phase = phase;
    }
    SeatBotReconciler* const reconciler = room.reconciler();
    if (reconciler == nullptr) {
      return;
    }
    const bool abandoned =
        lobbies_.room(room.lobby_id()).session_count() == 0 &&
        (phase == simulation::MatchPhase::kCountdown || phase == simulation::MatchPhase::kRunning);
    try {
      reconciler->reconcile(*latest, abandoned);
    } catch (...) {
      // `reconcile` contains every failure it can name; anything else is a process-level condition
      // the next poll will meet again, and a control poll may not propagate.
    }
  }

  // A room whose worker died is announced once, with the exception, and left alone: its publication
  // is already not ready, so its sessions close themselves, and the rooms that work keep serving.
  void observe_runtime_failure(Room& room, RoomWatch& watch) noexcept {
    if (watch.failure_reported ||
        room.runtime().state() != runtime::SimulationRuntimeState::kFailed) {
      return;
    }
    watch.failure_reported = true;
    std::string message = "no exception was retained";
    try {
      room.runtime().rethrow_if_failed();
    } catch (const std::exception& failure) {
      message = failure.what();
    } catch (...) {
      message = "non-standard exception";
    }
    logger_.write({.severity = observability::LogSeverity::kError,
                   .event = "runtime.failed",
                   .lobby_id = room.lobby_id(),
                   .error_code = "RUNTIME.WORKER_FAILED",
                   .detail = "message=" + message});
  }

  void observe_component_state() noexcept {
    for (std::size_t index = 0; index < rooms_.size(); ++index) {
      Room& room = *rooms_[index];
      RoomWatch& watch = watches_[index];
      observe_dropped_commands(room, watch);
      observe_tick_statistics(room, watch);
      observe_room_world(room, watch);
      observe_runtime_failure(room, watch);
    }
    const server::GameServerState server_state = game_server_.state();
    if (server_state == server::GameServerState::kStopped ||
        server_state == server::GameServerState::kFailed) {
      finish(ControlWakeReason::kServerTerminated);
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

  std::span<const std::unique_ptr<Room>> rooms_;
  const server::LobbyDirectory& lobbies_;
  const server::GameServer& game_server_;
  std::chrono::steady_clock::duration presentation_interval_;
  observability::StructuredLogger& logger_;
  std::vector<RoomWatch> watches_;
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
  // The cross-value rules first, because each is arithmetic over already-validated values and each
  // describes a match that would only fail once it was being played. They hold for every room,
  // because every room plays the same match on the same map.
  require_map_matches_published_world(application_config.simulation_config(), map);
  // The hazard table travels with the other two, because the standing hazard population is part of
  // the worst case and no one of the three values can see the other two on its own.
  require_match_fits_snapshot_bound(application_config.match_configuration(), map,
                                    application_config.game_mode_configuration().hazards);

  const MatchConfiguration& match = application_config.match_configuration();
  const std::uint64_t room_count = application_config.lobbies_configuration().count();
  std::vector<std::unique_ptr<Room>> rooms;
  rooms.reserve(room_count);
  for (std::uint64_t lobby_id = 1; lobby_id <= room_count; ++lobby_id) {
    // The mode is resolved from the registry once per room and handed the validated `[<mode>]`
    // sections; each room's engine reads its declarations once, validates the map through it, and
    // destroys it.
    std::unique_ptr<const simulation::GameMode> mode = gameplay::GameModeRegistry::create(
        match.mode_name(), application_config.game_mode_configuration());
    if (lobby_id == 1) {
      // The same map for every room, so the marker-per-seat rule is asked once.
      require_lobby_fits_map(*mode, match.lobby_seat_count(), map);
    }

    // **Room 1 plays the world the caller built** -- the map plus any scenario -- and every further
    // room plays the map alone, seeded `seed + (lobby_id - 1)` so two rooms never draw the same
    // hazards. A scenario with more than one room is refused by the loader, so the two shapes
    // never mix.
    simulation::GameWorld world =
        lobby_id == 1 ? std::move(initial_world)
                      : simulation::GameWorld::create(application_config.simulation_config(), map,
                                                      match.seed() + (lobby_id - 1));

    // **The lobby is seeded here, into the world, before the engine ever sees it, and only for a
    // mode that has one.** `MatchState::seats` is engine state -- it is the input to the machine's
    // first transition -- and its initial size is `[match] lobby_seat_count`, so the one place that
    // has both the world and the parsed sections is this composition root. A mode that accepts no
    // `start_match` starts with no roster, which is the empty array protocol v2 promises for a
    // world that declared no lobby, and for a mode that does the `[match] bots` roster is the
    // declaration of the first seats rather than a startup roster (`match_startup_validation.hpp`).
    world.mutable_match().seats =
        initial_seat_roster_for(*mode, match.lobby_seat_count(), match.bot_roster());

    // The accepted command mask is copied out **before** the mode is moved into the engine, which
    // destroys it once it has read its declarations. It is the set a protocol v2 `welcome`
    // advertises and the set the session boundary enforces, and copying the mode's own declaration
    // is what keeps the advertised set and the enforced set from being two answers.
    const simulation::CommandKindMask accepted_command_kinds = mode->accepted_command_kinds();
    simulation::GameSimulation game_simulation = simulation::GameSimulation::create(
        application_config.simulation_config(), std::move(world),
        simulation::GameSimulationSetup::of_mode(map, std::move(mode)));
    rooms.push_back(std::make_unique<Room>(lobby_id, std::move(game_simulation),
                                           accepted_command_kinds, match,
                                           registered_npc_controller_kinds(), logger));
  }

  // A prvalue, because the composition root is deliberately neither copyable nor movable: the
  // directory and the server hold references into the rooms this object owns.
  return BlobRoyaleApplication{std::move(application_config), std::move(rooms), logger};
}

BlobRoyaleApplication::BlobRoyaleApplication(ApplicationConfig application_config,
                                             std::vector<std::unique_ptr<Room>> rooms,
                                             observability::StructuredLogger& logger)
    : logger_(logger), application_config_(std::move(application_config)), rooms_(std::move(rooms)),
      lobby_directory_(directory_of(rooms_)),
      // The only capability the network boundary receives: every room's read-only publication and
      // write-only session context, by lobby id. The server still receives no simulation, no
      // runtime, and no lifecycle transition.
      game_server_(application_config_.server_config(), lobby_directory_, logger_) {}

server::LobbyDirectory
BlobRoyaleApplication::directory_of(const std::span<const std::unique_ptr<Room>> rooms) {
  std::vector<server::LobbyDirectory::Room> entries;
  entries.reserve(rooms.size());
  for (const std::unique_ptr<Room>& room : rooms) {
    entries.push_back({.lobby_id = room->lobby_id(),
                       .snapshot_publication = room->runtime().snapshot_publication(),
                       .match_session = room->match_session()});
  }
  return server::LobbyDirectory::create(std::move(entries));
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
        rooms_, lobby_directory_, game_server_,
        presentation_interval_of(application_config_.server_config().snapshots_per_second()),
        logger_);
    for (const std::unique_ptr<Room>& room : rooms_) {
      room->runtime().start();
    }
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
                   .lifecycle_state = "running",
                   .detail = "lobby_count=" + std::to_string(rooms_.size())});
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
  for (const std::unique_ptr<Room>& room : rooms_) {
    room->runtime().stop();
  }
}

void BlobRoyaleApplication::rethrow_retained_component_failure() const {
  game_server_.rethrow_if_failed();
  for (const std::unique_ptr<Room>& room : rooms_) {
    room->runtime().rethrow_if_failed();
  }
}

} // namespace blob_royale::application
