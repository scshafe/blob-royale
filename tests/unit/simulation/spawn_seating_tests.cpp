#include "spawn_seating.hpp"

#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

constexpr double kPlayerRadius = 10.0;

[[nodiscard]] simulation::GameWorld world_with_body_at(const double x, const double y) {
  return simulation::GameWorld::create({simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(1),
      simulation::PhysicsBody::create(simulation::Vector2::create(x, y),
                                      simulation::Vector2::create(0.0, 0.0),
                                      simulation::Vector2::create(0.0, 0.0)),
      simulation::ControllerId::create(1))});
}

} // namespace

TEST_CASE("a point is occupied within contact range of a live body and free beyond it",
          "[unit][simulation][spawn_seating]") {
  // The predicate is the baseline pair contact rule applied to a candidate seat: `2r`, inclusive
  // within the accepted position tolerance, so two bodies are never seated in contact.
  const simulation::GameWorld world = world_with_body_at(50.0, 50.0);
  const std::span<const simulation::ComponentStore<simulation::PhysicsBody>::Entry> bodies =
      world.store<simulation::PhysicsBody>().entries();

  CHECK(simulation::point_is_occupied(simulation::Vector2::create(50.0, 50.0), bodies,
                                      kPlayerRadius));
  CHECK(simulation::point_is_occupied(simulation::Vector2::create(69.0, 50.0), bodies,
                                      kPlayerRadius));
  // Exactly `2r` away is contact, as the pair predicate has it.
  CHECK(simulation::point_is_occupied(simulation::Vector2::create(70.0, 50.0), bodies,
                                      kPlayerRadius));
  CHECK_FALSE(simulation::point_is_occupied(simulation::Vector2::create(71.0, 50.0), bodies,
                                            kPlayerRadius));
  CHECK_FALSE(simulation::point_is_occupied(simulation::Vector2::create(50.0, 71.0), bodies,
                                            kPlayerRadius));
}

TEST_CASE("an empty arena has no occupied point", "[unit][simulation][spawn_seating]") {
  const simulation::GameWorld empty = simulation::GameWorld::create({});
  CHECK_FALSE(simulation::point_is_occupied(simulation::Vector2::create(0.0, 0.0),
                                            empty.store<simulation::PhysicsBody>().entries(),
                                            kPlayerRadius));
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
