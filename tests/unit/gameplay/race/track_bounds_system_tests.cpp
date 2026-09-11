#include "race/track_bounds_system.hpp"

#include "race/race_test_fixture.hpp"

#include "components/race_progress_component.hpp"
#include "components/respawn_timer_component.hpp"
#include "events/elimination_event.hpp"
#include "race/race_mode.hpp"

#include <catch2/catch_test_macros.hpp>

#include <variant>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] std::vector<simulation::EntityId> eliminated_in(const simulation::GameWorld& world) {
  std::vector<simulation::EntityId> entities;
  for (const simulation::WorldEvent& event : world.events()) {
    if (const auto* elimination = std::get_if<simulation::EliminationEvent>(&event);
        elimination != nullptr) {
      entities.push_back(elimination->entity);
    }
  }
  return entities;
}

} // namespace

TEST_CASE("the track corridor includes its boundary and tolerance but eliminates an outside centre",
          "[unit][gameplay][race][bounds]") {
  const auto configuration = testing::race_test_configuration();
  const testing::TickHarness harness{simulation::TickSequence::create(1), testing::race_test_map()};
  const auto course = gameplay::RaceCourse::create(harness.map(), configuration);
  const auto system = gameplay::TrackBoundsSystem::create(course);
  for (const double offset : {0.0, simulation::kPositionTolerance / 2.0}) {
    simulation::GameWorld world = testing::race_test_world(
        {simulation::Vector2::create(300.0, 320.0 + course.track_half_width() + offset)});
    system->apply(world, harness.context());
    CHECK(eliminated_in(world).empty());
  }
  simulation::GameWorld outside = testing::race_test_world(
      {simulation::Vector2::create(300.0, 321.0 + course.track_half_width())});
  system->apply(outside, harness.context());
  CHECK(eliminated_in(outside) ==
        std::vector<simulation::EntityId>{simulation::EntityId::create(1)});
}

TEST_CASE("track bounds evaluates only alive racers while running in entity order",
          "[unit][gameplay][race][bounds]") {
  const testing::TickHarness harness{simulation::TickSequence::create(1), testing::race_test_map()};
  const auto system = gameplay::TrackBoundsSystem::create(
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration()));
  for (const simulation::MatchPhase phase :
       {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
        simulation::MatchPhase::kEnded}) {
    simulation::GameWorld world =
        testing::race_test_world({simulation::Vector2::create(300.0, 450.0)}, phase);
    system->apply(world, harness.context());
    CHECK(eliminated_in(world).empty());
  }

  simulation::GameWorld world = testing::race_test_world(
      {simulation::Vector2::create(300.0, 450.0), simulation::Vector2::create(500.0, 450.0),
       simulation::Vector2::create(600.0, 450.0)});
  world.mutable_store<simulation::PhysicsBody>().erase(simulation::EntityId::create(2));
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      simulation::EntityId::create(4),
      simulation::PhysicsBody::create_static(simulation::Vector2::create(700.0, 450.0)));
  system->apply(world, harness.context());
  CHECK(eliminated_in(world) == std::vector<simulation::EntityId>{simulation::EntityId::create(1),
                                                                  simulation::EntityId::create(3)});
}

TEST_CASE("an off-road racer loses its body in the same committed tick through the declared mode",
          "[unit][gameplay][race][bounds][match]") {
  const auto configuration = testing::race_test_configuration();
  simulation::GameWorld world =
      testing::race_test_world({simulation::Vector2::create(300.0, 450.0)});
  testing::SteppedGame driver{simulation::GameSimulation::create(
      testing::gameplay_configuration(), std::move(world),
      simulation::GameSimulationSetup::of_mode(testing::race_test_map(),
                                               gameplay::RaceMode::create(configuration)))};
  const simulation::WorldSnapshot snapshot = driver.step();
  CHECK_FALSE(testing::published_body(snapshot, 1).has_value());
  REQUIRE(snapshot.components<simulation::Controllable>().size() == 1);
  REQUIRE(snapshot.components<simulation::RaceProgress>().size() == 1);
  CHECK(snapshot.components<simulation::RaceProgress>()[0].value.next_checkpoint == 0);
  REQUIRE(snapshot.components<simulation::RespawnTimer>().size() == 1);
  CHECK(snapshot.components<simulation::RespawnTimer>()[0].value.ticks_remaining ==
        configuration.respawn_delay_ticks());
  CHECK(snapshot.match().phase() == simulation::MatchPhase::kRunning);
}
