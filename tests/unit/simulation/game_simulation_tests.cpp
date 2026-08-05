#include "entity_id.hpp"
#include "fixed_delta.hpp"
#include "game_simulation.hpp"
#include "game_world.hpp"
#include "physics.hpp"
#include "physics_body.hpp"
#include "player.hpp"
#include "player_snapshot.hpp"
#include "simulation_config.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::SimulationConfig
configuration(const double width = 500.0, const double height = 500.0, const double radius = 10.0,
              const std::uint64_t columns = 10, const std::uint64_t rows = 10) {
  return simulation::SimulationConfig::create(
      width, height, radius, simulation::SimulationConfig::kRequiredTicksPerSecond, columns, rows);
}

[[nodiscard]] simulation::Player player(const simulation::EntityId::Value id, const double x,
                                        const double y, const double velocity_x = 0.0,
                                        const double velocity_y = 0.0,
                                        const double acceleration_x = 0.0,
                                        const double acceleration_y = 0.0) {
  return simulation::Player::create(
      simulation::EntityId::create(id),
      simulation::PhysicsBody::create(simulation::Vector2::create(x, y),
                                      simulation::Vector2::create(velocity_x, velocity_y),
                                      simulation::Vector2::create(acceleration_x, acceleration_y)));
}

[[nodiscard]] simulation::GameSimulation
game(std::vector<simulation::Player> players,
     simulation::SimulationConfig simulation_configuration = configuration()) {
  return simulation::GameSimulation::create(std::move(simulation_configuration),
                                            simulation::GameWorld::create(std::move(players)));
}

void advance(simulation::GameSimulation& simulation_game, const std::size_t tick_count) {
  for (std::size_t tick = 0; tick < tick_count; ++tick) {
    simulation_game.step(simulation::FixedDelta::canonical());
  }
}

[[nodiscard]] const simulation::PlayerSnapshot&
snapshot_player(const simulation::WorldSnapshot& snapshot,
                const simulation::EntityId::Value entity_id) {
  const auto match =
      std::lower_bound(snapshot.players().begin(), snapshot.players().end(), entity_id,
                       [](const simulation::PlayerSnapshot& player_snapshot,
                          const simulation::EntityId::Value searched_id) {
                         return player_snapshot.entity_id().value() < searched_id;
                       });
  REQUIRE(match != snapshot.players().end());
  REQUIRE(match->entity_id().value() == entity_id);
  return *match;
}

void check_vector(const simulation::Vector2& actual, const double expected_x,
                  const double expected_y,
                  const double absolute_tolerance = simulation::kScalarTolerance) {
  CHECK(
      actual.x() ==
      Catch::Approx(expected_x).margin(absolute_tolerance).epsilon(simulation::kRelativeTolerance));
  CHECK(
      actual.y() ==
      Catch::Approx(expected_y).margin(absolute_tolerance).epsilon(simulation::kRelativeTolerance));
}

[[nodiscard]] std::vector<simulation::Player> equal_distance_players(const bool shuffled) {
  std::vector<simulation::Player> players{
      player(1, 50.0, 50.0, 0.0, 0.0), player(2, 70.0, 50.0, -1.0, 0.0),
      player(3, 30.0, 50.0, 2.0, 0.0), player(9, 300.0, 300.0, 1.25, -0.75)};
  if (shuffled) {
    std::reverse(players.begin(), players.end());
  }
  return players;
}

} // namespace

TEST_CASE("GameSimulation has one movable owner and exposes a coherent committed tick zero",
          "[unit][simulation][game_simulation]") {
  STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<simulation::GameSimulation>);
  STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<simulation::GameSimulation>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<simulation::GameSimulation>);
  STATIC_REQUIRE(std::is_nothrow_move_assignable_v<simulation::GameSimulation>);

  simulation::GameSimulation simulation_game = game({player(9, 90.0, 90.0), player(2, 20.0, 20.0)});
  const simulation::WorldSnapshot initial = simulation_game.snapshot();

  CHECK(simulation_game.tick_sequence() == simulation::TickSequence::zero());
  CHECK(initial.tick_sequence() == simulation::TickSequence::zero());
  REQUIRE(initial.players().size() == 2);
  CHECK(initial.players()[0].entity_id().value() == 2);
  CHECK(initial.players()[1].entity_id().value() == 9);
  CHECK(simulation_game.configuration() == configuration());
}

TEST_CASE("GameSimulation rejects an initial disc outside the configured world",
          "[unit][simulation][game_simulation][validation]") {
  CHECK_THROWS_AS(game({player(1, 9.999, 50.0)}, configuration(100.0, 100.0, 10.0, 4, 4)),
                  simulation::SimulationValidationError);
  CHECK_NOTHROW(game({player(1, 10.0, 90.0)}, configuration(100.0, 100.0, 10.0, 4, 4)));
}

TEST_CASE("one tick applies stored acceleration before pair response and integration",
          "[unit][simulation][game_simulation][phases]") {
  simulation::GameSimulation simulation_game =
      game({player(1, 30.0, 50.0, 0.0, 0.0, 400.0, 0.0), player(2, 50.0, 50.0)},
           configuration(100.0, 100.0, 10.0, 4, 4));

  simulation_game.step(simulation::FixedDelta::canonical());
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  const simulation::PlayerSnapshot& first = snapshot_player(snapshot, 1);
  const simulation::PlayerSnapshot& second = snapshot_player(snapshot, 2);

  CHECK(snapshot.tick_sequence().value() == 1);
  check_vector(first.velocity(), 0.0, 0.0);
  check_vector(second.velocity(), 1.0, 0.0);
  check_vector(first.position(), 30.0, 50.0);
  check_vector(second.position(), 50.0025, 50.0);
  check_vector(first.acceleration(), 400.0, 0.0);
}

TEST_CASE("equal-distance contacts resolve once in lexicographic EntityId order",
          "[unit][simulation][game_simulation][collision][order]") {
  simulation::GameSimulation simulation_game = game(equal_distance_players(true));

  simulation_game.step(simulation::FixedDelta::canonical());
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  check_vector(snapshot_player(snapshot, 1).velocity(), 2.0, 0.0);
  check_vector(snapshot_player(snapshot, 2).velocity(), 0.0, 0.0);
  check_vector(snapshot_player(snapshot, 3).velocity(), -1.0, 0.0);
  check_vector(snapshot_player(snapshot, 1).position(), 50.005, 50.0);
  check_vector(snapshot_player(snapshot, 2).position(), 70.0, 50.0);
  check_vector(snapshot_player(snapshot, 3).position(), 29.9975, 50.0);
}

TEST_CASE("pair response precedes wall resolution in the same canonical tick",
          "[unit][simulation][game_simulation][collision][wall]") {
  simulation::GameSimulation simulation_game =
      game({player(1, 10.0, 50.0), player(2, 30.0, 50.0, -400.0, 0.0)},
           configuration(100.0, 100.0, 10.0, 4, 4));

  simulation_game.step(simulation::FixedDelta::canonical());
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  check_vector(snapshot_player(snapshot, 1).position(), 11.0, 50.0);
  check_vector(snapshot_player(snapshot, 1).velocity(), 400.0, 0.0);
  check_vector(snapshot_player(snapshot, 2).position(), 30.0, 50.0);
  check_vector(snapshot_player(snapshot, 2).velocity(), 0.0, 0.0);
}

TEST_CASE("corner and multi-wall overshoot commit bounded inward-facing state",
          "[unit][simulation][game_simulation][wall]") {
  simulation::GameSimulation corner_game =
      game({player(1, 89.0, 69.0, 400.0, 400.0)}, configuration(100.0, 80.0, 10.0, 4, 4));
  simulation::GameSimulation overshoot_game =
      game({player(1, 50.0, 40.0, 148'000.0, 0.0)}, configuration(100.0, 80.0, 10.0, 4, 4));

  corner_game.step(simulation::FixedDelta::canonical());
  overshoot_game.step(simulation::FixedDelta::canonical());
  const simulation::WorldSnapshot corner_snapshot = corner_game.snapshot();
  const simulation::WorldSnapshot overshoot_snapshot = overshoot_game.snapshot();
  const simulation::PlayerSnapshot& corner = snapshot_player(corner_snapshot, 1);
  const simulation::PlayerSnapshot& overshoot = snapshot_player(overshoot_snapshot, 1);

  check_vector(corner.position(), 90.0, 70.0);
  check_vector(corner.velocity(), -400.0, -400.0);
  check_vector(overshoot.position(), 80.0, 40.0);
  check_vector(overshoot.velocity(), -148'000.0, 0.0);
}

TEST_CASE("player crossing during integration receives no swept collision impulse",
          "[unit][simulation][game_simulation][collision][discrete]") {
  simulation::GameSimulation simulation_game =
      game({player(1, 30.0, 50.0, 10'000.0, 0.0), player(2, 70.0, 50.0, -10'000.0, 0.0)},
           configuration(100.0, 100.0, 5.0, 4, 4));

  simulation_game.step(simulation::FixedDelta::canonical());
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  check_vector(snapshot_player(snapshot, 1).position(), 55.0, 50.0);
  check_vector(snapshot_player(snapshot, 1).velocity(), 10'000.0, 0.0);
  check_vector(snapshot_player(snapshot, 2).position(), 45.0, 50.0);
  check_vector(snapshot_player(snapshot, 2).velocity(), -10'000.0, 0.0);
}

TEST_CASE("legacy pair seed reaches contact on tick 1600 and resolves on tick 1601",
          "[unit][simulation][game_simulation][fixture][horizon]") {
  simulation::GameSimulation simulation_game =
      game({player(1, 250.0, 80.0, 0.0, 2.5), player(2, 250.0, 120.0, 0.0, -2.5),
            player(3, 300.0, 400.0, 2.0, 0.0), player(4, 340.0, 410.0, -2.0, 0.0)});

  advance(simulation_game, 1'600);
  const simulation::WorldSnapshot contact_snapshot = simulation_game.snapshot();
  CHECK(contact_snapshot.tick_sequence().value() == 1'600);
  check_vector(snapshot_player(contact_snapshot, 1).position(), 250.0, 90.0,
               simulation::kPositionTolerance);
  check_vector(snapshot_player(contact_snapshot, 2).position(), 250.0, 110.0,
               simulation::kPositionTolerance);
  check_vector(snapshot_player(contact_snapshot, 1).velocity(), 0.0, 2.5);
  check_vector(snapshot_player(contact_snapshot, 2).velocity(), 0.0, -2.5);

  simulation_game.step(simulation::FixedDelta::canonical());
  const simulation::WorldSnapshot resolved_snapshot = simulation_game.snapshot();
  CHECK(resolved_snapshot.tick_sequence().value() == 1'601);
  check_vector(snapshot_player(resolved_snapshot, 1).velocity(), 0.0, -2.5);
  check_vector(snapshot_player(resolved_snapshot, 2).velocity(), 0.0, 2.5);
  CHECK(snapshot_player(resolved_snapshot, 1).position().y() < 90.0);
  CHECK(snapshot_player(resolved_snapshot, 2).position().y() > 110.0);
  check_vector(snapshot_player(resolved_snapshot, 3).position(), 308.005, 400.0,
               simulation::kPositionTolerance);
  check_vector(snapshot_player(resolved_snapshot, 4).position(), 331.995, 410.0,
               simulation::kPositionTolerance);
}

TEST_CASE("legacy wall seed reaches the lower wall inward on tick 2000",
          "[unit][simulation][game_simulation][fixture][horizon]") {
  simulation::GameSimulation simulation_game =
      game({player(1, 15.0, 70.0, -1.0, 3.2), player(2, 500.0, 400.0, 2.5, 1.8)},
           configuration(1'000.0, 800.0, 10.0, 20, 16));

  advance(simulation_game, 2'000);
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  const simulation::PlayerSnapshot& wall_player = snapshot_player(snapshot, 1);

  CHECK(snapshot.tick_sequence().value() == 2'000);
  check_vector(wall_player.position(), 10.0, 86.0, simulation::kPositionTolerance);
  check_vector(wall_player.velocity(), 1.0, 3.2);
  check_vector(snapshot_player(snapshot, 2).position(), 512.5, 409.0,
               simulation::kPositionTolerance);
}

TEST_CASE("grid-edge contact resolves once exactly like the exhaustive pair reference",
          "[unit][simulation][game_simulation][spatial_grid][reference]") {
  constexpr double radius = 5.0;
  const simulation::Player first = player(1, 45.0, 50.0, 1.0, 0.0);
  const simulation::Player second = player(2, 55.0, 50.0, -1.0, 0.0);
  const simulation::PlayerPairCollisionResult expected =
      simulation::resolve_player_pair_collision(first.body(), second.body(), radius);
  simulation::GameSimulation simulation_game =
      game({second, first}, configuration(100.0, 100.0, radius, 2, 2));

  simulation_game.step(simulation::FixedDelta::canonical());
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  check_vector(snapshot_player(snapshot, 1).velocity(), expected.first_velocity().x(),
               expected.first_velocity().y(), simulation::kVelocityTolerance);
  check_vector(snapshot_player(snapshot, 2).velocity(), expected.second_velocity().x(),
               expected.second_velocity().y(), simulation::kVelocityTolerance);
  check_vector(snapshot_player(snapshot, 1).position(), 44.9975, 50.0,
               simulation::kPositionTolerance);
  check_vector(snapshot_player(snapshot, 2).position(), 55.0025, 50.0,
               simulation::kPositionTolerance);
}

TEST_CASE("grid-corner contact resolves once exactly like the exhaustive pair reference",
          "[unit][simulation][game_simulation][spatial_grid][reference]") {
  constexpr double radius = 5.0;
  const double axis_offset = radius / std::sqrt(2.0);
  const simulation::Player first = player(1, 50.0 - axis_offset, 50.0 - axis_offset, 1.0, 1.0);
  const simulation::Player second = player(2, 50.0 + axis_offset, 50.0 + axis_offset, -1.0, -1.0);
  const simulation::PlayerPairCollisionResult expected =
      simulation::resolve_player_pair_collision(first.body(), second.body(), radius);
  simulation::GameSimulation simulation_game =
      game({second, first}, configuration(100.0, 100.0, radius, 2, 2));

  simulation_game.step(simulation::FixedDelta::canonical());
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  check_vector(snapshot_player(snapshot, 1).velocity(), expected.first_velocity().x(),
               expected.first_velocity().y(), simulation::kVelocityTolerance);
  check_vector(snapshot_player(snapshot, 2).velocity(), expected.second_velocity().x(),
               expected.second_velocity().y(), simulation::kVelocityTolerance);
}

TEST_CASE("legacy partition seed follows the no-grid reference across cell boundaries",
          "[unit][simulation][game_simulation][fixture][spatial_grid]") {
  simulation::GameSimulation simulation_game =
      game({player(1, 15.0, 70.0, 3.0, -4.0)}, configuration(100.0, 100.0, 10.0, 10, 10));

  constexpr std::size_t tick_count = 1'500;
  advance(simulation_game, tick_count);
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  const simulation::PlayerSnapshot& traced_player = snapshot_player(snapshot, 1);
  const double elapsed_seconds =
      static_cast<double>(tick_count) * simulation::FixedDelta::canonical().seconds();

  check_vector(traced_player.position(), 15.0 + (3.0 * elapsed_seconds),
               70.0 - (4.0 * elapsed_seconds), simulation::kPositionTolerance);
  check_vector(traced_player.velocity(), 3.0, -4.0);
}

TEST_CASE("snapshots are copy-owned and remain stable across later committed ticks",
          "[unit][simulation][game_simulation][snapshot]") {
  simulation::GameSimulation simulation_game = game({player(1, 50.0, 50.0, 4.0, -2.0)});
  const simulation::WorldSnapshot initial = simulation_game.snapshot();

  advance(simulation_game, 3);
  const simulation::WorldSnapshot third_tick = simulation_game.snapshot();
  advance(simulation_game, 2);
  const simulation::WorldSnapshot fifth_tick = simulation_game.snapshot();

  CHECK(initial.tick_sequence().value() == 0);
  CHECK(third_tick.tick_sequence().value() == 3);
  CHECK(fifth_tick.tick_sequence().value() == 5);
  check_vector(snapshot_player(initial, 1).position(), 50.0, 50.0);
  check_vector(snapshot_player(third_tick, 1).position(), 50.03, 49.985);
  check_vector(snapshot_player(fifth_tick, 1).position(), 50.05, 49.975);
}

TEST_CASE("a failed tick leaves the complete previously committed state unchanged",
          "[unit][simulation][game_simulation][exception_safety]") {
  simulation::GameSimulation simulation_game =
      game({player(1, 50.0, 50.0, simulation::kMaximumPhysicalComponentMagnitude, 0.0,
                   simulation::kMaximumPhysicalComponentMagnitude, 0.0)},
           configuration(100.0, 100.0, 10.0, 4, 4));
  const simulation::WorldSnapshot before = simulation_game.snapshot();

  CHECK_THROWS_AS(simulation_game.step(simulation::FixedDelta::canonical()),
                  simulation::SimulationValidationError);

  CHECK(simulation_game.snapshot() == before);
  CHECK(simulation_game.tick_sequence() == simulation::TickSequence::zero());
}

TEST_CASE("empty simulations advance as coherent committed ticks",
          "[unit][simulation][game_simulation][empty]") {
  simulation::GameSimulation simulation_game = game({});

  advance(simulation_game, 3);
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  CHECK(snapshot.tick_sequence().value() == 3);
  CHECK(snapshot.players().empty());
}

TEST_CASE("one executable produces bit-identical ordered snapshots across 100 fresh runs",
          "[unit][simulation][game_simulation][determinism]") {
  simulation::GameSimulation baseline_game = game(equal_distance_players(false));
  advance(baseline_game, 25);
  const simulation::WorldSnapshot baseline = baseline_game.snapshot();

  for (std::size_t run = 1; run < 100; ++run) {
    simulation::GameSimulation repeated_game = game(equal_distance_players(run % 2 == 0));
    advance(repeated_game, 25);
    INFO("fresh deterministic run " << run);
    CHECK(repeated_game.snapshot() == baseline);
  }
}
