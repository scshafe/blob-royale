#include "race/checkpoint_progress_system.hpp"

#include "race/race_test_fixture.hpp"

#include "components/race_progress_component.hpp"
#include "events/elimination_event.hpp"
#include "events/race_checkpoint_event.hpp"
#include "gameplay_validation_error.hpp"
#include "spawn_seating.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] std::uint64_t progress_of(const simulation::GameWorld& world,
                                        const std::uint64_t entity = 1) {
  const auto* progress =
      world.store<simulation::RaceProgress>().find(simulation::EntityId::create(entity));
  REQUIRE(progress != nullptr);
  return progress->next_checkpoint;
}

void credit(simulation::GameWorld& world, const std::uint64_t next,
            const simulation::MotionTime time = simulation::MotionTime::start()) {
  world.emit(simulation::RaceCheckpointEvent{simulation::EntityId::create(1), next, time});
}

} // namespace

TEST_CASE("race endpoint occupancy alone never manufactures certified gate credit",
          "[unit][gameplay][race][progress]") {
  const testing::TickHarness harness{simulation::TickSequence::create(3), testing::race_test_map()};
  const auto system = gameplay::CheckpointProgressSystem::create(
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration()));
  auto world = testing::race_test_world({simulation::Vector2::create(300.0, 320.0)});
  system->apply(world, harness.context());
  CHECK(progress_of(world) == 0);
}

TEST_CASE("multiple certified gate facts advance progress and finish clears all movement intent",
          "[unit][gameplay][race][progress]") {
  const testing::TickHarness harness{simulation::TickSequence::create(3), testing::race_test_map()};
  const auto system = gameplay::CheckpointProgressSystem::create(
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration()));
  auto world = testing::race_test_world({simulation::Vector2::create(100.0, 320.0)});
  auto* control =
      world.mutable_store<simulation::Controllable>().mutable_find(simulation::EntityId::create(1));
  control->normalized_thrust_intent = simulation::Vector2::create(1.0, 0.0);
  control->commands_this_tick.push_back(testing::thrust_command(1, 1.0, 0.0));
  credit(world, 1, simulation::MotionTime::create(0.25));
  credit(world, 2, simulation::MotionTime::create(0.75));
  system->apply(world, harness.context());
  CHECK(progress_of(world) == 2);
  CHECK(control->normalized_thrust_intent == simulation::Vector2::create(0.0, 0.0));
  CHECK(control->commands_this_tick.empty());
  CHECK(world.store<simulation::PhysicsBody>().size() == 1);
}

TEST_CASE("a certified gate remains earned after a later fall removes the body",
          "[unit][gameplay][race][progress]") {
  const testing::TickHarness harness{simulation::TickSequence::create(3), testing::race_test_map()};
  const auto system = gameplay::CheckpointProgressSystem::create(
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration()));
  auto world = testing::race_test_world({simulation::Vector2::create(100.0, 320.0)});
  credit(world, 1, simulation::MotionTime::create(0.25));
  world.emit(simulation::EliminationEvent{simulation::EntityId::create(1)});
  world.mutable_store<simulation::PhysicsBody>().erase(simulation::EntityId::create(1));
  system->apply(world, harness.context());
  CHECK(progress_of(world) == 1);
}

TEST_CASE("race progress zero is attached only to alive running entities",
          "[unit][gameplay][race][progress]") {
  const testing::TickHarness harness{simulation::TickSequence::create(3), testing::race_test_map()};
  const auto system = gameplay::CheckpointProgressSystem::create(
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration()));
  for (const auto phase : {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
                           simulation::MatchPhase::kEnded}) {
    auto world = testing::race_test_world({simulation::Vector2::create(300.0, 320.0)}, phase);
    system->apply(world, harness.context());
    CHECK(world.store<simulation::RaceProgress>().empty());
  }
  auto world = testing::race_test_world({simulation::Vector2::create(100.0, 320.0)});
  const auto pending = simulation::EntityId::create(2);
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      pending, simulation::Controllable{simulation::ControllerId::create(2)});
  system->apply(world, harness.context());
  CHECK(progress_of(world) == 0);
  CHECK(world.store<simulation::RaceProgress>().size() == 1);
  simulation::seat_body_at_rest(world, pending, simulation::Vector2::create(200.0, 320.0), 10.0);
  system->apply(world, harness.context());
  CHECK(progress_of(world, 2) == 0);
}

TEST_CASE("race progress beyond the course fails visibly",
          "[unit][gameplay][race][progress][validation]") {
  const testing::TickHarness harness{simulation::TickSequence::create(3), testing::race_test_map()};
  const auto system = gameplay::CheckpointProgressSystem::create(
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration()));
  auto world = testing::race_test_world({simulation::Vector2::create(300.0, 320.0)});
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(simulation::EntityId::create(1),
                                                                   simulation::RaceProgress{3});
  try {
    system->apply(world, harness.context());
    FAIL("progress beyond the course was accepted");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() == gameplay::GameplayValidationCode::kRaceProgressBeyondCourse);
  }
}

TEST_CASE("checkpoint facts cannot skip gates or credit a missing controller",
          "[unit][gameplay][race][progress][validation]") {
  const testing::TickHarness harness{simulation::TickSequence::create(3), testing::race_test_map()};
  const auto system = gameplay::CheckpointProgressSystem::create(
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration()));
  for (const bool missing_controller : {false, true}) {
    auto world = testing::race_test_world({simulation::Vector2::create(100.0, 320.0)});
    if (missing_controller) {
      world.mutable_store<simulation::Controllable>().erase(simulation::EntityId::create(1));
    }
    credit(world, missing_controller ? 1 : 2);
    try {
      system->apply(world, harness.context());
      FAIL("invalid checkpoint fact was accepted");
    } catch (const gameplay::GameplayValidationError& error) {
      CHECK(error.validation_code() ==
            gameplay::GameplayValidationCode::kRaceCheckpointEventInvalid);
    }
  }
}
