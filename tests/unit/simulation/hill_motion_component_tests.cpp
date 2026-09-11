#include "components/hill_motion_component.hpp"
#include "fixtures/hill_motion_fixture.hpp"
#include "game_simulation.hpp"

#include <catch2/catch_test_macros.hpp>

namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::hill_motion_fixture;

TEST_CASE("hill motion participates in world equality and entity destruction",
          "[unit][simulation][hill_motion][component_registry]") {
  simulation::GameWorld world = fixture::world();
  const simulation::GameWorld initial = world;
  simulation::HillMotion changed = fixture::motion();
  REQUIRE(changed.schedule.has_value());
  changed.schedule->next_retarget_tick = changed.schedule->next_retarget_tick.next();
  world.mutable_store<simulation::HillMotion>().insert_or_assign(fixture::entity(), changed);
  CHECK(world != initial);
  world.destroy_entity(fixture::entity());
  CHECK(world.store<simulation::HillMotion>().empty());
  CHECK_FALSE(world.contains(fixture::entity()));
}

TEST_CASE("hill motion snapshot publishes current velocity but no future schedule",
          "[unit][simulation][hill_motion][snapshot]") {
  simulation::GameWorld world = fixture::world();
  const simulation::GameSimulation game =
      simulation::GameSimulation::create(fixture::configuration(), world);
  const simulation::WorldSnapshot snapshot = game.snapshot();
  const auto motions = snapshot.components<simulation::HillMotion>();
  REQUIRE(motions.size() == 1);
  CHECK(motions.front().value.velocity == fixture::motion().velocity);
  CHECK_FALSE(motions.front().value.schedule.has_value());
  CHECK(world.store<simulation::HillMotion>().find(fixture::entity())->schedule ==
        fixture::motion().schedule);

  simulation::HillMotion changed = fixture::motion();
  changed.schedule->next_retarget_tick = changed.schedule->next_retarget_tick.next();
  world.mutable_store<simulation::HillMotion>().insert_or_assign(fixture::entity(), changed);
  CHECK(simulation::GameSimulation::create(fixture::configuration(), world).snapshot() == snapshot);
}
