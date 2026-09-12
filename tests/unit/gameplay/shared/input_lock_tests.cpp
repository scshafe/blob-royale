#include "shared/input_lock.hpp"

#include "components/race_progress_component.hpp"
#include "fixtures/status_fixture.hpp"
#include "race/course_publisher_system.hpp"
#include "race/race_test_fixture.hpp"

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

TEST_CASE("active stun input lock remains independent of phase and body presence",
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

TEST_CASE("completed race progress locks later activation across phases and body replacement",
          "[unit][gameplay][input_lock][race]") {
  namespace testing = blob_royale::testing;
  const testing::TickHarness harness{fixture::tick(), testing::race_test_map()};
  auto world = testing::race_test_world({simulation::Vector2::create(100.0, 320.0)});
  const auto course =
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration());
  gameplay::CoursePublisherSystem::create(course, testing::race_test_configuration())
      ->apply(world, harness.context());
  for (const std::uint64_t progress : {0U, 1U, 2U}) {
    world.mutable_store<simulation::RaceProgress>().insert_or_assign(
        fixture::entity(), simulation::RaceProgress{progress});
    CHECK(gameplay::input_is_locked(world, fixture::entity(), fixture::tick()) == (progress == 2));
  }
  world.mutable_store<simulation::PhysicsBody>().erase(fixture::entity());
  for (const auto phase : simulation::kMatchPhases) {
    world.mutable_match().phase = phase;
    CHECK(gameplay::input_is_locked(world, fixture::entity(), fixture::tick()));
  }
  world.mutable_store<simulation::RaceProgress>().erase(fixture::entity());
  CHECK_FALSE(gameplay::input_is_locked(world, fixture::entity(), fixture::tick()));
}
