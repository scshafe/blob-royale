#include "simulation_runtime.hpp"

#include "command_registry.hpp"
#include "entity_id_reservation.hpp"
#include "fixed_delta.hpp"
#include "input_batch.hpp"
#include "simulation_runtime_lifecycle_error.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <stop_token>
#include <utility>
#include <vector>

namespace blob_royale::runtime {

namespace {

// The reservation width a tick needs is its spawn count plus the system headroom, so the drained
// commands are counted before the block is asked for. Counted here rather than tracked by the
// mailbox because a superseded spawn must not be counted twice and only the drained vector knows
// what actually survived to this tick.
[[nodiscard]] std::uint64_t
spawn_command_count(const std::vector<simulation::Command>& commands) noexcept {
  std::uint64_t count = 0;
  for (const simulation::Command& command : commands) {
    if (simulation::command_kind_of(command) == simulation::CommandKind::kSpawn) {
      ++count;
    }
  }
  return count;
}

} // namespace

SimulationRuntime::SimulationRuntime(simulation::GameSimulation game_simulation)
    : game_simulation_(std::move(game_simulation)),
      snapshot_publication_(game_simulation_.snapshot()),
      entity_id_allocator_(EntityIdAllocator::above_committed_state(game_simulation_)),
      command_mailbox_(game_simulation_.accepted_command_kinds()),
      command_sink_(command_mailbox_, controller_directory_, entity_id_allocator_),
      simulation_thread_([this](const std::stop_token stop_token) { run(stop_token); }) {}

SimulationRuntime::~SimulationRuntime() { stop(); }

void SimulationRuntime::start() { transition_to_running("start"); }

void SimulationRuntime::resume() { transition_to_running("resume"); }

void SimulationRuntime::transition_to_running(const char* const operation) {
  const std::lock_guard transition_lock(lifecycle_transition_mutex_);
  std::unique_lock lock(lifecycle_mutex_);
  lifecycle_changed_.wait(lock,
                          [this] { return lifecycle_state_ != SimulationRuntimeState::kPausing; });

  if (worker_failure_) {
    const std::exception_ptr failure = worker_failure_;
    lock.unlock();
    std::rethrow_exception(failure);
  }

  if (lifecycle_state_ == SimulationRuntimeState::kStopped ||
      lifecycle_state_ == SimulationRuntimeState::kStopping) {
    throw SimulationRuntimeLifecycleError(operation, lifecycle_state_);
  }
  if (lifecycle_state_ == SimulationRuntimeState::kRunning) {
    return;
  }

  lifecycle_state_ = SimulationRuntimeState::kRunning;
  lock.unlock();
  lifecycle_changed_.notify_all();
}

void SimulationRuntime::pause() {
  const std::lock_guard transition_lock(lifecycle_transition_mutex_);
  std::unique_lock lock(lifecycle_mutex_);

  if (worker_failure_) {
    const std::exception_ptr failure = worker_failure_;
    lock.unlock();
    std::rethrow_exception(failure);
  }

  if (lifecycle_state_ == SimulationRuntimeState::kReady) {
    snapshot_publication_.mark_not_ready();
    lifecycle_state_ = SimulationRuntimeState::kPaused;
    lock.unlock();
    lifecycle_changed_.notify_all();
    return;
  }
  if (lifecycle_state_ == SimulationRuntimeState::kPaused) {
    return;
  }
  if (lifecycle_state_ == SimulationRuntimeState::kStopped ||
      lifecycle_state_ == SimulationRuntimeState::kStopping) {
    throw SimulationRuntimeLifecycleError("pause", lifecycle_state_);
  }
  if (lifecycle_state_ == SimulationRuntimeState::kRunning) {
    lifecycle_state_ = SimulationRuntimeState::kPausing;
    lifecycle_changed_.notify_all();
  }

  lifecycle_changed_.wait(lock,
                          [this] { return lifecycle_state_ != SimulationRuntimeState::kPausing; });
  if (lifecycle_state_ == SimulationRuntimeState::kStopping) {
    lifecycle_changed_.wait(lock, [this] {
      return lifecycle_state_ == SimulationRuntimeState::kStopped ||
             lifecycle_state_ == SimulationRuntimeState::kFailed;
    });
  }
  if (worker_failure_) {
    const std::exception_ptr failure = worker_failure_;
    lock.unlock();
    std::rethrow_exception(failure);
  }
}

void SimulationRuntime::stop() noexcept {
  const std::lock_guard transition_lock(lifecycle_transition_mutex_);
  std::unique_lock lock(lifecycle_mutex_);
  if (worker_joined_) {
    return;
  }

  snapshot_publication_.mark_not_ready();
  if (lifecycle_state_ != SimulationRuntimeState::kFailed) {
    lifecycle_state_ = SimulationRuntimeState::kStopping;
  }
  simulation_thread_.request_stop();
  lock.unlock();
  lifecycle_changed_.notify_all();

  if (simulation_thread_.joinable()) {
    simulation_thread_.join();
  }

  lock.lock();
  worker_joined_ = true;
  if (lifecycle_state_ != SimulationRuntimeState::kFailed) {
    lifecycle_state_ = SimulationRuntimeState::kStopped;
  }
  lock.unlock();
  lifecycle_changed_.notify_all();
}

SimulationRuntimeState SimulationRuntime::state() const noexcept {
  const std::lock_guard lock(lifecycle_mutex_);
  return lifecycle_state_;
}

bool SimulationRuntime::wait_for_state(const SimulationRuntimeState expected_state,
                                       const std::chrono::steady_clock::duration timeout) const {
  std::unique_lock lock(lifecycle_mutex_);
  return lifecycle_changed_.wait_for(lock, timeout, [this, expected_state] {
    return lifecycle_state_ == expected_state ||
           (is_terminal_state_locked() && lifecycle_state_ != expected_state);
  }) && lifecycle_state_ == expected_state;
}

void SimulationRuntime::rethrow_if_failed() const {
  std::exception_ptr failure;
  {
    const std::lock_guard lock(lifecycle_mutex_);
    failure = worker_failure_;
  }
  if (failure) {
    std::rethrow_exception(failure);
  }
}

void SimulationRuntime::run(const std::stop_token stop_token) noexcept {
  const simulation::FixedDelta fixed_delta = simulation::FixedDelta::canonical();
  const Clock::duration tick_duration = fixed_delta.duration();
  std::unique_lock lock(lifecycle_mutex_);

  while (!stop_token.stop_requested()) {
    lifecycle_changed_.wait(lock, stop_token, [this] {
      return lifecycle_state_ == SimulationRuntimeState::kRunning ||
             lifecycle_state_ == SimulationRuntimeState::kStopping;
    });
    if (stop_token.stop_requested() || lifecycle_state_ == SimulationRuntimeState::kStopping) {
      return;
    }

    Clock::time_point next_tick = Clock::now();
    while (!stop_token.stop_requested() && lifecycle_state_ == SimulationRuntimeState::kRunning) {
      lock.unlock();
      std::shared_ptr<const simulation::WorldSnapshot> completed_snapshot;
      try {
        // Exactly one drain per tick. A second drainer would hand one tick's commands to two ticks,
        // which is why nothing but this worker may call `drain`.
        std::vector<simulation::Command> commands = command_mailbox_.drain();
        const simulation::EntityIdReservation reservation =
            entity_id_allocator_.reserve_for_tick(spawn_command_count(commands));
        // Never `InputBatch::empty()`: the reservation is non-empty on every tick, so a system that
        // must create an entity on its first running tick always has an id to draw.
        const simulation::InputBatch input_batch = simulation::InputBatch::create(
            std::move(commands), command_mailbox_.accepted_kinds(), reservation);
        game_simulation_.step(fixed_delta, input_batch);
        completed_snapshot =
            std::make_shared<const simulation::WorldSnapshot>(game_simulation_.snapshot());
      } catch (...) {
        // A throw from `InputBatch::create` means the sink and the engine disagree about the
        // running mode or about what a command may carry, which
        // `docs/architecture/0003-deterministic-simulation-contract.md` § "Accepted simulation
        // input" keeps a hard failure rather than a dropped input.
        record_worker_failure(std::current_exception());
        return;
      }
      lock.lock();

      if (stop_token.stop_requested() || lifecycle_state_ == SimulationRuntimeState::kStopping) {
        return;
      }
      snapshot_publication_.publish(std::move(completed_snapshot));

      if (lifecycle_state_ == SimulationRuntimeState::kPausing) {
        snapshot_publication_.mark_not_ready();
        lifecycle_state_ = SimulationRuntimeState::kPaused;
        lifecycle_changed_.notify_all();
        break;
      }

      next_tick += tick_duration;
      lifecycle_changed_.wait_until(lock, stop_token, next_tick, [this] {
        return lifecycle_state_ != SimulationRuntimeState::kRunning;
      });
      if (lifecycle_state_ == SimulationRuntimeState::kPausing) {
        snapshot_publication_.mark_not_ready();
        lifecycle_state_ = SimulationRuntimeState::kPaused;
        lifecycle_changed_.notify_all();
      }
    }
  }
}

void SimulationRuntime::record_worker_failure(std::exception_ptr failure) noexcept {
  const std::lock_guard lock(lifecycle_mutex_);
  snapshot_publication_.mark_not_ready();
  worker_failure_ = std::move(failure);
  lifecycle_state_ = SimulationRuntimeState::kFailed;
  lifecycle_changed_.notify_all();
}

bool SimulationRuntime::is_terminal_state_locked() const noexcept {
  return lifecycle_state_ == SimulationRuntimeState::kStopped ||
         lifecycle_state_ == SimulationRuntimeState::kFailed;
}

} // namespace blob_royale::runtime
