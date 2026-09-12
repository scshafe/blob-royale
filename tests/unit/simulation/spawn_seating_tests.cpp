#include "spawn_seating.hpp"

#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "simulation_tolerance.hpp"
#include "terrain_definition.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

constexpr double kPlayerRadius = 10.0;

[[nodiscard]] simulation::TerrainDefinition solid_terrain() {
  return simulation::TerrainDefinition::solid(simulation::ArenaBounds::create(200, 200));
}

[[nodiscard]] simulation::GameWorld world_with_body_at(const double x, const double y) {
  return simulation::GameWorld::create({simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(1),
      simulation::PhysicsBody::create(simulation::Vector2::create(x, y),
                                      simulation::Vector2::create(0.0, 0.0),
                                      simulation::Vector2::create(0.0, 0.0)),
      simulation::ControllerId::create(1))});
}

} // namespace

TEST_CASE("supported seats are unavailable within contact range and available beyond it",
          "[unit][simulation][spawn_seating]") {
  // The predicate is the baseline pair contact rule applied to a candidate seat: `2r`, inclusive
  // within the accepted position tolerance, so two bodies are never seated in contact.
  const simulation::GameWorld world = world_with_body_at(50.0, 50.0);
  const std::span<const simulation::ComponentStore<simulation::PhysicsBody>::Entry> bodies =
      world.store<simulation::PhysicsBody>().entries();

  const auto terrain = solid_terrain();
  CHECK_FALSE(simulation::seat_is_supported_and_unoccupied(simulation::Vector2::create(50, 50),
                                                           bodies, kPlayerRadius, terrain));
  CHECK_FALSE(simulation::seat_is_supported_and_unoccupied(simulation::Vector2::create(69, 50),
                                                           bodies, kPlayerRadius, terrain));
  // Exactly `2r` away is contact, as the pair predicate has it.
  CHECK_FALSE(simulation::seat_is_supported_and_unoccupied(simulation::Vector2::create(70, 50),
                                                           bodies, kPlayerRadius, terrain));
  CHECK(simulation::seat_is_supported_and_unoccupied(simulation::Vector2::create(71, 50), bodies,
                                                     kPlayerRadius, terrain));
  CHECK(simulation::seat_is_supported_and_unoccupied(simulation::Vector2::create(50, 71), bodies,
                                                     kPlayerRadius, terrain));
}

TEST_CASE("an empty arena admits supported discs but not an unsupported seat",
          "[unit][simulation][spawn_seating]") {
  const simulation::GameWorld empty = simulation::GameWorld::create({});
  const auto terrain = solid_terrain();
  CHECK(simulation::seat_is_supported_and_unoccupied(
      simulation::Vector2::create(10, 10), empty.store<simulation::PhysicsBody>().entries(),
      kPlayerRadius, terrain));
  CHECK_FALSE(simulation::seat_is_supported_and_unoccupied(
      simulation::Vector2::create(0, 0), empty.store<simulation::PhysicsBody>().entries(),
      kPlayerRadius, terrain));
}

TEST_CASE("seat clearance uses actual body radius and complete terrain support",
          "[unit][simulation][spawn_seating]") {
  auto world = world_with_body_at(50, 50);
  const auto entity = simulation::EntityId::create(1);
  const auto original = *world.store<simulation::PhysicsBody>().find(entity);
  const auto terrain = solid_terrain();
  for (const bool is_static : {false, true}) {
    auto obstacle =
        is_static ? simulation::PhysicsBody::create_static(original.position()) : original;
    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity,
                                                                    obstacle.with_radius(30));
    CHECK_FALSE(simulation::seat_is_supported_and_unoccupied(
        simulation::Vector2::create(90, 50), world.store<simulation::PhysicsBody>().entries(),
        kPlayerRadius, terrain));
    CHECK(simulation::seat_is_supported_and_unoccupied(
        simulation::Vector2::create(91, 50), world.store<simulation::PhysicsBody>().entries(),
        kPlayerRadius, terrain));
    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity,
                                                                    obstacle.with_radius(2));
    CHECK(simulation::seat_is_supported_and_unoccupied(
        simulation::Vector2::create(63, 50), world.store<simulation::PhysicsBody>().entries(),
        kPlayerRadius, terrain));
  }
  const auto hole = simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(200, 200), simulation::TerrainGround::kSolid, {},
      {simulation::TerrainHole::create("seat_pit", simulation::Vector2::create(150, 150), 10)});
  CHECK_FALSE(simulation::seat_is_supported_and_unoccupied(
      simulation::Vector2::create(135, 150), world.store<simulation::PhysicsBody>().entries(),
      kPlayerRadius, hole));
  CHECK(simulation::seat_is_supported_and_unoccupied(
      simulation::Vector2::create(129, 150), world.store<simulation::PhysicsBody>().entries(),
      kPlayerRadius, hole));
}

TEST_CASE("seating writes a body at rest with the configured radius and an ordinary blob's physics",
          "[unit][simulation][spawn_seating]") {
  simulation::GameWorld world = simulation::GameWorld::create({});
  const simulation::EntityId entity = simulation::EntityId::create(7);
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      entity, simulation::Controllable{simulation::ControllerId::create(7)});

  simulation::seat_body_at_rest(world, entity, simulation::Vector2::create(120.0, 80.0),
                                kPlayerRadius);

  const simulation::PhysicsBody* body = world.store<simulation::PhysicsBody>().find(entity);
  REQUIRE(body != nullptr);
  CHECK(body->position() == simulation::Vector2::create(120.0, 80.0));
  CHECK(body->velocity() == simulation::Vector2::create(0.0, 0.0));
  CHECK(body->acceleration() == simulation::Vector2::create(0.0, 0.0));
  CHECK(body->radius() == kPlayerRadius);
  CHECK(body->mass() == simulation::PhysicsBody::kDefaultMass);
  CHECK(body->restitution() == simulation::PhysicsBody::kDefaultRestitution);
  CHECK(body->collision_layer() == simulation::PhysicsBody::kDefaultCollisionLayer);
  CHECK(body->collision_mask() == simulation::PhysicsBody::kDefaultCollisionMask);
  CHECK_FALSE(body->is_static());
  CHECK(body->ground_attachment() == simulation::GroundAttachment::kGroundBound);
  // The seated entity is now alive by the roster's definition: a body and a controller.
  CHECK(world.store<simulation::Controllable>().find(entity) != nullptr);
}

TEST_CASE("seating an entity that already carries a body replaces it",
          "[unit][simulation][spawn_seating]") {
  simulation::GameWorld world = world_with_body_at(50.0, 50.0);

  simulation::seat_body_at_rest(world, simulation::EntityId::create(1),
                                simulation::Vector2::create(200.0, 200.0), kPlayerRadius);

  const simulation::PhysicsBody* body =
      world.store<simulation::PhysicsBody>().find(simulation::EntityId::create(1));
  REQUIRE(body != nullptr);
  CHECK(body->position() == simulation::Vector2::create(200.0, 200.0));
  CHECK(world.store<simulation::PhysicsBody>().entries().size() == 1);
}
