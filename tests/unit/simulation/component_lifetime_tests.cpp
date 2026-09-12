#include "component_lifetime.hpp"
#include "fixtures/component_lifetime_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::component_lifetime_fixture;

TEST_CASE("component lifetimes distinguish body-bound counters from persistent entity state",
          "[unit][simulation][component_lifetime]") {
  STATIC_REQUIRE(simulation::ComponentLifetime<simulation::HillPresence>::bound_to_body);
  STATIC_REQUIRE(simulation::ComponentLifetime<simulation::ZoneExposure>::bound_to_body);
  STATIC_REQUIRE(simulation::ComponentLifetime<simulation::Stun>::bound_to_body);
  STATIC_REQUIRE(simulation::ComponentLifetime<simulation::ContactEffectAdmission>::bound_to_body);
  // A shield guards a body, so a body that is gone leaves nothing for it to guard: the sweep that
  // follows body removal is what clears it on zero-delay return and round reset, and no ability
  // owner runs a cleanup pass of its own.
  STATIC_REQUIRE(simulation::ComponentLifetime<simulation::Shield>::bound_to_body);
  // A charge cooldown is a per-body wait, so a body that is gone has no next activation to wait
  // for. Both ability values answer the same way, which is what keeps the ability system out of the
  // cleanup business entirely: it writes activations and removes expired ones, and nothing else.
  STATIC_REQUIRE(simulation::ComponentLifetime<simulation::Charge>::bound_to_body);
  STATIC_REQUIRE_FALSE(simulation::ComponentLifetime<simulation::PhysicsBody>::bound_to_body);
  STATIC_REQUIRE_FALSE(simulation::ComponentLifetime<simulation::Controllable>::bound_to_body);
  STATIC_REQUIRE_FALSE(simulation::ComponentLifetime<simulation::Score>::bound_to_body);
  STATIC_REQUIRE_FALSE(simulation::ComponentLifetime<simulation::RaceProgress>::bound_to_body);
  STATIC_REQUIRE_FALSE(simulation::ComponentLifetime<simulation::RespawnTimer>::bound_to_body);
  STATIC_REQUIRE_FALSE(simulation::ComponentLifetime<simulation::Hill>::bound_to_body);
  STATIC_REQUIRE_FALSE(simulation::ComponentLifetime<simulation::HillMotion>::bound_to_body);
}

TEST_CASE("body-bound cleanup removes adjacent bodyless entries and preserves every live entry",
          "[unit][simulation][component_lifetime]") {
  auto world = fixture::mixed_world();
  const auto before = world;
  world.erase_body_bound_components_without_body();
  simulation::ComponentRegistry::for_each_kind([&world, &before]<typename Component>() {
    if constexpr (simulation::ComponentLifetime<Component>::bound_to_body) {
      for (const auto& entry : world.store<Component>().entries()) {
        CHECK(world.store<simulation::PhysicsBody>().find(entry.entity) != nullptr);
      }
      for (const auto& entry : before.store<Component>().entries()) {
        const auto* retained = world.store<Component>().find(entry.entity);
        if (before.store<simulation::PhysicsBody>().find(entry.entity) == nullptr) {
          CHECK(retained == nullptr);
        } else {
          REQUIRE(retained != nullptr);
          CHECK(*retained == entry.value);
        }
      }
    }
  });
  CHECK_FALSE(world.contains(fixture::entity(fixture::kBoundOnlyEntity)));
}

TEST_CASE("body-bound cleanup preserves persistent stores and unrelated world state exactly",
          "[unit][simulation][component_lifetime]") {
  auto world = fixture::mixed_world();
  const auto before = world;
  world.erase_body_bound_components_without_body();
  simulation::ComponentRegistry::for_each_kind([&world, &before]<typename Component>() {
    if constexpr (!simulation::ComponentLifetime<Component>::bound_to_body) {
      CHECK(world.store<Component>() == before.store<Component>());
    }
  });
  CHECK(world.match() == before.match());
  CHECK(world.random_draw_counts() == before.random_draw_counts());
  CHECK(world.entity_id_reservation() == before.entity_id_reservation());
}

TEST_CASE("body-bound cleanup is nonthrowing and idempotent including an empty world",
          "[unit][simulation][component_lifetime]") {
  auto world = fixture::mixed_world();
  STATIC_REQUIRE(noexcept(world.erase_body_bound_components_without_body()));
  world.erase_body_bound_components_without_body();
  const auto swept = world;
  world.erase_body_bound_components_without_body();
  CHECK(world == swept);

  auto empty = simulation::GameWorld::create({});
  const auto before = empty;
  empty.erase_body_bound_components_without_body();
  CHECK(empty == before);
}
