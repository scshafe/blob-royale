#include "royale/zone_shrink_system.hpp"

#include "gameplay_test_fixture.hpp"

#include "components/zone_component.hpp"
#include "game_simulation.hpp"
#include "gameplay_validation_error.hpp"
#include "input_batch.hpp"
#include "map_definition.hpp"
#include "match_phase.hpp"
#include "royale/royale_configuration.hpp"
#include "royale/royale_mode.hpp"
#include "simulation_limits.hpp"
#include "simulation_tolerance.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

// The accepted fixture arena, which is also the shipped map's geometry.
[[nodiscard]] simulation::ArenaBounds arena() {
  return simulation::ArenaBounds::create(960.0, 640.0);
}

} // namespace

TEST_CASE("the zone is centred on the arena and starts at its circumscribed radius",
          "[unit][gameplay][royale][zone]") {
  CHECK(gameplay::zone_center(arena()) == simulation::Vector2::create(480.0, 320.0));

  // `R_full = sqrt((width / 2)^2 + (height / 2)^2)`. Starting there is what guarantees that no
  // player is outside at the moment `running` begins: a committed centre is confined to
  // `[r, width - r] x [r, height - r]`, whose farthest point from the centre is strictly less.
  const double full_radius = gameplay::zone_full_radius(arena());
  CHECK(simulation::approximately_equal(full_radius, 576.888204074238, 1.0e-9));

  const double farthest_committed_center =
      std::sqrt(((480.0 - 10.0) * (480.0 - 10.0)) + ((320.0 - 10.0) * (320.0 - 10.0)));
  CHECK(farthest_committed_center < full_radius);
}

TEST_CASE("the zone radius is a pure function of one integer tick difference",
          "[unit][gameplay][royale][zone]") {
  const double full_radius = gameplay::zone_full_radius(arena());
  const double minimum_radius = 60.0;
  const std::uint64_t shrink_ticks = 36'000;

  // `radius(0)` is exactly `R_full`, which is why the tick that enters `running` -- on which the
  // system still observes `countdown` and writes `R_full` -- is consistent with the first snapshot
  // that reports `running`.
  CHECK(gameplay::zone_radius(full_radius, minimum_radius, 0, shrink_ticks) == full_radius);

  // At and past `T` the floor is returned **by assignment rather than by arithmetic**, so the held
  // radius is exactly `R_min` rather than a rounding of it.
  CHECK(gameplay::zone_radius(full_radius, minimum_radius, shrink_ticks, shrink_ticks) ==
        minimum_radius);
  CHECK(gameplay::zone_radius(full_radius, minimum_radius, shrink_ticks + 1, shrink_ticks) ==
        minimum_radius);

  // `T == 0` is defined instead of dividing by zero.
  CHECK(gameplay::zone_radius(full_radius, minimum_radius, 0, 0) == minimum_radius);
  CHECK(gameplay::zone_radius(full_radius, minimum_radius, 5, 0) == minimum_radius);

  CHECK(simulation::approximately_equal(
      gameplay::zone_radius(full_radius, minimum_radius, shrink_ticks / 2, shrink_ticks),
      (full_radius + minimum_radius) / 2.0, simulation::kPositionTolerance));

  // Non-increasing in `e`, which is what makes the circle a contraction rather than a curve.
  double previous = full_radius;
  for (std::uint64_t elapsed = 0; elapsed <= shrink_ticks; elapsed += 1'000) {
    const double radius = gameplay::zone_radius(full_radius, minimum_radius, elapsed, shrink_ticks);
    CHECK(radius <= previous);
    previous = radius;
  }

  // The contraction rate the ADR states for the proposed values: about 5.743 wu/s, roughly 35 times
  // slower than the 200 wu/s terminal speed, so a thrusting player can always outrun the boundary.
  const double contraction_per_second =
      full_radius - gameplay::zone_radius(full_radius, minimum_radius, 400, shrink_ticks);
  CHECK(simulation::approximately_equal(contraction_per_second, 5.7432, 1.0e-4));
}

TEST_CASE("zone_shrink creates the zone entity once and rewrites its component every tick",
          "[unit][gameplay][royale][zone]") {
  testing::SteppedGame driver{testing::gameplay_simulation(
      gameplay::RoyaleMode::create(), testing::gameplay_map(4, "royale_zone_map"))};
  const simulation::EntityId zone_entity = driver.next_entity_id();

  const simulation::WorldSnapshot first = driver.step();
  REQUIRE(first.components<simulation::Zone>().size() == 1);
  CHECK(first.components<simulation::Zone>()[0].entity == zone_entity);
  // The zone entity owns neither of the two components that make an entity alive, so it never
  // enters a contact pair, never integrates, and is never counted alive.
  CHECK(first.components<simulation::PhysicsBody>().empty());
  CHECK(first.players().empty());
  CHECK(first.entities().size() == 1);

  const simulation::WorldSnapshot later = driver.advance(8);
  REQUIRE(later.components<simulation::Zone>().size() == 1);
  CHECK(later.components<simulation::Zone>()[0].entity == zone_entity);
  CHECK(later.match().phase() == simulation::MatchPhase::kLobby);
  // Still `R_full`, because the zone covers the whole arena in `lobby` and `countdown`.
  CHECK(later.components<simulation::Zone>()[0].value.radius ==
        gameplay::zone_full_radius(arena()));
}

TEST_CASE("a tick that may create no entity is a rejection naming the zone, not a match with no "
          "zone",
          "[unit][gameplay][royale][zone][validation]") {
  simulation::GameSimulation game = testing::gameplay_simulation(
      gameplay::RoyaleMode::create(), testing::gameplay_map(4, "royale_unreserved_map"));

  // `InputBatch::empty()` is the no-input tick: no commands and no reservation, so a tick handed
  // nothing may create nothing. Royale needs one id on its first tick, and playing on without a
  // zone would mean never evaluating elimination -- a match silently played by different rules.
  try {
    game.step(testing::kGameplayFixedDelta, simulation::InputBatch::empty());
    FAIL("a tick with no reservation created the zone anyway");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() == gameplay::GameplayValidationCode::kRoyaleZoneEntityUnreserved);
    CHECK(error.code() == std::string_view{"GAMEPLAY.ROYALE_ZONE_ENTITY_UNRESERVED"});
    CHECK(error.context() == "zone_shrink.zone_entity");
  }

  // The tick failed against a working copy, so the committed world is exactly what the previous
  // commit left: no entity, no zone, and the sequence unchanged.
  CHECK(game.tick_sequence().value() == 0);
  const simulation::WorldSnapshot committed = game.snapshot();
  CHECK(committed.entities().empty());
}
