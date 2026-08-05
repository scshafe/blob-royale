#ifndef BLOB_ROYALE_RUNTIME_SIMULATION_RUNTIME_HPP
#define BLOB_ROYALE_RUNTIME_SIMULATION_RUNTIME_HPP

#include "game_simulation.hpp"
#include "simulation_runtime_state.hpp"
#include "snapshot_publication.hpp"

#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <stop_token>
#include <thread>

namespace blob_royale::runtime {

// canonical: simulation_runtime -- sole clock and mutation owner for one simulation timeline.
class SimulationRuntime final {
public:
  // Takes exclusive ownership of a validated simulation and publishes its coherent initial state.
  explicit SimulationRuntime(simulation::GameSimulation game_simulation);

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

private:
  using Clock = std::chrono::steady_clock;

  void transition_to_running(const char* operation);
  void run(std::stop_token stop_token) noexcept;
  void record_worker_failure(std::exception_ptr failure) noexcept;
  [[nodiscard]] bool is_terminal_state_locked() const noexcept;

  simulation::GameSimulation game_simulation_;
  SnapshotPublication snapshot_publication_;

  // Serializes public lifecycle operations while the state mutex coordinates with the worker.
  std::mutex lifecycle_transition_mutex_;
  mutable std::mutex lifecycle_mutex_;
  mutable std::condition_variable_any lifecycle_changed_;
  SimulationRuntimeState lifecycle_state_{SimulationRuntimeState::kReady};
  std::exception_ptr worker_failure_;
  bool worker_joined_{false};

  // This member is last so the run loop can only observe fully initialized runtime state.
  std::jthread simulation_thread_;
};

} // namespace blob_royale::runtime

#endif
