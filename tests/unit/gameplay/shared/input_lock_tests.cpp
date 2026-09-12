#include "shared/input_lock.hpp"

#include "fixtures/status_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

namespace simulation = blob_royale::simulation;
namespace gameplay = blob_royale::gameplay;
namespace fixture = blob_royale::testing::status_fixture;

TEST_CASE("input lock is exactly the active stun window including absolute endpoints",
          "[unit][gameplay][input_lock][stun]") {
  const auto world = fixture::world_with_status();
  // Entity/tick factories can validate; the predicate itself is nonthrowing.
  const auto entity = fixture::entity();
  const auto tick = fixture::tick();
  STATIC_REQUIRE(noexcept(gameplay::input_is_locked(world, entity, tick)));
  CHECK_FALSE(gameplay::input_is_locked(world, entity,
                                        fixture::tick(fixture::kActivation - fixture::kOneTick)));
  CHECK(gameplay::input_is_locked(world, entity, tick));
  CHECK(gameplay::input_is_locked(world, entity,
                                  fixture::tick(fixture::kExpiry - fixture::kOneTick)));
  CHECK_FALSE(gameplay::input_is_locked(world, entity, fixture::tick(fixture::kExpiry)));
  CHECK_FALSE(gameplay::input_is_locked(world, fixture::entity(fixture::kMissingEntity), tick));
}

TEST_CASE("input lock does not add phase body presence or race finish admission rules",
          "[unit][gameplay][input_lock][stun]") {
  auto world = fixture::world_with_status();
  world.mutable_store<simulation::PhysicsBody>().erase(fixture::entity());
  for (const auto phase : simulation::kMatchPhases) {
    world.mutable_match().phase = phase;
    CHECK(gameplay::input_is_locked(world, fixture::entity(), fixture::tick()));
  }
  world.mutable_store<simulation::Stun>().erase(fixture::entity());
  CHECK_FALSE(gameplay::input_is_locked(world, fixture::entity(), fixture::tick()));
}
