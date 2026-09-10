#include "race/checkpoint_progress_system.hpp"

#include "race/race_test_fixture.hpp"

#include "components/race_progress_component.hpp"
#include "gameplay_validation_error.hpp"
#include "spawn_seating.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] std::uint64_t progress_of(const simulation::GameWorld& world,
                                        const std::uint64_t entity = 1) {
  const simulation::RaceProgress* progress =
      world.store<simulation::RaceProgress>().find(simulation::EntityId::create(entity));
  REQUIRE(progress != nullptr);
  return progress->next_checkpoint;
}

void move_player(simulation::GameWorld& world, const double x, const double y = 320.0) {
  const simulation::EntityId entity = simulation::EntityId::create(1);
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      entity, world.store<simulation::PhysicsBody>().find(entity)->with_position(
                  simulation::Vector2::create(x, y)));
}

} // namespace

TEST_CASE("race gates are taken only in their declared order and a finish retains the body",
          "[unit][gameplay][race][progress]") {
  const testing::TickHarness harness{simulation::TickSequence::create(3), testing::race_test_map()};
  const auto system = gameplay::CheckpointProgressSystem::create(
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration()));
  simulation::GameWorld world =
      testing::race_test_world({simulation::Vector2::create(600.0, 320.0)});

  system->apply(world, harness.context());
  CHECK(progress_of(world) == 0);
  move_player(world, 300.0);
  system->apply(world, harness.context());
  CHECK(progress_of(world) == 1);
  system->apply(world, harness.context());
  CHECK(progress_of(world) == 1);
  move_player(world, 600.0);
  system->apply(world, harness.context());
  CHECK(progress_of(world) == 2);
  system->apply(world, harness.context());
  CHECK(progress_of(world) == 2);
  CHECK(world.store<simulation::PhysicsBody>().find(simulation::EntityId::create(1)) != nullptr);
}

TEST_CASE("overlapping race gates still advance at most one checkpoint per tick",
          "[unit][gameplay][race][progress]") {
  const testing::TickHarness harness{
      simulation::TickSequence::create(3),
      testing::race_test_map("overlapping_gates", {simulation::Vector2::create(300.0, 320.0),
                                                   simulation::Vector2::create(305.0, 320.0)})};
  const auto system = gameplay::CheckpointProgressSystem::create(
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration()));
  simulation::GameWorld world =
      testing::race_test_world({simulation::Vector2::create(300.0, 320.0)});
  system->apply(world, harness.context());
  CHECK(progress_of(world) == 1);
  system->apply(world, harness.context());
  CHECK(progress_of(world) == 2);
}

TEST_CASE("race progress is attached on every running tick only to alive entities",
          "[unit][gameplay][race][progress]") {
  const testing::TickHarness harness{simulation::TickSequence::create(3), testing::race_test_map()};
  const auto system = gameplay::CheckpointProgressSystem::create(
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration()));
  for (const simulation::MatchPhase phase :
       {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
        simulation::MatchPhase::kEnded}) {
    simulation::GameWorld world =
        testing::race_test_world({simulation::Vector2::create(300.0, 320.0)}, phase);
    system->apply(world, harness.context());
    CHECK(world.store<simulation::RaceProgress>().empty());
  }

  simulation::GameWorld world =
      testing::race_test_world({simulation::Vector2::create(100.0, 320.0)});
  system->apply(world, harness.context());
  CHECK(progress_of(world) == 0);
  const simulation::EntityId pending = simulation::EntityId::create(2);
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      pending, simulation::Controllable{simulation::ControllerId::create(2)});
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      simulation::EntityId::create(3),
      simulation::PhysicsBody::create_static(simulation::Vector2::create(300.0, 320.0)));
  system->apply(world, harness.context());
  CHECK(world.store<simulation::RaceProgress>().entries().size() == 1);
  simulation::seat_body_at_rest(world, pending, simulation::Vector2::create(200.0, 320.0), 10.0);
  system->apply(world, harness.context());
  CHECK(progress_of(world, 2) == 0);
}

TEST_CASE("a gate rim is inside including the shared position tolerance",
          "[unit][gameplay][race][progress]") {
  const testing::TickHarness harness{simulation::TickSequence::create(3), testing::race_test_map()};
  const auto configuration = testing::race_test_configuration();
  const auto system = gameplay::CheckpointProgressSystem::create(
      gameplay::RaceCourse::create(harness.map(), configuration));
  simulation::GameWorld world = testing::race_test_world({simulation::Vector2::create(
      300.0 + configuration.checkpoint_radius() + simulation::kPositionTolerance / 2.0, 320.0)});
  system->apply(world, harness.context());
  CHECK(progress_of(world) == 1);
}

TEST_CASE("race progress beyond the course is a named internal failure",
          "[unit][gameplay][race][progress][validation]") {
  const testing::TickHarness harness{simulation::TickSequence::create(3), testing::race_test_map()};
  const auto system = gameplay::CheckpointProgressSystem::create(
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration()));
  simulation::GameWorld world =
      testing::race_test_world({simulation::Vector2::create(300.0, 320.0)});
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(simulation::EntityId::create(1),
                                                                   simulation::RaceProgress{3});
  try {
    system->apply(world, harness.context());
    FAIL("progress beyond the course was accepted");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() == gameplay::GameplayValidationCode::kRaceProgressBeyondCourse);
    CHECK(error.code() == std::string_view{"GAMEPLAY.RACE_PROGRESS_BEYOND_COURSE"});
  }
}
