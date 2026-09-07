#include "shared/lifetime_expiry_system.hpp"

#include "gameplay_test_fixture.hpp"

#include "components/lifetime_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "events/despawn_event.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_event_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] simulation::EntityId entity(const simulation::EntityId::Value value) {
  return simulation::EntityId::create(value);
}

[[nodiscard]] simulation::PhysicsBody body_at(const double x) {
  return simulation::PhysicsBody::create(simulation::Vector2::create(x, 100.0),
                                         simulation::Vector2::create(0.0, 0.0),
                                         simulation::Vector2::create(0.0, 0.0));
}

// An entity carrying a body and, when `ticks` is given, a lifetime.
void seat(simulation::GameWorld& world, const simulation::EntityId id, const double x) {
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(id, body_at(x));
}

void seat_with_lifetime(simulation::GameWorld& world, const simulation::EntityId id, const double x,
                        const std::uint64_t ticks) {
  seat(world, id, x);
  world.mutable_store<simulation::Lifetime>().insert_or_assign(id, simulation::Lifetime{ticks});
}

[[nodiscard]] std::vector<simulation::EntityId> despawned(const simulation::GameWorld& world) {
  std::vector<simulation::EntityId> named;
  for (const simulation::WorldEvent& event : world.events()) {
    if (const auto* despawn = std::get_if<simulation::DespawnEvent>(&event); despawn != nullptr) {
      named.push_back(despawn->entity);
    }
  }
  return named;
}

void run(simulation::GameWorld& world) {
  const testing::TickHarness harness{simulation::TickSequence::create(5)};
  gameplay::LifetimeExpirySystem::create()->apply(world, harness.context());
}

} // namespace

TEST_CASE("lifetime_expiry decrements a counter that has ticks left",
          "[unit][gameplay][shared][lifetime_expiry]") {
  simulation::GameWorld world = simulation::GameWorld::create({});
  seat_with_lifetime(world, entity(1), 100.0, 3);

  run(world);

  CHECK(world.store<simulation::Lifetime>().find(entity(1))->ticks_remaining == 2);
  CHECK(despawned(world).empty());
}

TEST_CASE("lifetime_expiry despawns an entity whose last tick is this one",
          "[unit][gameplay][shared][lifetime_expiry]") {
  simulation::GameWorld world = simulation::GameWorld::create({});
  seat_with_lifetime(world, entity(1), 100.0, 1);

  run(world);

  // Named for removal, not removed here: phase 10's `apply_despawn_events` destroys everything a
  // DespawnEvent names, which is the one place that also decides whether the spatial index has to
  // be rebuilt.
  CHECK(despawned(world) == std::vector<simulation::EntityId>{entity(1)});
  CHECK(world.contains(entity(1)));
}

TEST_CASE("lifetime_expiry treats a zero counter as already spent rather than underflowing",
          "[unit][gameplay][shared][lifetime_expiry]") {
  simulation::GameWorld world = simulation::GameWorld::create({});
  seat_with_lifetime(world, entity(1), 100.0, 0);

  run(world);

  // The failure this pins is a real one for an unsigned countdown: decrementing zero would wrap to
  // 2^64-1 and the entity would outlive the match rather than being removed from it.
  CHECK(despawned(world) == std::vector<simulation::EntityId>{entity(1)});
  CHECK(world.store<simulation::Lifetime>().find(entity(1))->ticks_remaining == 0);
}

TEST_CASE("lifetime_expiry leaves an entity carrying no Lifetime untouched",
          "[unit][gameplay][shared][lifetime_expiry]") {
  simulation::GameWorld world =
      simulation::GameWorld::create({simulation::GameWorld::EntitySeed::create(
          entity(1), body_at(100.0), simulation::ControllerId::create(1))});
  const simulation::GameWorld before = world;

  run(world);

  // A player has no lifetime, and this is the assertion that adding the system changed nothing that
  // already ran: the world is the same value it was.
  CHECK(world == before);
  CHECK(despawned(world).empty());
}

TEST_CASE("lifetime_expiry names expiring entities in ascending EntityId order",
          "[unit][gameplay][shared][lifetime_expiry]") {
  simulation::GameWorld world = simulation::GameWorld::create({});
  // Seated out of order on purpose; the store canonicalizes to ascending id and the walk inherits
  // that, so the tick's DespawnEvents are ordered without the system sorting anything.
  seat_with_lifetime(world, entity(7), 300.0, 1);
  seat_with_lifetime(world, entity(2), 200.0, 1);
  seat_with_lifetime(world, entity(4), 250.0, 5);
  seat_with_lifetime(world, entity(1), 100.0, 1);

  run(world);

  CHECK(despawned(world) == std::vector<simulation::EntityId>{entity(1), entity(2), entity(7)});
  CHECK(world.store<simulation::Lifetime>().find(entity(4))->ticks_remaining == 4);
}
