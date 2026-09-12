#ifndef BLOB_ROYALE_RUNTIME_SIMULATION_RUNTIME_HPP
#define BLOB_ROYALE_RUNTIME_SIMULATION_RUNTIME_HPP

#include "command_mailbox.hpp"
#include "command_sink.hpp"
#include "controller_directory.hpp"
#include "entity_id_allocator.hpp"
#include "game_simulation.hpp"
#include "movement_tuning_result_delivery.hpp"
#include "simulation_runtime_state.hpp"
#include "snapshot_publication.hpp"
#include "tick_deadline.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <stop_token>
#include <thread>

namespace blob_royale::runtime {

// canonical: simulation_runtime -- sole clock and mutation owner for one simulation timeline.
//
// **One writer, and the whole path into it.** The runtime owns the simulation, one persistent
// worker, one `SnapshotPublication`, one `CommandMailbox`, one `EntityIdAllocator`, and one
// `ControllerDirectory` (`docs/architecture/0002-simulation-architecture.md` § "Ownership and
// lifecycle"). Only the worker mutates the simulation and only the worker drains the mailbox, so a
// command source outside the runtime holds a write-only `CommandSink&` and a read-only
// `const SnapshotPublication&` and can obtain neither `GameSimulation&` nor a lifecycle transition.
//
// **Every tick is built the same way and none of them is the no-input tick.** The worker drains the
// mailbox exactly once, asks the allocator for a block of `spawn_count + 1` contiguous ids, builds
// one `InputBatch` under the running mode's accepted kinds, and calls `step`. The block is never
// empty, because royale creates its zone entity from the tick's reservation on its first running
// tick, so `InputBatch::empty()` -- which carries no reservation at all -- would be a hard failure
// there rather than a quieter tick (`entity_id_allocator.hpp`).
// **The clock is bounded, and tick numbers never skip.** The worker waits for each tick's deadline
// and advances it by one fixed quantum; a late worker catches up back-to-back for at most
// `kMaximumCatchUpTicks` quanta and is then re-based to now, so a stall becomes a moment of slow
// motion rather than a sprint (`tick_deadline.hpp`). What the clock did is counted in
// `TickStatistics`, which only rises: `blob_runtime` links no logger by contract, so the runtime
// counts and the composition root logs, exactly as it does for a dropped command.
// related: command_sink.hpp -- the only capability a command source is given.
// related: command_mailbox.hpp -- the bounded buffer this drains.
// related: entity_id_allocator.hpp -- the monotonic issuer of every tick's reservation.
// related: controller_directory.hpp -- presentation values the encoding boundary joins.
// related: tick_deadline.hpp -- the deadline policy the worker applies once per tick.

// canonical: tick_statistics -- what the worker's clock did, as counters that only rise.
struct TickStatistics final {
  // Ticks committed since construction. A re-base moves the deadline, never this.
  std::uint64_t committed_tick_count{0};
  // Ticks that ended after the next tick was already due, whether the step was slow or the thread
  // woke late; `maximum_tick_duration_nanoseconds` says which.
  std::uint64_t tick_overrun_count{0};
  // Deadlines re-based to now because the worker was more than `kMaximumCatchUpTicks` quanta
  // behind.
  std::uint64_t clock_rebase_count{0};
  // The quanta the worker was behind at each re-base, summed: how far the room's clock has been
  // moved behind wall time altogether.
  std::uint64_t rebased_ticks_behind_total{0};
  // The longest step-and-snapshot, in nanoseconds.
  std::uint64_t maximum_tick_duration_nanoseconds{0};
  // The furthest a tick ended past the next tick's due time, in nanoseconds.
  std::uint64_t maximum_lateness_nanoseconds{0};

  friend bool operator==(const TickStatistics&, const TickStatistics&) = default;
};

class SimulationRuntime final {
public:
  // Takes exclusive ownership of a validated simulation and publishes its coherent initial state.
  // The entity-id cursor opens above everything the simulation has already committed and above its
  // map's whole static-body block, and the mailbox copies the mode's accepted command kinds.
  explicit SimulationRuntime(
      simulation::GameSimulation game_simulation,
      simulation::NpcCatalogue npc_catalogue = simulation::NpcCatalogue::empty());

  SimulationRuntime(const SimulationRuntime&) = delete;
  SimulationRuntime(SimulationRuntime&&) = delete;
  SimulationRuntime& operator=(const SimulationRuntime&) = delete;
  SimulationRuntime& operator=(SimulationRuntime&&) = delete;
  ~SimulationRuntime();

  // Starts a ready runtime or resumes a paused runtime. Repeated calls while running are no-ops.
  // Throws SimulationRuntimeLifecycleError after stop and rethrows a worker failure after failure.
  void start();

  // Synchronously reaches a quiescent paused state. No tick can publish after this call returns.
  // Repeated calls while ready or paused are no-ops.
  void pause();

  // Resumes a ready or paused runtime. Repeated calls while running are no-ops.
  // Throws SimulationRuntimeLifecycleError after stop and rethrows a worker failure after failure.
  void resume();

  // Requests cancellation and joins the one worker. Repeated calls are synchronous no-ops.
  // A runtime that failed retains kFailed so callers can inspect and rethrow its original failure.
  void stop() noexcept;

  [[nodiscard]] SimulationRuntimeState state() const noexcept;

  // Waits only as a lifecycle observation aid; false means the deadline or a different terminal
  // state was reached. Simulation timing never consumes this caller-supplied duration.
  [[nodiscard]] bool wait_for_state(SimulationRuntimeState expected_state,
                                    std::chrono::steady_clock::duration timeout) const;

  // Rethrows the exact exception captured from the simulation worker, if one exists.
  void rethrow_if_failed() const;

  // Exposes only immutable publication capability, even to callers holding a mutable runtime.
  [[nodiscard]] const SnapshotPublication& snapshot_publication() const& noexcept {
    return snapshot_publication_;
  }
  [[nodiscard]] const SnapshotPublication& snapshot_publication() const&& = delete;

  // The write-only command capability grants no world read and no lifecycle transition.
  [[nodiscard]] CommandSink& command_sink() & noexcept { return command_sink_; }
  CommandSink& command_sink() && = delete;

  // The separate delivery capability transfers only owned terminal tuning results. It grants no
  // command submission, world mutation, or runtime lifecycle access.
  [[nodiscard]] MovementTuningResultDelivery& tuning_result_delivery() & noexcept {
    return tuning_result_delivery_;
  }
  MovementTuningResultDelivery& tuning_result_delivery() && = delete;

  // Read-only presentation values, joined at the encoding boundary. Writing is `CommandSink`'s
  // `open_session`/`close_session`, so a reader cannot register or retire a controller.
  [[nodiscard]] const ControllerDirectory& controller_directory() const& noexcept {
    return controller_directory_;
  }
  [[nodiscard]] const ControllerDirectory& controller_directory() const&& = delete;

  // One coherent observation of the mailbox's counters, including `dropped_command_count`. This is
  // how a drop becomes visible: `blob_runtime` links no logger by contract, so the runtime counts
  // and the composition root logs (`command_mailbox.hpp`).
  [[nodiscard]] CommandMailbox::Statistics command_mailbox_statistics() const {
    return command_mailbox_.statistics();
  }

  // One coherent observation of what the worker's clock has done: committed ticks, overruns,
  // re-bases, and the worst step and lateness seen. Readable from any thread at any lifecycle
  // state; the counters only rise.
  [[nodiscard]] TickStatistics tick_statistics() const;

  // The lowest EntityId no tick has been given yet, for diagnostics and boundary checks.
  [[nodiscard]] simulation::EntityId next_entity_id() const noexcept {
    return entity_id_allocator_.next_entity_id();
  }

private:
  using Clock = std::chrono::steady_clock;

  void transition_to_running(const char* operation);
  void run(std::stop_token stop_token) noexcept;
  // Accounts one committed tick under `lifecycle_mutex_`.
  void record_tick_locked(Clock::duration tick_duration,
                          const TickDeadlineAdvance& advance) noexcept;
  void record_worker_failure(std::exception_ptr failure) noexcept;
  [[nodiscard]] bool is_terminal_state_locked() const noexcept;

  simulation::GameSimulation game_simulation_;
  SnapshotPublication snapshot_publication_;
  // Declared before the mailbox and the sink because both are constructed from it, and before the
  // worker because the worker is the only thread that advances it.
  EntityIdAllocator entity_id_allocator_;
  CommandMailbox command_mailbox_;
  ControllerDirectory controller_directory_;
  CommandSink command_sink_;
  MovementTuningResultDelivery tuning_result_delivery_;

  // Serializes public lifecycle operations while the state mutex coordinates with the worker.
  std::mutex lifecycle_transition_mutex_;
  mutable std::mutex lifecycle_mutex_;
  mutable std::condition_variable_any lifecycle_changed_;
  SimulationRuntimeState lifecycle_state_{SimulationRuntimeState::kReady};
  // Written by the worker under `lifecycle_mutex_`, read by anyone under the same mutex.
  TickStatistics tick_statistics_;
  std::exception_ptr worker_failure_;
  bool worker_joined_{false};

  // This member is last so the run loop can only observe fully initialized runtime state.
  std::jthread simulation_thread_;
};

} // namespace blob_royale::runtime

#endif
