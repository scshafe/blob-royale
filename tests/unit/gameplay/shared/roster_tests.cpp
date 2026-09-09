#include "shared/roster.hpp"

#include "components/controllable_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::PhysicsBody body_at(const double x) {
  return simulation::PhysicsBody::create(simulation::Vector2::create(x, 50.0),
                                         simulation::Vector2::create(0.0, 0.0),
                                         simulation::Vector2::create(0.0, 0.0));
}

// One of each population: entity 1 is alive, entity 2 awaits a seat (or is out of play with its
// body erased), entity 3 is a wall, and entity 4 is alive.
[[nodiscard]] simulation::GameWorld mixed_world() {
  simulation::GameWorld world = simulation::GameWorld::create(
      {simulation::GameWorld::EntitySeed::create(simulation::EntityId::create(1), body_at(50.0),
                                                 simulation::ControllerId::create(1)),
       simulation::GameWorld::EntitySeed::create(simulation::EntityId::create(4), body_at(200.0),
                                                 simulation::ControllerId::create(4))});
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      simulation::EntityId::create(2),
      simulation::Controllable{simulation::ControllerId::create(2)});
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      simulation::EntityId::create(3),
      simulation::PhysicsBody::create_static(simulation::Vector2::create(300.0, 300.0)));
  return world;
}

} // namespace

TEST_CASE("alive is a body and a controller; a participant is a controller",
          "[unit][gameplay][shared][roster]") {
  const simulation::GameWorld world = mixed_world();

  CHECK(gameplay::alive_count(world) == 2);
  CHECK(gameplay::alive_entities(world) ==
        std::vector<simulation::EntityId>{simulation::EntityId::create(1),
                                          simulation::EntityId::create(4)});
  // The pending or respawning entity is playing; the wall is not.
  CHECK(gameplay::participant_count(world) == 3);
  CHECK(gameplay::participant_entities(world) ==
        std::vector<simulation::EntityId>{simulation::EntityId::create(1),
                                          simulation::EntityId::create(2),
                                          simulation::EntityId::create(4)});
}

TEST_CASE("an empty world has nobody alive and nobody playing",
          "[unit][gameplay][shared][roster]") {
  const simulation::GameWorld world = simulation::GameWorld::create({});
  CHECK(gameplay::alive_count(world) == 0);
  CHECK(gameplay::alive_entities(world).empty());
  CHECK(gameplay::participant_count(world) == 0);
  CHECK(gameplay::participant_entities(world).empty());
}

TEST_CASE("erasing a body turns an alive entity into a participant that is not alive",
          "[unit][gameplay][shared][roster]") {
  // The shape a respawn leaves behind: the controller link stays, the body is gone.
  simulation::GameWorld world = mixed_world();
  world.mutable_store<simulation::PhysicsBody>().erase(simulation::EntityId::create(4));

  CHECK(gameplay::alive_count(world) == 1);
  CHECK(gameplay::participant_count(world) == 3);
}
