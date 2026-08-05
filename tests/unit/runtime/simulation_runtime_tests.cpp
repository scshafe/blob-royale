#include "entity_id.hpp"
#include "fixed_delta.hpp"
#include "game_simulation.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "player.hpp"
#include "simulation_config.hpp"
#include "simulation_limits.hpp"
#include "simulation_runtime.hpp"
#include "simulation_runtime_lifecycle_error.hpp"
#include "simulation_runtime_state.hpp"
#include "simulation_validation_error.hpp"
#include "snapshot_publication.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <latch>
#include <memory>
#include <semaphore>
#include <thread>
#include <vector>

namespace runtime = blob_royale::runtime;
namespace simulation = blob_royale::simulation;

namespace {

using namespace std::chrono_literals;

constexpr double kWorldWidth = 100.0;
constexpr double kWorldHeight = 80.0;
constexpr double kPlayerRadius = 1.0;
constexpr std::uint64_t kTickRate = simulation::SimulationConfig::kRequiredTicksPerSecond;
constexpr std::uint64_t kGridColumns = 10;
constexpr std::uint64_t kGridRows = 8;
constexpr simulation::EntityId::Value kFirstPlayerId = 3;
constexpr simulation::EntityId::Value kSecondPlayerId = 9;
constexpr double kFirstPlayerPositionX = 12.0;
constexpr double kFirstPlayerPositionY = 14.0;
constexpr double kSecondPlayerPositionX = 72.0;
constexpr double kSecondPlayerPositionY = 64.0;
constexpr std::size_t kReaderCount = 8;
constexpr simulation::TickSequence::Value kConcurrentReaderTickTarget = 24;
constexpr std::size_t kPausedReadCount = 4'096;
constexpr std::size_t kLifecycleCycleCount = 12;
constexpr std::size_t kConcurrentLifecycleCallerCount = 6;
constexpr simulation::TickSequence::Value kCadenceObservationTickTarget = 65;
constexpr auto kTestDeadline = 5s;
constexpr auto kCadenceExpectedElapsed =
    simulation::FixedDelta::canonical().duration() *
    static_cast<std::int64_t>(kCadenceObservationTickTarget - 1);
constexpr auto kCadenceMinimumElapsed = kCadenceExpectedElapsed / 2;

struct ConcurrentLifecycleOperationResult final {
  bool all_callers_returned;
  bool unexpected_exception_observed;
};

[[nodiscard]] simulation::SimulationConfig simulation_config_fixture() {
  return simulation::SimulationConfig::create(kWorldWidth, kWorldHeight, kPlayerRadius, kTickRate,
                                              kGridColumns, kGridRows);
}

[[nodiscard]] simulation::Player player_fixture(const simulation::EntityId::Value entity_id,
                                                const double position_x, const double position_y) {
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  return simulation::Player::create(
      simulation::EntityId::create(entity_id),
      simulation::PhysicsBody::create(simulation::Vector2::create(position_x, position_y), zero,
                                      zero));
}

[[nodiscard]] simulation::GameSimulation stable_simulation_fixture() {
  return simulation::GameSimulation::create(
      simulation_config_fixture(),
      simulation::GameWorld::create(
          {player_fixture(kSecondPlayerId, kSecondPlayerPositionX, kSecondPlayerPositionY),
           player_fixture(kFirstPlayerId, kFirstPlayerPositionX, kFirstPlayerPositionY)}));
}

[[nodiscard]] simulation::GameSimulation empty_simulation_fixture() {
  return simulation::GameSimulation::create(simulation_config_fixture(),
                                            simulation::GameWorld::create({}));
}

[[nodiscard]] simulation::GameSimulation failing_simulation_fixture() {
  const double maximum = simulation::kMaximumPhysicalComponentMagnitude;
  const simulation::Vector2 maximum_vector = simulation::Vector2::create(maximum, maximum);
  const simulation::Player player = simulation::Player::create(
      simulation::EntityId::create(kFirstPlayerId),
      simulation::PhysicsBody::create(
          simulation::Vector2::create(kFirstPlayerPositionX, kFirstPlayerPositionY), maximum_vector,
          maximum_vector));
  return simulation::GameSimulation::create(simulation_config_fixture(),
                                            simulation::GameWorld::create({player}));
}

[[nodiscard]] bool
wait_for_tick(const runtime::SnapshotPublication& publication,
              const simulation::TickSequence::Value target,
              const std::chrono::steady_clock::duration timeout = kTestDeadline) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (publication.latest()->tick_sequence().value() >= target) {
      return true;
    }
    std::this_thread::yield();
  }
  return publication.latest()->tick_sequence().value() >= target;
}

[[nodiscard]] bool
wait_for_ready_tick(const runtime::SnapshotPublication& publication,
                    const simulation::TickSequence::Value target,
                    const std::chrono::steady_clock::duration timeout = kTestDeadline) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    // Readiness is the acquire barrier that makes its preceding snapshot publication visible.
    if (publication.is_ready() && publication.latest()->tick_sequence().value() >= target) {
      return true;
    }
    std::this_thread::yield();
  }
  return publication.is_ready() && publication.latest()->tick_sequence().value() >= target;
}

[[nodiscard]] bool
wait_for_latch(const std::latch& latch,
               const std::chrono::steady_clock::duration timeout = kTestDeadline) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (latch.try_wait()) {
      return true;
    }
    std::this_thread::yield();
  }
  return latch.try_wait();
}

template <typename Operation>
[[nodiscard]] ConcurrentLifecycleOperationResult
run_concurrent_lifecycle_operation(Operation operation) {
  std::barrier start_line(static_cast<std::ptrdiff_t>(kConcurrentLifecycleCallerCount + 1));
  std::latch callers_returned(static_cast<std::ptrdiff_t>(kConcurrentLifecycleCallerCount));
  std::atomic<bool> unexpected_exception_observed{false};
  std::vector<std::jthread> callers;
  callers.reserve(kConcurrentLifecycleCallerCount);

  for (std::size_t caller_index = 0; caller_index < kConcurrentLifecycleCallerCount;
       ++caller_index) {
    callers.emplace_back([&] {
      start_line.arrive_and_wait();
      try {
        operation();
      } catch (...) {
        unexpected_exception_observed.store(true, std::memory_order_release);
      }
      callers_returned.count_down();
    });
  }

  start_line.arrive_and_wait();
  const bool all_callers_returned = wait_for_latch(callers_returned);
  for (std::jthread& caller : callers) {
    caller.join();
  }

  return ConcurrentLifecycleOperationResult{
      all_callers_returned, unexpected_exception_observed.load(std::memory_order_acquire)};
}

[[nodiscard]] bool snapshot_is_coherent(const simulation::WorldSnapshot& snapshot) {
  if (snapshot.players().size() != 2) {
    return false;
  }
  const auto& first_player = snapshot.players()[0];
  const auto& second_player = snapshot.players()[1];
  return first_player.entity_id().value() == kFirstPlayerId &&
         second_player.entity_id().value() == kSecondPlayerId &&
         first_player.position() ==
             simulation::Vector2::create(kFirstPlayerPositionX, kFirstPlayerPositionY) &&
         second_player.position() ==
             simulation::Vector2::create(kSecondPlayerPositionX, kSecondPlayerPositionY);
}

} // namespace

TEST_CASE("SimulationRuntime repeats lifecycle transitions on one coherent timeline",
          "[unit][runtime][lifecycle]") {
  runtime::SimulationRuntime simulation_runtime(stable_simulation_fixture());
  const auto initial_snapshot = simulation_runtime.snapshot_publication().latest();

  REQUIRE(initial_snapshot);
  CHECK(simulation_runtime.state() == runtime::SimulationRuntimeState::kReady);
  CHECK_FALSE(simulation_runtime.snapshot_publication().is_ready());
  CHECK(initial_snapshot->tick_sequence() == simulation::TickSequence::zero());

  simulation_runtime.pause();
  simulation_runtime.pause();
  CHECK(simulation_runtime.state() == runtime::SimulationRuntimeState::kPaused);
  CHECK_FALSE(simulation_runtime.snapshot_publication().is_ready());
  simulation_runtime.start();
  simulation_runtime.start();
  for (std::size_t cycle = 0; cycle < kLifecycleCycleCount; ++cycle) {
    const simulation::TickSequence::Value target =
        simulation_runtime.snapshot_publication().latest()->tick_sequence().value() + 1;
    REQUIRE(wait_for_ready_tick(simulation_runtime.snapshot_publication(), target));
    CHECK(simulation_runtime.snapshot_publication().is_ready());

    simulation_runtime.pause();
    simulation_runtime.pause();
    REQUIRE(simulation_runtime.state() == runtime::SimulationRuntimeState::kPaused);
    CHECK_FALSE(simulation_runtime.snapshot_publication().is_ready());
    const auto paused_snapshot = simulation_runtime.snapshot_publication().latest();
    const simulation::TickSequence paused_sequence = paused_snapshot->tick_sequence();
    for (std::size_t read = 0; read < kPausedReadCount; ++read) {
      CHECK(simulation_runtime.snapshot_publication().latest()->tick_sequence() == paused_sequence);
    }
    CHECK(initial_snapshot->tick_sequence() == simulation::TickSequence::zero());

    if ((cycle % 2U) == 0U) {
      simulation_runtime.resume();
      simulation_runtime.resume();
    } else {
      simulation_runtime.start();
      simulation_runtime.start();
    }
  }

  simulation_runtime.stop();
  simulation_runtime.stop();
  CHECK(simulation_runtime.state() == runtime::SimulationRuntimeState::kStopped);
  CHECK_FALSE(simulation_runtime.snapshot_publication().is_ready());
  CHECK_THROWS_AS(simulation_runtime.start(), runtime::SimulationRuntimeLifecycleError);
  CHECK_THROWS_AS(simulation_runtime.resume(), runtime::SimulationRuntimeLifecycleError);
  CHECK_THROWS_AS(simulation_runtime.pause(), runtime::SimulationRuntimeLifecycleError);
}

TEST_CASE("SimulationRuntime publishes monotonic coherent snapshots to concurrent retained readers",
          "[unit][runtime][publication][concurrency]") {
  runtime::SimulationRuntime simulation_runtime(stable_simulation_fixture());
  const runtime::SnapshotPublication& publication = simulation_runtime.snapshot_publication();
  std::latch readers_ready(kReaderCount);
  std::latch readers_start(1);
  std::atomic<bool> readers_done{false};
  std::atomic<bool> reader_failed{false};
  std::vector<std::jthread> readers;
  readers.reserve(kReaderCount);

  for (std::size_t reader_index = 0; reader_index < kReaderCount; ++reader_index) {
    readers.emplace_back([&](const std::stop_token stop_token) {
      readers_ready.count_down();
      readers_start.wait();
      simulation::TickSequence::Value previous_sequence = 0;
      std::shared_ptr<const simulation::WorldSnapshot> retained_snapshot;
      simulation::TickSequence::Value retained_sequence = 0;

      while (!stop_token.stop_requested() && !readers_done.load(std::memory_order_acquire)) {
        const auto snapshot = publication.latest();
        const simulation::TickSequence::Value sequence = snapshot->tick_sequence().value();
        if (sequence < previous_sequence || !snapshot_is_coherent(*snapshot)) {
          reader_failed.store(true, std::memory_order_release);
          return;
        }
        if (retained_snapshot && retained_snapshot->tick_sequence().value() != retained_sequence) {
          reader_failed.store(true, std::memory_order_release);
          return;
        }
        retained_snapshot = snapshot;
        retained_sequence = sequence;
        previous_sequence = sequence;
        std::this_thread::yield();
      }
    });
  }

  const bool all_readers_ready = wait_for_latch(readers_ready);
  readers_start.count_down();
  REQUIRE(all_readers_ready);
  simulation_runtime.start();
  REQUIRE(wait_for_ready_tick(publication, kConcurrentReaderTickTarget));
  CHECK(publication.is_ready());
  simulation_runtime.pause();
  CHECK_FALSE(publication.is_ready());
  readers_done.store(true, std::memory_order_release);
  for (std::jthread& reader : readers) {
    reader.request_stop();
    reader.join();
  }

  CHECK_FALSE(reader_failed.load(std::memory_order_acquire));
  CHECK(publication.latest()->tick_sequence().value() >= kConcurrentReaderTickTarget);
}

TEST_CASE("SimulationRuntime destruction joins an active worker within a bounded deadline",
          "[unit][runtime][lifecycle]") {
  std::binary_semaphore destruction_completed(0);
  std::jthread owner([&] {
    {
      runtime::SimulationRuntime simulation_runtime(stable_simulation_fixture());
      simulation_runtime.start();
      if (!wait_for_tick(simulation_runtime.snapshot_publication(), 1)) {
        return;
      }
    }
    destruction_completed.release();
  });

  CHECK(destruction_completed.try_acquire_for(kTestDeadline));
}

TEST_CASE("SimulationRuntime serializes concurrent pause start and stop callers",
          "[unit][runtime][lifecycle][concurrency]") {
  runtime::SimulationRuntime simulation_runtime(stable_simulation_fixture());
  simulation_runtime.start();
  REQUIRE(wait_for_ready_tick(simulation_runtime.snapshot_publication(), 1));

  const ConcurrentLifecycleOperationResult pause_result =
      run_concurrent_lifecycle_operation([&] { simulation_runtime.pause(); });
  REQUIRE(pause_result.all_callers_returned);
  CHECK_FALSE(pause_result.unexpected_exception_observed);
  CHECK(simulation_runtime.state() == runtime::SimulationRuntimeState::kPaused);
  CHECK_FALSE(simulation_runtime.snapshot_publication().is_ready());
  const simulation::TickSequence paused_sequence =
      simulation_runtime.snapshot_publication().latest()->tick_sequence();

  const ConcurrentLifecycleOperationResult start_result =
      run_concurrent_lifecycle_operation([&] { simulation_runtime.start(); });
  REQUIRE(start_result.all_callers_returned);
  CHECK_FALSE(start_result.unexpected_exception_observed);
  REQUIRE(
      wait_for_ready_tick(simulation_runtime.snapshot_publication(), paused_sequence.value() + 1));
  CHECK(simulation_runtime.state() == runtime::SimulationRuntimeState::kRunning);

  const ConcurrentLifecycleOperationResult stop_result =
      run_concurrent_lifecycle_operation([&] { simulation_runtime.stop(); });
  REQUIRE(stop_result.all_callers_returned);
  CHECK_FALSE(stop_result.unexpected_exception_observed);
  CHECK(simulation_runtime.state() == runtime::SimulationRuntimeState::kStopped);
  CHECK_FALSE(simulation_runtime.snapshot_publication().is_ready());
}

TEST_CASE("SimulationRuntime reaches stopped under a mixed lifecycle caller race",
          "[unit][runtime][lifecycle][concurrency]") {
  runtime::SimulationRuntime simulation_runtime(stable_simulation_fixture());
  simulation_runtime.start();
  REQUIRE(wait_for_ready_tick(simulation_runtime.snapshot_publication(), 1));

  std::barrier start_line(4);
  std::latch callers_returned(3);
  std::atomic<bool> unexpected_exception_observed{false};
  const auto run_terminal_race_operation = [&](auto operation) {
    start_line.arrive_and_wait();
    try {
      operation();
    } catch (const runtime::SimulationRuntimeLifecycleError&) {
      // Start or pause may lose the serialization race to the terminal stop operation.
    } catch (...) {
      unexpected_exception_observed.store(true, std::memory_order_release);
    }
    callers_returned.count_down();
  };

  std::jthread pause_caller(
      [&] { run_terminal_race_operation([&] { simulation_runtime.pause(); }); });
  std::jthread start_caller(
      [&] { run_terminal_race_operation([&] { simulation_runtime.start(); }); });
  std::jthread stop_caller(
      [&] { run_terminal_race_operation([&] { simulation_runtime.stop(); }); });

  start_line.arrive_and_wait();
  const bool all_callers_returned = wait_for_latch(callers_returned);
  pause_caller.join();
  start_caller.join();
  stop_caller.join();

  REQUIRE(all_callers_returned);
  CHECK_FALSE(unexpected_exception_observed.load(std::memory_order_acquire));
  CHECK(simulation_runtime.state() == runtime::SimulationRuntimeState::kStopped);
  CHECK_FALSE(simulation_runtime.snapshot_publication().is_ready());
}

TEST_CASE("SimulationRuntime observes the fixed cadence instead of free running",
          "[unit][runtime][lifecycle][cadence]") {
  runtime::SimulationRuntime simulation_runtime(empty_simulation_fixture());

  const auto observation_started_at = std::chrono::steady_clock::now();
  simulation_runtime.start();
  REQUIRE(wait_for_ready_tick(simulation_runtime.snapshot_publication(),
                              kCadenceObservationTickTarget));
  const auto target_observed_at = std::chrono::steady_clock::now();
  simulation_runtime.pause();

  const auto observed_elapsed = target_observed_at - observation_started_at;
  CHECK(observed_elapsed >= kCadenceMinimumElapsed);
  CHECK_FALSE(simulation_runtime.snapshot_publication().is_ready());
}

TEST_CASE("SimulationRuntime preserves the last complete snapshot and original worker failure",
          "[unit][runtime][failure]") {
  runtime::SimulationRuntime simulation_runtime(failing_simulation_fixture());

  simulation_runtime.start();
  REQUIRE(
      simulation_runtime.wait_for_state(runtime::SimulationRuntimeState::kFailed, kTestDeadline));

  CHECK(simulation_runtime.state() == runtime::SimulationRuntimeState::kFailed);
  CHECK_FALSE(simulation_runtime.snapshot_publication().is_ready());
  CHECK(simulation_runtime.snapshot_publication().latest()->tick_sequence() ==
        simulation::TickSequence::zero());
  CHECK_THROWS_AS(simulation_runtime.rethrow_if_failed(), simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation_runtime.pause(), simulation::SimulationValidationError);

  simulation_runtime.stop();
  simulation_runtime.stop();
  CHECK(simulation_runtime.state() == runtime::SimulationRuntimeState::kFailed);
  CHECK_THROWS_AS(simulation_runtime.resume(), simulation::SimulationValidationError);
}
