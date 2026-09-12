#include "candidate_pair.hpp"
#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "commands/despawn_command.hpp"
#include "commands/spawn_command.hpp"
#include "commands/thrust_command.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/lifetime_component.hpp"
#include "components/score_component.hpp"
#include "contact_rule_table.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "entity_id_reservation.hpp"
#include "fixed_delta.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "map_definition.hpp"
#include "match_lifecycle_system.hpp"
#include "match_phase.hpp"
#include "match_snapshot.hpp"
#include "physics.hpp"
#include "physics_body.hpp"
#include "player_snapshot.hpp"
#include "simulation_config.hpp"
#include "simulation_limits.hpp"
#include "simulation_system.hpp"
#include "simulation_test_fixture.hpp"
#include "simulation_validation_error.hpp"
#include "spatial_grid.hpp"
#include "system_pipeline.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_event_registry.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] simulation::SimulationConfig
configuration(const double width = 500.0, const double height = 500.0, const double radius = 10.0,
              const std::uint64_t columns = 10, const std::uint64_t rows = 10) {
  return simulation::SimulationConfig::create(
      width, height, radius, simulation::SimulationConfig::kRequiredTicksPerSecond, columns, rows);
}

[[nodiscard]] simulation::GameWorld::EntitySeed
player(const simulation::EntityId::Value id, const double x, const double y,
       const double velocity_x = 0.0, const double velocity_y = 0.0,
       const double acceleration_x = 0.0, const double acceleration_y = 0.0) {
  return simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(id),
      simulation::PhysicsBody::create(simulation::Vector2::create(x, y),
                                      simulation::Vector2::create(velocity_x, velocity_y),
                                      simulation::Vector2::create(acceleration_x, acceleration_y)));
}

[[nodiscard]] simulation::GameSimulation
game(std::vector<simulation::GameWorld::EntitySeed> players,
     simulation::SimulationConfig simulation_configuration = configuration()) {
  return simulation::GameSimulation::create(std::move(simulation_configuration),
                                            simulation::GameWorld::create(std::move(players)));
}

void advance(simulation::GameSimulation& simulation_game, const std::size_t tick_count) {
  for (std::size_t tick = 0; tick < tick_count; ++tick) {
    simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
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

[[nodiscard]] std::vector<simulation::GameWorld::EntitySeed>
equal_distance_players(const bool shuffled) {
  std::vector<simulation::GameWorld::EntitySeed> players{
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

TEST_CASE("GameSimulation permits initial wall overlap but rejects a center outside the envelope",
          "[unit][simulation][game_simulation][validation]") {
  CHECK_THROWS_AS(game({player(1, -0.001, 50.0)}, configuration(100.0, 100.0, 10.0, 4, 4)),
                  simulation::SimulationValidationError);
  CHECK_NOTHROW(game({player(1, 9.999, 50.0)}, configuration(100.0, 100.0, 10.0, 4, 4)));
  CHECK_NOTHROW(game({player(1, 10.0, 90.0)}, configuration(100.0, 100.0, 10.0, 4, 4)));
}

TEST_CASE("one tick applies stored acceleration before pair response and integration",
          "[unit][simulation][game_simulation][phases]") {
  simulation::GameSimulation simulation_game =
      game({player(1, 30.0, 50.0, 0.0, 0.0, 400.0, 0.0), player(2, 50.0, 50.0)},
           configuration(100.0, 100.0, 10.0, 4, 4));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
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

TEST_CASE("equal-time contacts use EntityId order and external changes reenable earlier pairs",
          "[unit][simulation][game_simulation][collision][order]") {
  simulation::GameSimulation simulation_game = game(equal_distance_players(true));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  // Step 16: the (1,3) response changes body 1 and re-enables its touching (1,2) pair.
  check_vector(snapshot_player(snapshot, 1).velocity(), 0.0, 0.0);
  check_vector(snapshot_player(snapshot, 2).velocity(), 2.0, 0.0);
  check_vector(snapshot_player(snapshot, 3).velocity(), -1.0, 0.0);
  check_vector(snapshot_player(snapshot, 1).position(), 50.0, 50.0);
  check_vector(snapshot_player(snapshot, 2).position(), 70.005, 50.0);
  check_vector(snapshot_player(snapshot, 3).position(), 29.9975, 50.0);
}

TEST_CASE("pair wins a wall tie and the wall response can reenable the touching pair",
          "[unit][simulation][game_simulation][collision][wall]") {
  simulation::GameSimulation simulation_game =
      game({player(1, 10.0, 50.0), player(2, 30.0, 50.0, -400.0, 0.0)},
           configuration(100.0, 100.0, 10.0, 4, 4));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  // Step 16: reflection transfers back through the re-enabled pair at this same instant.
  check_vector(snapshot_player(snapshot, 1).position(), 10.0, 50.0);
  check_vector(snapshot_player(snapshot, 1).velocity(), 0.0, 0.0);
  check_vector(snapshot_player(snapshot, 2).position(), 31.0, 50.0);
  check_vector(snapshot_player(snapshot, 2).velocity(), 400.0, 0.0);
}

TEST_CASE("corner and multi-wall overshoot commit bounded inward-facing state",
          "[unit][simulation][game_simulation][wall]") {
  simulation::GameSimulation corner_game =
      game({player(1, 89.0, 69.0, 400.0, 400.0)}, configuration(100.0, 80.0, 10.0, 4, 4));
  simulation::GameSimulation overshoot_game =
      game({player(1, 50.0, 40.0, 148'000.0, 0.0)}, configuration(100.0, 80.0, 10.0, 4, 4));

  corner_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  overshoot_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  const simulation::WorldSnapshot corner_snapshot = corner_game.snapshot();
  const simulation::WorldSnapshot overshoot_snapshot = overshoot_game.snapshot();
  const simulation::PlayerSnapshot& corner = snapshot_player(corner_snapshot, 1);
  const simulation::PlayerSnapshot& overshoot = snapshot_player(overshoot_snapshot, 1);

  check_vector(corner.position(), 90.0, 70.0);
  check_vector(corner.velocity(), -400.0, -400.0);
  check_vector(overshoot.position(), 80.0, 40.0);
  check_vector(overshoot.velocity(), -148'000.0, 0.0);
}

TEST_CASE(
    "player crossing receives a swept impulse at the certified contact before integration ends",
    "[unit][simulation][game_simulation][collision][continuous]") {
  simulation::GameSimulation simulation_game =
      game({player(1, 30.0, 50.0, 10'000.0, 0.0), player(2, 70.0, 50.0, -10'000.0, 0.0)},
           configuration(100.0, 100.0, 5.0, 4, 4));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  // Relative travel reaches the summed radius at t=.6; both discs reverse for the remaining .4.
  check_vector(snapshot_player(snapshot, 1).position(), 35.0, 50.0);
  check_vector(snapshot_player(snapshot, 1).velocity(), -10'000.0, 0.0);
  check_vector(snapshot_player(snapshot, 2).position(), 65.0, 50.0);
  check_vector(snapshot_player(snapshot, 2).velocity(), 10'000.0, 0.0);
}

TEST_CASE("migrated pair fixture reaches contact on tick 1600 and resolves on tick 1601",
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

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
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

TEST_CASE("migrated wall fixture preserves the positive gap at tick 2000 and reflects on tick 2001",
          "[unit][simulation][game_simulation][fixture][horizon]") {
  simulation::GameSimulation simulation_game =
      game({player(1, 15.0, 70.0, -1.0, 3.2), player(2, 500.0, 400.0, 2.5, 1.8)},
           configuration(1'000.0, 800.0, 10.0, 20, 16));

  advance(simulation_game, 2'000);
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  const simulation::PlayerSnapshot& wall_player = snapshot_player(snapshot, 1);

  CHECK(snapshot.tick_sequence().value() == 2'000);
  check_vector(wall_player.position(), 10.0, 86.0, simulation::kPositionTolerance);
  // Repeated integration leaves a positive substep gap. The continuous wall rule does not
  // snap this tolerance-near endpoint to the wall or reflect before the exact crossing.
  const double remaining_gap = wall_player.position().x() - 10.0;
  CAPTURE(remaining_gap);
  CHECK(remaining_gap > 0.0);
  CHECK(remaining_gap < simulation::FixedDelta::canonical().seconds());
  check_vector(wall_player.velocity(), -1.0, 3.2);
  check_vector(snapshot_player(snapshot, 2).position(), 512.5, 409.0,
               simulation::kPositionTolerance);

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  const auto reflected = simulation_game.snapshot();
  CHECK(reflected.tick_sequence().value() == 2'001);
  check_vector(snapshot_player(reflected, 1).velocity(), 1.0, 3.2);
  check_vector(snapshot_player(reflected, 1).position(),
               10.0 + simulation::FixedDelta::canonical().seconds() - remaining_gap, 86.008,
               simulation::kPositionTolerance);
}

TEST_CASE("grid-edge contact resolves once exactly like the exhaustive pair reference",
          "[unit][simulation][game_simulation][spatial_grid][reference]") {
  constexpr double radius = 5.0;
  const simulation::GameWorld::EntitySeed first = player(1, 45.0, 50.0, 1.0, 0.0);
  const simulation::GameWorld::EntitySeed second = player(2, 55.0, 50.0, -1.0, 0.0);
  const simulation::PlayerPairCollisionResult expected =
      simulation::resolve_player_pair_collision(first.body, second.body, radius);
  simulation::GameSimulation simulation_game =
      game({second, first}, configuration(100.0, 100.0, radius, 2, 2));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
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
  const simulation::GameWorld::EntitySeed first =
      player(1, 50.0 - axis_offset, 50.0 - axis_offset, 1.0, 1.0);
  const simulation::GameWorld::EntitySeed second =
      player(2, 50.0 + axis_offset, 50.0 + axis_offset, -1.0, -1.0);
  const simulation::PlayerPairCollisionResult expected =
      simulation::resolve_player_pair_collision(first.body, second.body, radius);
  simulation::GameSimulation simulation_game =
      game({second, first}, configuration(100.0, 100.0, radius, 2, 2));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  check_vector(snapshot_player(snapshot, 1).velocity(), expected.first_velocity().x(),
               expected.first_velocity().y(), simulation::kVelocityTolerance);
  check_vector(snapshot_player(snapshot, 2).velocity(), expected.second_velocity().x(),
               expected.second_velocity().y(), simulation::kVelocityTolerance);
}

TEST_CASE("migrated partition fixture follows the no-grid reference across cell boundaries",
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

  CHECK_THROWS_AS(
      simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()),
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

namespace {

using BodyEntry = simulation::ComponentStore<simulation::PhysicsBody>::Entry;

[[nodiscard]] simulation::SimulationConfig
dragged_configuration(const double drag_per_second, const double width = 500.0,
                      const double height = 500.0, const double radius = 10.0,
                      const std::uint64_t columns = 10, const std::uint64_t rows = 10) {
  return simulation::SimulationConfig::create(width, height, radius,
                                              simulation::SimulationConfig::kRequiredTicksPerSecond,
                                              columns, rows, drag_per_second);
}

[[nodiscard]] simulation::GameSimulation
staged_game(std::vector<simulation::GameWorld::EntitySeed> players,
            std::vector<simulation::SystemPipeline::StagedSystem> declared_systems,
            simulation::SimulationConfig simulation_configuration = configuration()) {
  return simulation::GameSimulation::create(
      std::move(simulation_configuration), simulation::GameWorld::create(std::move(players)),
      simulation::GameSimulationSetup::engine_defaults().with_systems(
          simulation::SystemPipeline::create(std::move(declared_systems))));
}

[[nodiscard]] simulation::MapDefinition arena_map(const double width, const double height) {
  return simulation::MapDefinition::bare_arena(simulation::ArenaBounds::create(width, height));
}

// The production shape: every declaration arrives through one setup value, and the mode is
// injected once. `GameSimulationSetup::of_mode` is the value form of the
// `create(configuration, map, mode)` the ADR names.
[[nodiscard]] simulation::GameSimulation
mode_game(std::vector<simulation::GameWorld::EntitySeed> seeds, simulation::MapDefinition map,
          testing::TestGameMode::Declaration declaration,
          simulation::SimulationConfig simulation_configuration = configuration()) {
  return simulation::GameSimulation::create(
      std::move(simulation_configuration), simulation::GameWorld::create(std::move(seeds)),
      simulation::GameSimulationSetup::of_mode(
          std::move(map), testing::TestGameMode::create(std::move(declaration))));
}

[[nodiscard]] simulation::InputBatch batch(std::vector<simulation::Command> commands) {
  return simulation::InputBatch::create(std::move(commands), simulation::CommandKindMask::all(),
                                        simulation::EntityIdReservation::none());
}

// A batch that carries a contiguous EntityIdReservation, which is what a tick needs before
// anything -- a spawn command or a system -- may create an entity.
[[nodiscard]] simulation::InputBatch reserved_batch(std::vector<simulation::Command> commands,
                                                    const simulation::EntityId::Value first,
                                                    const std::uint64_t count) {
  return simulation::InputBatch::create(
      std::move(commands), simulation::CommandKindMask::all(),
      simulation::EntityIdReservation::create(simulation::EntityId::create(first), count));
}

[[nodiscard]] simulation::Command thrust_command(const simulation::EntityId::Value entity,
                                                 const double x, const double y) {
  return simulation::Command{simulation::ThrustCommand{simulation::EntityId::create(entity),
                                                       simulation::Vector2::create(x, y)}};
}

[[nodiscard]] simulation::Command despawn_command(const simulation::EntityId::Value entity) {
  return simulation::Command{simulation::DespawnCommand{simulation::EntityId::create(entity)}};
}

[[nodiscard]] simulation::Command spawn_command(const simulation::ControllerId::Value controller) {
  return simulation::Command{
      simulation::SpawnCommand{simulation::ControllerId::create(controller)}};
}

[[nodiscard]] std::int64_t score_of(const simulation::WorldSnapshot& snapshot,
                                    const simulation::EntityId::Value entity_id) {
  const simulation::EntityId entity = simulation::EntityId::create(entity_id);
  for (const simulation::ComponentStore<simulation::Score>::Entry& entry :
       snapshot.components<simulation::Score>()) {
    if (entry.entity == entity) {
      return entry.value.points;
    }
  }
  FAIL("the committed snapshot carries no Score cell for the probed entity");
  return 0;
}

[[nodiscard]] std::uint64_t order_trail_of(const simulation::WorldSnapshot& snapshot,
                                           const simulation::EntityId::Value entity_id) {
  const simulation::EntityId entity = simulation::EntityId::create(entity_id);
  for (const simulation::ComponentStore<simulation::Lifetime>::Entry& entry :
       snapshot.components<simulation::Lifetime>()) {
    if (entry.entity == entity) {
      return entry.value.ticks_remaining;
    }
  }
  FAIL("the committed snapshot carries no Lifetime trail for the probed entity");
  return 0;
}

[[nodiscard]] const simulation::Controllable&
controllable_of(const simulation::WorldSnapshot& snapshot,
                const simulation::EntityId::Value entity_id) {
  const simulation::EntityId entity = simulation::EntityId::create(entity_id);
  const auto match =
      std::lower_bound(snapshot.components<simulation::Controllable>().begin(),
                       snapshot.components<simulation::Controllable>().end(), entity,
                       [](const simulation::ComponentStore<simulation::Controllable>::Entry& entry,
                          const simulation::EntityId searched) { return entry.entity < searched; });
  REQUIRE(match != snapshot.components<simulation::Controllable>().end());
  REQUIRE(match->entity == entity);
  return match->value;
}

// The four players and the accepted horizon of the migrated pair fixture.
[[nodiscard]] std::vector<simulation::GameWorld::EntitySeed> migrated_pair_fixture_players() {
  return {player(1, 250.0, 80.0, 0.0, 2.5), player(2, 250.0, 120.0, 0.0, -2.5),
          player(3, 300.0, 400.0, 2.0, 0.0), player(4, 340.0, 410.0, -2.0, 0.0)};
}

inline constexpr std::size_t kMigratedPairFixtureHorizon = 1'601;

// canonical: accepted_baseline_tick -- the retained pre-continuous seven-phase historical oracle.
//
// This is ADR 0003's accepted tick written out again, exactly as it stood before the kernel gained
// stages: stored acceleration, canonical pairs, wall resolution, position integration, reindex. It
// has no phase 0, no drag factor, and no hook stage, and it is deliberately a *second*
// implementation calling the same pure equations. Comparing the staged kernel against it with
// exact double equality -- not a tolerance -- is what proves that staging the tick changed no
// arithmetic.
class AcceptedBaselineTick final {
public:
  AcceptedBaselineTick(simulation::SimulationConfig baseline_configuration,
                       simulation::GameWorld baseline_world)
      : configuration_(baseline_configuration), world_(std::move(baseline_world)),
        grid_(simulation::SpatialGrid::create(configuration_, world_)) {}

  void step() {
    std::vector<BodyEntry> bodies;
    bodies.reserve(world_.store<simulation::PhysicsBody>().entries().size());
    for (const BodyEntry& entry : world_.store<simulation::PhysicsBody>().entries()) {
      bodies.push_back(BodyEntry{
          entry.entity, entry.value.with_velocity(simulation::integrate_accelerated_velocity(
                            entry.value.velocity(), entry.value.acceleration(),
                            simulation::FixedDelta::canonical()))});
    }

    for (const simulation::CandidatePair& pair : grid_.candidate_pairs()) {
      const std::size_t lower = index_of(bodies, pair.lower_id());
      const std::size_t higher = index_of(bodies, pair.higher_id());
      const simulation::PlayerPairCollisionResult collision =
          simulation::resolve_player_pair_collision(bodies[lower].value, bodies[higher].value,
                                                    configuration_.player_radius());
      bodies[lower].value = bodies[lower].value.with_velocity(collision.first_velocity());
      bodies[higher].value = bodies[higher].value.with_velocity(collision.second_velocity());
    }

    for (BodyEntry& entry : bodies) {
      const simulation::WallMotionResult motion = simulation::resolve_player_wall_motion(
          entry.value.position(), entry.value.velocity(), configuration_.world_width(),
          configuration_.world_height(), configuration_.player_radius(),
          simulation::FixedDelta::canonical());
      const simulation::Vector2 integrated =
          simulation::integrate_position(entry.value.position(), motion.displacement());
      entry.value = entry.value.with_velocity(motion.terminal_velocity()).with_position(integrated);
    }

    world_.mutable_store<simulation::PhysicsBody>() =
        simulation::ComponentStore<simulation::PhysicsBody>::create(std::move(bodies));
    grid_ = grid_.rebuilt(world_);
  }

  [[nodiscard]] std::span<const BodyEntry> bodies() const {
    return world_.store<simulation::PhysicsBody>().entries();
  }

private:
  [[nodiscard]] static std::size_t index_of(const std::vector<BodyEntry>& bodies,
                                            const simulation::EntityId entity) {
    const auto match =
        std::lower_bound(bodies.cbegin(), bodies.cend(), entity,
                         [](const BodyEntry& entry, const simulation::EntityId searched) {
                           return entry.entity < searched;
                         });
    REQUIRE(match != bodies.cend());
    return static_cast<std::size_t>(match - bodies.cbegin());
  }

  simulation::SimulationConfig configuration_;
  simulation::GameWorld world_;
  simulation::SpatialGrid grid_;
};

// Exact equality over every committed body, which is what "bit-identical" means here: Vector2's
// generated operator== compares doubles exactly and every physical component is canonicalized to
// positive zero before it is stored.
[[nodiscard]] bool bodies_are_bit_identical(const simulation::WorldSnapshot& snapshot,
                                            const std::span<const BodyEntry> expected) {
  const std::span<const BodyEntry> committed = snapshot.components<simulation::PhysicsBody>();
  return std::equal(committed.begin(), committed.end(), expected.begin(), expected.end());
}

} // namespace

TEST_CASE(
    "continuous motion first differs from the independent legacy oracle at the exact pair crossing",
    "[unit][simulation][game_simulation][fixture][horizon][determinism]") {
  simulation::GameSimulation kernel_game = game(migrated_pair_fixture_players());
  AcceptedBaselineTick baseline(configuration(),
                                simulation::GameWorld::create(migrated_pair_fixture_players()));
  REQUIRE(kernel_game.configuration().drag_per_second() == 0.0);

  std::size_t first_divergent_tick = 0;
  auto before_contact = kernel_game.snapshot();
  for (std::size_t tick = 1; tick <= kMigratedPairFixtureHorizon; ++tick) {
    if (tick == kMigratedPairFixtureHorizon) {
      before_contact = kernel_game.snapshot();
      CHECK(bodies_are_bit_identical(before_contact, baseline.bodies()));
    }
    kernel_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
    baseline.step();
    if (first_divergent_tick == 0 &&
        !bodies_are_bit_identical(kernel_game.snapshot(), baseline.bodies())) {
      first_divergent_tick = tick;
      const auto actual = kernel_game.snapshot();
      for (std::size_t index = 0; index < baseline.bodies().size(); ++index) {
        const auto& live = actual.components<simulation::PhysicsBody>()[index].value;
        const auto& legacy = baseline.bodies()[index].value;
        CAPTURE(tick, index, live.position().x(), live.position().y(), live.velocity().x(),
                live.velocity().y(), legacy.position().x(), legacy.position().y(),
                legacy.velocity().x(), legacy.velocity().y());
        CHECK(tick == kMigratedPairFixtureHorizon);
      }
    }
  }

  // Both independent ticks remain exactly equal through 1600. The remaining positive gap is
  // below this tick's relative travel: the legacy tolerance admits contact at its start,
  // while continuous motion first traverses the gap and only then reverses the velocities.
  CAPTURE(first_divergent_tick);
  CHECK(first_divergent_tick == kMigratedPairFixtureHorizon);
  const auto& before_first = snapshot_player(before_contact, 1);
  const auto& before_second = snapshot_player(before_contact, 2);
  const double remaining_gap = before_second.position().y() - before_first.position().y() - 20.0;
  CAPTURE(remaining_gap);
  CHECK(remaining_gap > 0.0);
  CHECK(remaining_gap < 5.0 * simulation::FixedDelta::canonical().seconds());
  check_vector(before_first.velocity(), 0.0, 2.5);
  check_vector(before_second.velocity(), 0.0, -2.5);
  const auto after_contact = kernel_game.snapshot();
  const auto& live_first = snapshot_player(after_contact, 1);
  const auto& live_second = snapshot_player(after_contact, 2);
  check_vector(live_first.velocity(), 0.0, -2.5);
  check_vector(live_second.velocity(), 0.0, 2.5);
  check_vector(baseline.bodies()[0].value.velocity(), 0.0, -2.5);
  check_vector(baseline.bodies()[1].value.velocity(), 0.0, 2.5);
  CAPTURE(live_first.position().y(), baseline.bodies()[0].value.position().y(),
          live_second.position().y(), baseline.bodies()[1].value.position().y());
  CHECK(live_first.position().y() > baseline.bodies()[0].value.position().y());
  CHECK(live_second.position().y() < baseline.bodies()[1].value.position().y());
  for (std::size_t index = 0; index < 2; ++index) {
    const auto& live = after_contact.components<simulation::PhysicsBody>()[index].value;
    const auto& legacy = baseline.bodies()[index].value;
    CHECK(live.position().x() == legacy.position().x());
    CHECK(live.with_position(legacy.position()) == legacy);
  }
  CHECK(after_contact.components<simulation::PhysicsBody>()[2] == baseline.bodies()[2]);
  CHECK(after_contact.components<simulation::PhysicsBody>()[3] == baseline.bodies()[3]);
  CHECK(kernel_game.tick_sequence().value() == kMigratedPairFixtureHorizon);
}

TEST_CASE("phase 0 despawns before any pair is built, so the partner receives no impulse",
          "[unit][simulation][game_simulation][phases][command]") {
  // A despawn removes its entity before phase 2 builds the pair list, so a partner that would
  // otherwise have been in contact integrates unchanged on that tick.
  simulation::GameSimulation simulation_game =
      game({player(1, 45.0, 50.0, 1.0, 0.0), player(2, 55.0, 50.0, -1.0, 0.0)},
           configuration(100.0, 100.0, 5.0, 4, 4));

  simulation_game.step(simulation::FixedDelta::canonical(), batch({despawn_command(1)}));
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  REQUIRE(snapshot.entities().size() == 1);
  CHECK(snapshot.entities()[0] == simulation::EntityId::create(2));
  check_vector(snapshot_player(snapshot, 2).velocity(), -1.0, 0.0);
  check_vector(snapshot_player(snapshot, 2).position(), 54.9975, 50.0);
}

TEST_CASE("phase 0 despawn erases the entity from the committed spatial index too",
          "[unit][simulation][game_simulation][phases][command][spatial_grid]") {
  // The next tick's pairs are built from the committed index. If the despawned id survived there,
  // the pair phase would name a body no store holds.
  simulation::GameSimulation simulation_game =
      game({player(1, 45.0, 50.0, 1.0, 0.0), player(2, 55.0, 50.0, -1.0, 0.0)},
           configuration(100.0, 100.0, 5.0, 4, 4));

  simulation_game.step(simulation::FixedDelta::canonical(), batch({despawn_command(1)}));
  CHECK_NOTHROW(
      simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()));

  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  CHECK(snapshot.tick_sequence().value() == 2);
  REQUIRE(snapshot.entities().size() == 1);
  check_vector(snapshot_player(snapshot, 2).velocity(), -1.0, 0.0);
}

TEST_CASE("phase 0 records this tick's commands into the entity's Controllable without "
          "interpreting them",
          "[unit][simulation][game_simulation][phases][command]") {
  // The recorded commands are tick-local and are stripped at publication, so the observation point
  // is a system inside the tick rather than the snapshot: the probe writes the count it saw into
  // each entity's Score cell.
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(
      testing::staged(simulation::SystemStage::kPreKernel,
                      std::make_unique<const testing::RecordedCommandProbeSystem>(
                          "recorded_command_value_probe", thrust_command(1, 0.5, -0.25))));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0), player(2, 200.0, 50.0)}, std::move(declared));

  simulation_game.step(simulation::FixedDelta::canonical(), batch({thrust_command(1, 0.5, -0.25)}));
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  // 1 means the entity's single recorded command compared equal to the thrust that was submitted.
  CHECK(score_of(snapshot, 1) == 1);
  CHECK(score_of(snapshot, 2) == 0);
  // The kernel records and does not interpret: no acceleration and no velocity moved.
  check_vector(snapshot_player(snapshot, 1).acceleration(), 0.0, 0.0);
  check_vector(snapshot_player(snapshot, 1).velocity(), 0.0, 0.0);
}

TEST_CASE("phase 0 records a Controllable's commands in the batch's one canonical order",
          "[unit][simulation][game_simulation][phases][command]") {
  // **One canonical order governs one command list** (engine review finding 6). The batch arrives
  // in phase 0's application order -- despawns, then spawns, then the remaining kinds, each group
  // ascending by the identity it addresses -- and the recorded list is that order rather than a
  // second convention layered on top of it.
  //
  // The probe holds entity 2's thrust, so a 1 means entity 2's recorded list is exactly that one
  // command: the despawn of entity 1 and the spawn of a new entity, both submitted in the same
  // batch and both ranked ahead of the thrust, are applied rather than recorded, and neither leaks
  // into a recorded list.
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(
      testing::staged(simulation::SystemStage::kPreKernel,
                      std::make_unique<const testing::RecordedCommandProbeSystem>(
                          "recorded_command_order_probe", thrust_command(2, -1.0, 0.25))));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0), player(2, 200.0, 50.0)}, std::move(declared));

  // Submitted in an order the canonicalization has to undo, so the assertion is about the batch's
  // order and not about the submission order.
  simulation_game.step(
      simulation::FixedDelta::canonical(),
      reserved_batch({thrust_command(2, -1.0, 0.25), spawn_command(41), despawn_command(1)}, 50,
                     4));
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  CHECK(score_of(snapshot, 2) == 1);
  // The spawned entity recorded nothing at all: a spawn addresses a controller, so phase 0 creates
  // an entity for it rather than recording against one.
  CHECK(score_of(snapshot, 50) == 0);
}

TEST_CASE("phase 0 clears the previous tick's recorded commands",
          "[unit][simulation][game_simulation][phases][command]") {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(
      simulation::SystemStage::kPreKernel,
      std::make_unique<const testing::RecordedCommandCountProbeSystem>("recorded_command_probe")));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0)}, std::move(declared));

  simulation_game.step(simulation::FixedDelta::canonical(), batch({thrust_command(1, 1.0, 0.0)}));
  REQUIRE(score_of(simulation_game.snapshot(), 1) == 1);
  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  CHECK(score_of(simulation_game.snapshot(), 1) == 0);
}

TEST_CASE("a published snapshot carries no recorded commands",
          "[unit][simulation][game_simulation][snapshot][command][disclosure]") {
  // Engine review finding 4. `Controllable::commands_this_tick` is one entity's live input for the
  // tick being committed. Publishing it would hand every reader of a snapshot -- an in-process bot
  // at Step 23 above all -- every player's input for the tick it is rendering, which is the field
  // protocol v2 deliberately withholds from the wire and a break of the human/bot symmetry in the
  // bot's favour. A system inside the tick still sees it; nothing outside does.
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(
      simulation::SystemStage::kPreKernel,
      std::make_unique<const testing::RecordedCommandCountProbeSystem>("recorded_command_probe")));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0), player(2, 200.0, 50.0)}, std::move(declared));

  simulation_game.step(simulation::FixedDelta::canonical(),
                       batch({thrust_command(1, 1.0, 0.0), thrust_command(2, -1.0, 0.0)}));

  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  // The tick recorded one command for each entity, and the pre-kernel stage read both.
  REQUIRE(score_of(snapshot, 1) == 1);
  REQUIRE(score_of(snapshot, 2) == 1);
  // The publication carries none of them, for any entity.
  for (const simulation::ComponentStore<simulation::Controllable>::Entry& entry :
       snapshot.components<simulation::Controllable>()) {
    CHECK(entry.value.commands_this_tick.empty());
  }
  // The controller link itself is still published, because that is what the component is for.
  CHECK(controllable_of(snapshot, 1).controller_id.value() == 1);
}

TEST_CASE("phase 0 ignores a command that disagrees with the committed world",
          "[unit][simulation][game_simulation][phases][command]") {
  // A despawn for an entity that does not exist and a thrust recorded for an entity that is not
  // live are ignored rather than failing the tick: the command source is a network session, and a
  // hard failure would let one client stop the match.
  // The probe is what makes "the live entity recorded nothing" a real assertion: a publication no
  // longer carries the recorded list, so reading it from the snapshot would assert nothing at all.
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(
      simulation::SystemStage::kPreKernel,
      std::make_unique<const testing::RecordedCommandCountProbeSystem>("recorded_command_probe")));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0, 4.0, 0.0)}, std::move(declared));
  const simulation::WorldSnapshot before = simulation_game.snapshot();

  CHECK_NOTHROW(simulation_game.step(simulation::FixedDelta::canonical(),
                                     batch({despawn_command(9), thrust_command(9, 1.0, 0.0)})));

  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  CHECK(snapshot.tick_sequence().value() == 1);
  REQUIRE(snapshot.entities().size() == 1);
  CHECK(score_of(snapshot, 1) == 0);
  check_vector(snapshot_player(snapshot, 1).position(), 50.01, 50.0);
  CHECK(before.entities().size() == 1);
}

TEST_CASE("a spawn command creates an entity from the tick's reservation and the mode's policy "
          "seats it",
          "[unit][simulation][game_simulation][phases][command][spawn]") {
  // Step 17 asserted a spawn command was a no-op, because seating an unseated entity needed the
  // map's spawn markers and the mode's SpawnPolicy and neither existed. Both exist now: phase 0
  // draws the id from this tick's EntityIdReservation and writes the Controllable that links it to
  // the asking controller, and the engine's SpawnSystem then offers it to the mode's policy over
  // `map.spawn_points()`. The replaced assertion is the point of this step.
  simulation::GameSimulation spawning_game =
      mode_game({}, testing::spawn_point_map(2), testing::TestGameMode::Declaration{});

  CHECK_NOTHROW(spawning_game.step(simulation::FixedDelta::canonical(),
                                   reserved_batch({spawn_command(7)}, 100, 4)));

  const simulation::WorldSnapshot spawned = spawning_game.snapshot();
  REQUIRE(spawned.entities().size() == 1);
  CHECK(spawned.entities()[0] == simulation::EntityId::create(100));
  CHECK(controllable_of(spawned, 100).controller_id.value() == 7);
  // Seated at rest at the first declared spawn point, which is what "at rest" means: a seated
  // entity moves only once its controller asks it to.
  check_vector(snapshot_player(spawned, 100).position(), 50.0, 50.0);
  check_vector(snapshot_player(spawned, 100).velocity(), 0.0, 0.0);
  check_vector(snapshot_player(spawned, 100).acceleration(), 0.0, 0.0);
}

TEST_CASE("a spawn for a controller that already holds a body is ignored",
          "[unit][simulation][game_simulation][phases][command][spawn]") {
  // `InputBatch` collapses repeated spawns within one tick, but nothing stopped a source from
  // asking again in a later tick, so a session or a bot that spawned twice drove one blob and
  // abandoned a second in the arena. A controller drives at most one body, and a duplicate spawn is
  // ignored rather than rejected because the source is an untrusted session
  // (`docs/architecture/0005-royale-mode.md` § "Roster edge rules").
  simulation::GameSimulation spawning_game =
      mode_game({}, testing::spawn_point_map(2), testing::TestGameMode::Declaration{});

  spawning_game.step(simulation::FixedDelta::canonical(),
                     reserved_batch({spawn_command(7)}, 100, 4));
  const simulation::WorldSnapshot after_first = spawning_game.snapshot();
  REQUIRE(after_first.entities().size() == 1);

  // The same controller asks again on a later tick, with a fresh reservation that could seat it.
  spawning_game.step(simulation::FixedDelta::canonical(),
                     reserved_batch({spawn_command(7)}, 200, 4));

  const simulation::WorldSnapshot after_second = spawning_game.snapshot();
  CHECK(after_second.entities().size() == 1);
  CHECK(after_second.entities()[0] == simulation::EntityId::create(100));

  // A different controller is unaffected: the rule is one body per controller, not one body total.
  spawning_game.step(simulation::FixedDelta::canonical(),
                     reserved_batch({spawn_command(8)}, 300, 4));

  const simulation::WorldSnapshot after_other = spawning_game.snapshot();
  REQUIRE(after_other.entities().size() == 2);
  CHECK(controllable_of(after_other, 300).controller_id.value() == 8);
}

TEST_CASE("a spawn command with no spawn point leaves the entity unseated and offers it again",
          "[unit][simulation][game_simulation][phases][command][spawn]") {
  // A policy that declines is a deferral, not a failure: the entity exists carrying its controller
  // link and is offered again, in the same ascending order, on every later tick.
  simulation::GameSimulation spawning_game =
      mode_game({}, arena_map(500.0, 500.0), testing::TestGameMode::Declaration{});

  spawning_game.step(simulation::FixedDelta::canonical(),
                     reserved_batch({spawn_command(7)}, 100, 4));

  const simulation::WorldSnapshot spawned = spawning_game.snapshot();
  REQUIRE(spawned.entities().size() == 1);
  CHECK(spawned.components<simulation::PhysicsBody>().empty());
  CHECK(spawned.players().empty());
  CHECK(controllable_of(spawned, 100).controller_id.value() == 7);
}

TEST_CASE("a spawn on an exhausted reservation fails the tick and commits nothing",
          "[unit][simulation][game_simulation][phases][command][spawn][entity_id_reservation]") {
  // Exhaustion is a hard simulation failure, never a silent skip: a reused id would graft one
  // entity's components onto another.
  simulation::GameSimulation spawning_game =
      mode_game({}, testing::spawn_point_map(4), testing::TestGameMode::Declaration{});
  const simulation::WorldSnapshot before = spawning_game.snapshot();

  CHECK_THROWS_AS(spawning_game.step(simulation::FixedDelta::canonical(),
                                     reserved_batch({spawn_command(7), spawn_command(8)}, 100, 1)),
                  simulation::SimulationValidationError);

  CHECK(spawning_game.snapshot() == before);
  CHECK(spawning_game.tick_sequence() == simulation::TickSequence::zero());
}

TEST_CASE("a kPreKernel system reads the start-of-tick positions",
          "[unit][simulation][game_simulation][stages]") {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(
      simulation::SystemStage::kPreKernel,
      std::make_unique<const testing::PositionProbeSystem>("pre_kernel_position_probe")));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0, 400.0, 0.0)}, std::move(declared));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  CHECK(score_of(simulation_game.snapshot(), 1) == 50);
  check_vector(snapshot_player(simulation_game.snapshot(), 1).position(), 51.0, 50.0);
}

TEST_CASE("a kPreKernel system reads the commands phase 0 recorded for this tick",
          "[unit][simulation][game_simulation][stages][command]") {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(
      simulation::SystemStage::kPreKernel,
      std::make_unique<const testing::RecordedCommandCountProbeSystem>("recorded_command_probe")));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0), player(2, 200.0, 50.0)}, std::move(declared));

  simulation_game.step(simulation::FixedDelta::canonical(), batch({thrust_command(1, 1.0, 0.0)}));

  CHECK(score_of(simulation_game.snapshot(), 1) == 1);
  CHECK(score_of(simulation_game.snapshot(), 2) == 0);
}

TEST_CASE("a kPostKernel system reads the positions the kernel committed and reindexed",
          "[unit][simulation][game_simulation][stages]") {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(
      simulation::SystemStage::kPostKernel,
      std::make_unique<const testing::PositionProbeSystem>("post_kernel_position_probe")));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0, 400.0, 0.0)}, std::move(declared));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  CHECK(score_of(simulation_game.snapshot(), 1) == 51);
  check_vector(snapshot_player(simulation_game.snapshot(), 1).position(), 51.0, 50.0);
}

TEST_CASE("the three stages run in kernel order and each system runs once in its declared order",
          "[unit][simulation][game_simulation][stages][order]") {
  // The trail is one decimal digit per system, appended in the order the systems ran, so the
  // committed number is the whole execution order rather than a set.
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(
      testing::staged(simulation::SystemStage::kLifecycle,
                      std::make_unique<const testing::OrderTrailSystem>("lifecycle_first", 5)));
  declared.push_back(
      testing::staged(simulation::SystemStage::kPreKernel,
                      std::make_unique<const testing::OrderTrailSystem>("pre_kernel_first", 1)));
  declared.push_back(
      testing::staged(simulation::SystemStage::kPostKernel,
                      std::make_unique<const testing::OrderTrailSystem>("post_kernel_first", 3)));
  declared.push_back(
      testing::staged(simulation::SystemStage::kPreKernel,
                      std::make_unique<const testing::OrderTrailSystem>("pre_kernel_second", 2)));
  declared.push_back(
      testing::staged(simulation::SystemStage::kPostKernel,
                      std::make_unique<const testing::OrderTrailSystem>("post_kernel_second", 4)));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0)}, std::move(declared));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  CHECK(order_trail_of(simulation_game.snapshot(), 1) == 12'345);
}

TEST_CASE("a system reads the tick sequence the tick is about to commit",
          "[unit][simulation][game_simulation][stages]") {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(
      simulation::SystemStage::kPreKernel,
      std::make_unique<const testing::TickSequenceProbeSystem>("tick_sequence_probe")));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0)}, std::move(declared));

  advance(simulation_game, 3);

  CHECK(simulation_game.tick_sequence().value() == 3);
  CHECK(order_trail_of(simulation_game.snapshot(), 1) == 3);
}

TEST_CASE("a kPostKernel system reads the events an earlier stage emitted",
          "[unit][simulation][game_simulation][stages][world_event]") {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(
      testing::staged(simulation::SystemStage::kPreKernel,
                      std::make_unique<const testing::EventEmittingSystem>(
                          "pre_kernel_emitter",
                          std::vector<simulation::WorldEvent>{
                              simulation::EliminationEvent{simulation::EntityId::create(1)},
                              simulation::EliminationEvent{simulation::EntityId::create(2)}})));
  declared.push_back(testing::staged(
      simulation::SystemStage::kPostKernel,
      std::make_unique<const testing::EventCountProbeSystem>("post_kernel_event_probe")));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0)}, std::move(declared));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  CHECK(score_of(simulation_game.snapshot(), 1) == 2);
}

TEST_CASE("the commit clears the tick's events, so no snapshot observes one",
          "[unit][simulation][game_simulation][world_event]") {
  // Events are tick-local. A system that emitted on tick 1 sees an empty list on tick 2, which is
  // what makes a consequence that must outlive a tick a component write rather than an event.
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(
      simulation::SystemStage::kPreKernel,
      std::make_unique<const testing::EventCountProbeSystem>("pre_kernel_event_probe")));
  declared.push_back(testing::staged(
      simulation::SystemStage::kPostKernel,
      std::make_unique<const testing::EventEmittingSystem>(
          "post_kernel_emitter", std::vector<simulation::WorldEvent>{simulation::EliminationEvent{
                                     simulation::EntityId::create(1)}})));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0)}, std::move(declared));

  advance(simulation_game, 2);

  CHECK(score_of(simulation_game.snapshot(), 1) == 0);
}

TEST_CASE("a DespawnEvent emitted at a stage leaves the roster at that tick's commit",
          "[unit][simulation][game_simulation][world_event][spatial_grid]") {
  // The commit applies the removal after validation and rebuilds the index from the survivors, so
  // the entity is present for the whole tick that removed it and absent from every later one.
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(
      simulation::SystemStage::kLifecycle,
      std::make_unique<const testing::EventEmittingSystem>(
          "lifecycle_remover", std::vector<simulation::WorldEvent>{
                                   simulation::DespawnEvent{simulation::EntityId::create(1)}})));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 45.0, 50.0, 1.0, 0.0), player(2, 55.0, 50.0, -1.0, 0.0)},
                  std::move(declared), configuration(100.0, 100.0, 5.0, 4, 4));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  const simulation::WorldSnapshot removed = simulation_game.snapshot();
  CHECK_NOTHROW(
      simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()));

  REQUIRE(removed.entities().size() == 1);
  CHECK(removed.entities()[0] == simulation::EntityId::create(2));
  // Entity 1 was live for the whole tick that removed it, so the pair still resolved.
  check_vector(snapshot_player(removed, 2).velocity(), 1.0, 0.0);
  const simulation::WorldSnapshot survivor = simulation_game.snapshot();
  CHECK(survivor.entities().size() == 1);
}

TEST_CASE("a body a stage writes outside the world fails the tick and commits nothing",
          "[unit][simulation][game_simulation][exception_safety][validation]") {
  // Phase 10 validates the surviving world before committing, so a system that wrote an
  // impossible surviving body leaves the previous commit exactly as it was. The continuous envelope
  // permits an initially wall-overlapping disc, so the rejected center must be outside [0,100].
  class OutOfBoundsSystem final : public simulation::SimulationSystem {
  public:
    [[nodiscard]] std::string_view name() const noexcept override { return "out_of_bounds"; }
    void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
      const simulation::EntityId entity = simulation::EntityId::create(1);
      const simulation::PhysicsBody* body = world.store<simulation::PhysicsBody>().find(entity);
      if (body == nullptr) {
        return;
      }
      world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
          entity, body->with_position(simulation::Vector2::create(-1.0, 50.0)));
    }
  };

  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(simulation::SystemStage::kPostKernel,
                                     std::make_unique<const OutOfBoundsSystem>()));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0, 4.0, 0.0)}, std::move(declared),
                  configuration(100.0, 100.0, 10.0, 4, 4));
  const simulation::WorldSnapshot before = simulation_game.snapshot();

  CHECK_THROWS_AS(
      simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()),
      simulation::SimulationValidationError);

  CHECK(simulation_game.snapshot() == before);
  CHECK(simulation_game.tick_sequence() == simulation::TickSequence::zero());
}

TEST_CASE("phase 1 decays velocity geometrically at the configured drag",
          "[unit][simulation][game_simulation][phases][drag]") {
  // v_after = v_before * max(0, 1 - drag_per_second * dt), applied once per tick after the
  // acceleration step.
  constexpr double drag_per_second = 4.0;
  const double factor = 1.0 - (drag_per_second * simulation::FixedDelta::canonical().seconds());
  simulation::GameSimulation simulation_game =
      game({player(1, 50.0, 250.0, 100.0, 0.0)}, dragged_configuration(drag_per_second));

  advance(simulation_game, 3);

  check_vector(snapshot_player(simulation_game.snapshot(), 1).velocity(),
               100.0 * factor * factor * factor, 0.0, simulation::kVelocityTolerance);
  CHECK(factor == Catch::Approx(0.99).margin(simulation::kScalarTolerance));
}

TEST_CASE("phase 1 drag converges to the documented discrete fixed point",
          "[unit][simulation][game_simulation][phases][drag]") {
  // Under a constant stored acceleration the sequence converges to
  // `a * (1 - drag_per_second * dt) / drag_per_second`, not to the continuous-limit
  // `a / drag_per_second`.
  constexpr double drag_per_second = 2.0;
  constexpr double stored_acceleration = 100.0;
  const double delta_seconds = simulation::FixedDelta::canonical().seconds();
  const double discrete_fixed_point =
      stored_acceleration * (1.0 - (drag_per_second * delta_seconds)) / drag_per_second;
  simulation::GameSimulation simulation_game =
      game({player(1, 10.0, 50.0, 0.0, 0.0, stored_acceleration, 0.0)},
           dragged_configuration(drag_per_second, 2'000.0, 100.0, 1.0, 20, 1));

  advance(simulation_game, 6'000);

  CHECK(discrete_fixed_point == Catch::Approx(49.75).margin(simulation::kScalarTolerance));
  CHECK(discrete_fixed_point != stored_acceleration / drag_per_second);
  check_vector(snapshot_player(simulation_game.snapshot(), 1).velocity(), discrete_fixed_point, 0.0,
               simulation::kVelocityTolerance);
}

TEST_CASE("a drag above one tick's worth stops a body at zero and never reverses it",
          "[unit][simulation][game_simulation][phases][drag]") {
  // `drag_per_second * dt` here is 2.5, so the unclamped factor would be -1.5 and the body would
  // reverse. The clamp at zero is what keeps the factor total.
  constexpr double drag_per_second = 1'000.0;
  REQUIRE(drag_per_second * simulation::FixedDelta::canonical().seconds() > 1.0);
  simulation::GameSimulation simulation_game =
      game({player(1, 50.0, 250.0, 100.0, -60.0)}, dragged_configuration(drag_per_second));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  check_vector(snapshot_player(simulation_game.snapshot(), 1).velocity(), 0.0, 0.0);
  check_vector(snapshot_player(simulation_game.snapshot(), 1).position(), 50.0, 250.0);
}

TEST_CASE("SimulationConfig rejects a drag that is not finite or is negative",
          "[unit][simulation][game_simulation][validation][drag]") {
  CHECK_THROWS_AS(dragged_configuration(-0.5), simulation::SimulationValidationError);
  CHECK_THROWS_AS(dragged_configuration(std::numeric_limits<double>::quiet_NaN()),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(dragged_configuration(std::numeric_limits<double>::infinity()),
                  simulation::SimulationValidationError);
  CHECK_NOTHROW(dragged_configuration(0.0));
  CHECK(configuration().drag_per_second() == simulation::SimulationConfig::kDefaultDragPerSecond);
}

namespace {

[[nodiscard]] simulation::Vector2 at(const double x, const double y) {
  return simulation::Vector2::create(x, y);
}

// One wall, as the value a map declares it, addressed by the id a kernel test chose. It is seated
// by `seat_static_body` rather than seeded, because a wall carries a PhysicsBody and nothing else
// and production static content is seated from the map by
// `GameWorld::create(configuration, map, seed)`.
struct SeatedWall final {
  simulation::EntityId entity;
  simulation::PhysicsBody body;
};

[[nodiscard]] SeatedWall wall(const simulation::EntityId::Value id, const double x, const double y,
                              const double velocity_x = 0.0, const double velocity_y = 0.0,
                              const double acceleration_x = 0.0,
                              const double acceleration_y = 0.0) {
  return SeatedWall{simulation::EntityId::create(id),
                    simulation::PhysicsBody::create(
                        at(x, y), at(velocity_x, velocity_y), at(acceleration_x, acceleration_y),
                        simulation::PhysicsBody::kUndeclaredRadius,
                        simulation::PhysicsBody::kDefaultMass,
                        simulation::PhysicsBody::kDefaultCollisionLayer,
                        simulation::PhysicsBody::kDefaultCollisionMask, true)};
}

[[nodiscard]] simulation::GameWorld::EntitySeed
masked_player(const simulation::EntityId::Value id, const double x, const double y,
              const double velocity_x, const simulation::PhysicsBody::CollisionLayer layer,
              const simulation::PhysicsBody::CollisionLayer mask) {
  return simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(id),
      simulation::PhysicsBody::create(at(x, y), at(velocity_x, 0.0), at(0.0, 0.0),
                                      simulation::PhysicsBody::kUndeclaredRadius,
                                      simulation::PhysicsBody::kDefaultMass, layer, mask, false));
}

[[nodiscard]] simulation::GameSimulation
mapped_game(std::vector<simulation::GameWorld::EntitySeed> seeds, simulation::MapDefinition map,
            simulation::SimulationConfig simulation_configuration,
            std::vector<simulation::SystemPipeline::StagedSystem> declared_systems = {},
            const std::vector<SeatedWall>& walls = {}) {
  simulation::GameWorld world = simulation::GameWorld::create(std::move(seeds));
  for (const SeatedWall& seated : walls) {
    testing::seat_static_body(world, seated.entity, seated.body);
  }
  return simulation::GameSimulation::create(
      std::move(simulation_configuration), std::move(world),
      simulation::GameSimulationSetup::engine_defaults()
          .with_map(std::move(map))
          .with_systems(simulation::SystemPipeline::create(std::move(declared_systems))));
}

[[nodiscard]] const simulation::PhysicsBody&
snapshot_body(const simulation::WorldSnapshot& snapshot,
              const simulation::EntityId::Value entity_id) {
  const simulation::EntityId entity = simulation::EntityId::create(entity_id);
  for (const BodyEntry& entry : snapshot.components<simulation::PhysicsBody>()) {
    if (entry.entity == entity) {
      return entry.value;
    }
  }
  FAIL("the committed snapshot carries no PhysicsBody for the probed entity");
  return snapshot.components<simulation::PhysicsBody>().front().value;
}

} // namespace

TEST_CASE("the map is the arena the kernel folds against, not the configuration's world size",
          "[unit][simulation][game_simulation][map_definition]") {
  // The configuration still publishes 500x500 for protocol v1, and the map declares the arena the
  // physics uses. A body driven at the map's edge bounces there and not at the configured one.
  simulation::GameSimulation simulation_game =
      mapped_game({player(1, 90.0, 50.0, 800.0, 0.0)}, arena_map(100.0, 100.0),
                  configuration(500.0, 500.0, 5.0, 4, 4));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  // 90 + 800 * 0.0025 = 92, folded about the 95 upper centre wall is 92; one more tick reaches it.
  check_vector(snapshot_player(simulation_game.snapshot(), 1).position(), 92.0, 50.0);
  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  check_vector(snapshot_player(simulation_game.snapshot(), 1).position(), 94.0, 50.0);
  CHECK(simulation_game.map().bounds() == simulation::ArenaBounds::create(100.0, 100.0));
  CHECK(simulation_game.configuration().world_width() == 500.0);
}

TEST_CASE("a static body is never integrated, accelerated, or dragged",
          "[unit][simulation][game_simulation][static_body][phases]") {
  // The seeded wall carries a velocity and a stored acceleration no phase may consume, and the
  // configuration carries a drag that would decay any velocity phase 1 touched. After a hundred
  // ticks the body is bit-identical to the value the map declared.
  const simulation::PhysicsBody declared = simulation::PhysicsBody::create(
      at(50.0, 50.0), at(7.0, -3.0), at(11.0, 13.0), simulation::PhysicsBody::kUndeclaredRadius,
      simulation::PhysicsBody::kDefaultMass, simulation::PhysicsBody::kDefaultCollisionLayer,
      simulation::PhysicsBody::kDefaultCollisionMask, true);
  simulation::GameSimulation simulation_game =
      mapped_game({player(2, 20.0, 20.0, 1.0, 1.0)}, arena_map(100.0, 100.0),
                  dragged_configuration(2.0, 100.0, 100.0, 5.0, 4, 4), {},
                  {wall(1, 50.0, 50.0, 7.0, -3.0, 11.0, 13.0)});

  advance(simulation_game, 100);

  CHECK(snapshot_body(simulation_game.snapshot(), 1) == declared);
}

TEST_CASE("a dynamic body reflects off a static one and the static one does not move",
          "[unit][simulation][game_simulation][static_body][contact_rule]") {
  // ADR 0003 § "Wall policy" applied to a body: the normal component reverses, the tangential
  // component stays attached to the moving body, and the wall is untouched.
  const simulation::PhysicsBody declared_wall =
      simulation::PhysicsBody::create_static(at(50.0, 50.0));
  simulation::GameSimulation simulation_game =
      mapped_game({player(1, 41.0, 50.0, 1.0, 2.0)}, arena_map(100.0, 100.0),
                  configuration(100.0, 100.0, 5.0, 4, 4), {}, {wall(2, 50.0, 50.0)});

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  check_vector(snapshot_body(snapshot, 1).velocity(), -1.0, 2.0);
  check_vector(snapshot_body(snapshot, 1).position(), 41.0 - 0.0025, 50.0 + 0.005);
  CHECK(snapshot_body(snapshot, 2) == declared_wall);
}

TEST_CASE("a static body meeting a dynamic one at the lower id still reflects the dynamic body",
          "[unit][simulation][game_simulation][static_body][contact_rule][orientation]") {
  // The canonical pair is (1, 2) with the wall at 1, so `reflect_static` matches in the swapped
  // orientation and the kernel maps the returned bodies back onto the canonical pair. Getting that
  // mapping wrong would move the wall and leave the blob alone.
  const simulation::PhysicsBody declared_wall =
      simulation::PhysicsBody::create_static(at(50.0, 50.0));
  simulation::GameSimulation simulation_game =
      mapped_game({player(2, 59.0, 50.0, -1.0, 0.0)}, arena_map(100.0, 100.0),
                  configuration(100.0, 100.0, 5.0, 4, 4), {}, {wall(1, 50.0, 50.0)});

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  check_vector(snapshot_body(snapshot, 2).velocity(), 1.0, 0.0);
  CHECK(snapshot_body(snapshot, 1) == declared_wall);
}

TEST_CASE("a static body on the arena edge is legal content and still reflects",
          "[unit][simulation][game_simulation][static_body][map_definition]") {
  // The obvious boundary obstacle: a wall centred exactly on the arena edge, which is outside the
  // disc-centre interval every dynamic body is held inside. The commit-time bounds check and the
  // spatial index both have to admit it, and this is the case that would otherwise fail the tick.
  // The wall centre at x = 2 is inside the closed arena rectangle and outside the [5, 95] centre
  // interval, which is precisely the case the pre-Step-18 bounds check and index would reject.
  simulation::GameSimulation simulation_game =
      mapped_game({player(1, 11.0, 50.0, -1.0, 0.0)}, arena_map(100.0, 100.0),
                  configuration(100.0, 100.0, 5.0, 4, 4), {}, {wall(2, 2.0, 50.0)});
  REQUIRE_FALSE(simulation_game.map().bounds().contains_disc_center(at(2.0, 50.0), 5.0));

  CHECK_NOTHROW(
      simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()));

  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  check_vector(snapshot_body(snapshot, 1).velocity(), 1.0, 0.0);
  CHECK(snapshot_body(snapshot, 2) == simulation::PhysicsBody::create_static(at(2.0, 50.0)));
}

TEST_CASE("phase 3 emits one contact event naming the row that matched",
          "[unit][simulation][game_simulation][contact_rule][world_event]") {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(
      simulation::SystemStage::kPostKernel,
      std::make_unique<const testing::ContactRuleProbeSystem>("reflect_probe", "reflect_static")));
  simulation::GameSimulation simulation_game = mapped_game(
      {player(1, 41.0, 50.0, 1.0, 0.0)}, arena_map(100.0, 100.0),
      configuration(100.0, 100.0, 5.0, 4, 4), std::move(declared), {wall(2, 50.0, 50.0)});

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  CHECK(score_of(simulation_game.snapshot(), 1) == 1);
}

TEST_CASE("phase 3 names elastic_disc for a dynamic pair",
          "[unit][simulation][game_simulation][contact_rule][world_event]") {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(
      simulation::SystemStage::kPostKernel,
      std::make_unique<const testing::ContactRuleProbeSystem>("elastic_probe", "elastic_disc")));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 45.0, 50.0, 1.0, 0.0), player(2, 55.0, 50.0, -1.0, 0.0)},
                  std::move(declared), configuration(100.0, 100.0, 5.0, 4, 4));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  CHECK(score_of(simulation_game.snapshot(), 1) == 1);
}

TEST_CASE("a separating pair produces no contact event at all",
          "[unit][simulation][game_simulation][contact_rule][world_event]") {
  // The narrow phase rejects separating contacts before the table is consulted, so an overlapping
  // pair that is already moving apart keeps its velocities and publishes nothing.
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(
      simulation::SystemStage::kPostKernel,
      std::make_unique<const testing::ContactRuleProbeSystem>("elastic_probe", "elastic_disc")));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 45.0, 50.0, -1.0, 0.0), player(2, 55.0, 50.0, 1.0, 0.0)},
                  std::move(declared), configuration(100.0, 100.0, 5.0, 4, 4));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  CHECK(score_of(simulation_game.snapshot(), 1) == 0);
  check_vector(snapshot_player(simulation_game.snapshot(), 1).velocity(), -1.0, 0.0);
}

TEST_CASE("phase 3 admits a pair only when both collision masks name the other's layer",
          "[unit][simulation][game_simulation][contact_rule][collision_admission]") {
  // Two dynamic discs in contact and approaching, on disjoint layers. The admission predicate runs
  // before the narrow phase, so neither receives an impulse and both integrate through each other.
  simulation::GameSimulation simulation_game =
      mapped_game({masked_player(1, 45.0, 50.0, 1.0, 0b01U, 0b01U),
                   masked_player(2, 55.0, 50.0, -1.0, 0b10U, 0b10U)},
                  arena_map(100.0, 100.0), configuration(100.0, 100.0, 5.0, 4, 4));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  check_vector(snapshot_body(snapshot, 1).velocity(), 1.0, 0.0);
  check_vector(snapshot_body(snapshot, 2).velocity(), -1.0, 0.0);
}

TEST_CASE("a body a kPreKernel system created is paired in the same tick",
          "[unit][simulation][game_simulation][spatial_grid][stages]") {
  // The index is a function of the body store, not of the entity roster observed at phase 0. A
  // stage that brings a body into the world before phase 2 has to be indexed before pairs are
  // built, or the new body silently produces no candidate pair at all.
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(
      simulation::SystemStage::kPreKernel,
      std::make_unique<const testing::BodyCreatingSystem>(
          "body_creator", simulation::EntityId::create(2),
          simulation::PhysicsBody::create(at(55.0, 50.0), at(-1.0, 0.0), at(0.0, 0.0)))));
  simulation::GameSimulation simulation_game =
      mapped_game({player(1, 45.0, 50.0, 1.0, 0.0)}, arena_map(100.0, 100.0),
                  configuration(100.0, 100.0, 5.0, 4, 4), std::move(declared));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  REQUIRE(snapshot.entities().size() == 2);
  check_vector(snapshot_body(snapshot, 1).velocity(), -1.0, 0.0);
  check_vector(snapshot_body(snapshot, 2).velocity(), 1.0, 0.0);
}

TEST_CASE("a body a kPostKernel system created is in the committed index",
          "[unit][simulation][game_simulation][spatial_grid][stages]") {
  // The stages run after the phase 6 reindex, so a stage-written body would otherwise commit an
  // index that is missing a live body and the next tick would build no pair for it.
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(
      simulation::SystemStage::kPostKernel,
      std::make_unique<const testing::BodyCreatingSystem>(
          "body_creator", simulation::EntityId::create(2),
          simulation::PhysicsBody::create(at(55.0, 50.0), at(-1.0, 0.0), at(0.0, 0.0)))));
  simulation::GameSimulation simulation_game =
      mapped_game({player(1, 45.0, 50.0, 1.0, 0.0)}, arena_map(100.0, 100.0),
                  configuration(100.0, 100.0, 5.0, 4, 4), std::move(declared));

  advance(simulation_game, 2);

  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  REQUIRE(snapshot.entities().size() == 2);
  check_vector(snapshot_body(snapshot, 1).velocity(), -1.0, 0.0);
  check_vector(snapshot_body(snapshot, 2).velocity(), 1.0, 0.0);
}

TEST_CASE("a stage that destroys an entity directly leaves a coherent committed index",
          "[unit][simulation][game_simulation][spatial_grid][stages]") {
  // `destroy_entity` is public and is the obvious call a system author reaches for, so it is the
  // default failure rather than an exotic one: a committed index holding a dead id makes the next
  // tick's pair walk reference a body that no longer exists.
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(simulation::SystemStage::kLifecycle,
                                     std::make_unique<const testing::EntityDestroyingSystem>(
                                         "entity_destroyer", simulation::EntityId::create(2))));
  simulation::GameSimulation simulation_game = mapped_game(
      {player(1, 45.0, 50.0, 1.0, 0.0), player(2, 55.0, 50.0, -1.0, 0.0)}, arena_map(100.0, 100.0),
      configuration(100.0, 100.0, 5.0, 4, 4), std::move(declared));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  const simulation::WorldSnapshot after_removal = simulation_game.snapshot();
  REQUIRE(after_removal.entities().size() == 1);

  CHECK_NOTHROW(
      simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()));
  const simulation::WorldSnapshot after_next_tick = simulation_game.snapshot();
  CHECK(after_next_tick.entities().size() == 1);
}

TEST_CASE("GameSimulation publishes the map and the contact rules it was built with",
          "[unit][simulation][game_simulation][map_definition][contact_rule]") {
  simulation::GameSimulation simulation_game = mapped_game(
      {player(1, 50.0, 50.0)}, arena_map(100.0, 100.0), configuration(100.0, 100.0, 5.0, 4, 4));

  CHECK(simulation_game.map() == arena_map(100.0, 100.0));
  CHECK(simulation_game.contact_rules() == simulation::ContactRuleTable::built_in());
  REQUIRE(simulation_game.contact_rules().size() == 3);
  CHECK(simulation_game.contact_rules().rows()[1].name() ==
        simulation::kElasticDiscContactRuleName);
}

TEST_CASE("a mode that declares no contact rule leaves every admitted contact unchanged",
          "[unit][simulation][game_simulation][contact_rule]") {
  // The table is total without a default row, and the kernel is what has to be total: an empty
  // table means bodies pass through each other rather than failing the tick.
  simulation::GameSimulation simulation_game = simulation::GameSimulation::create(
      configuration(100.0, 100.0, 5.0, 4, 4),
      simulation::GameWorld::create(
          {player(1, 45.0, 50.0, 1.0, 0.0), player(2, 55.0, 50.0, -1.0, 0.0)}),
      simulation::GameSimulationSetup::engine_defaults()
          .with_map(arena_map(100.0, 100.0))
          .with_contact_rules(simulation::ContactRuleTable::empty()));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  check_vector(snapshot_player(simulation_game.snapshot(), 1).velocity(), 1.0, 0.0);
  check_vector(snapshot_player(simulation_game.snapshot(), 2).velocity(), -1.0, 0.0);
}

TEST_CASE("a snapshot's entities cannot disagree with the components it publishes",
          "[unit][simulation][game_simulation][snapshot][entity_roster]") {
  // Engine review finding 2. The world used to hold a roster beside its stores, and
  // `mutable_store<C>()` let either drift from the other, so a stage that wrote a component for an
  // id the roster did not hold published a component for an entity the same snapshot's
  // `entities()` omitted. The roster is derived from the stores now, so this is not a case the
  // snapshot has to get right -- it is a case that cannot be expressed.
  class ScoreGraftingSystem final : public simulation::SimulationSystem {
  public:
    [[nodiscard]] std::string_view name() const noexcept override { return "score_grafter"; }
    void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
      world.mutable_store<simulation::Score>().insert_or_assign(simulation::EntityId::create(77),
                                                                simulation::Score{5});
    }
  };

  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(simulation::SystemStage::kPostKernel,
                                     std::make_unique<const ScoreGraftingSystem>()));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0)}, std::move(declared));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  REQUIRE(snapshot.entities().size() == 2);
  CHECK(snapshot.entities()[0] == simulation::EntityId::create(1));
  CHECK(snapshot.entities()[1] == simulation::EntityId::create(77));
  CHECK(score_of(snapshot, 77) == 5);
  // Every id any published store holds appears exactly once in the published roster.
  for (const simulation::ComponentStore<simulation::Score>::Entry& entry :
       snapshot.components<simulation::Score>()) {
    CHECK(std::count(snapshot.entities().begin(), snapshot.entities().end(), entry.entity) == 1);
  }
}

TEST_CASE("erasing the last component an entity carries removes it from the published roster",
          "[unit][simulation][game_simulation][snapshot][entity_roster]") {
  // The other direction of finding 2: a stage that erases from a store used to leave a roster seat
  // behind, so a snapshot named an entity that published nothing at all.
  class BodyErasingSystem final : public simulation::SimulationSystem {
  public:
    [[nodiscard]] std::string_view name() const noexcept override { return "body_eraser"; }
    void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
      world.mutable_store<simulation::PhysicsBody>().erase(simulation::EntityId::create(2));
      world.mutable_store<simulation::Controllable>().erase(simulation::EntityId::create(2));
    }
  };

  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(simulation::SystemStage::kPostKernel,
                                     std::make_unique<const BodyErasingSystem>()));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0), player(2, 200.0, 50.0)}, std::move(declared));

  CHECK_NOTHROW(
      simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()));

  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  REQUIRE(snapshot.entities().size() == 1);
  CHECK(snapshot.entities()[0] == simulation::EntityId::create(1));
  CHECK(snapshot.players().size() == 1);
}

TEST_CASE("a system creates an entity from the tick's reservation and the tick commits coherently",
          "[unit][simulation][game_simulation][stages][entity_id_reservation][spatial_grid]") {
  // Engine review finding 3. `create_entity()` draws from the tick's reservation, so a hook-stage
  // system can make a projectile, a pickup, or a zone entity. The debug commit invariant asserts
  // the committed index equals a fresh rebuild of the committed world, so a coherent tick here is
  // a coherent roster, index, and snapshot together.
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(
      simulation::SystemStage::kPreKernel,
      std::make_unique<const testing::ReservedEntityCreatingSystem>(
          "projectile_maker",
          simulation::PhysicsBody::create(at(200.0, 200.0), at(4.0, 0.0), at(0.0, 0.0)), 2)));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0)}, std::move(declared));

  CHECK_NOTHROW(
      simulation_game.step(simulation::FixedDelta::canonical(), reserved_batch({}, 500, 4)));

  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  REQUIRE(snapshot.entities().size() == 3);
  CHECK(snapshot.entities()[1] == simulation::EntityId::create(500));
  CHECK(snapshot.entities()[2] == simulation::EntityId::create(501));
  // The ids came out of the reservation in ascending order, which each created entity recorded.
  CHECK(order_trail_of(snapshot, 500) == 500);
  CHECK(order_trail_of(snapshot, 501) == 501);
  // Created before phase 1, so the body integrated on the very tick that created it.
  check_vector(snapshot_player(snapshot, 500).position(), 200.01, 200.0);
  CHECK(snapshot.players().size() == 3);

  // The committed index holds the created ids: the next tick builds pairs over them without
  // failing, which is what a stale index would break. The system creates again from that tick's
  // own reservation, because a reservation belongs to one tick and is cleared at every commit.
  CHECK_NOTHROW(
      simulation_game.step(simulation::FixedDelta::canonical(), reserved_batch({}, 600, 4)));
  const simulation::WorldSnapshot next_snapshot = simulation_game.snapshot();
  CHECK(next_snapshot.entities().size() == 5);
  CHECK(next_snapshot.entities()[1] == simulation::EntityId::create(500));
  CHECK(next_snapshot.entities()[3] == simulation::EntityId::create(600));
}

TEST_CASE("a system that exhausts the tick's reservation fails the tick and commits nothing",
          "[unit][simulation][game_simulation][stages][entity_id_reservation]") {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(
      simulation::SystemStage::kPreKernel,
      std::make_unique<const testing::ReservedEntityCreatingSystem>(
          "greedy_maker",
          simulation::PhysicsBody::create(at(200.0, 200.0), at(0.0, 0.0), at(0.0, 0.0)), 3)));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0)}, std::move(declared));
  const simulation::WorldSnapshot before = simulation_game.snapshot();

  CHECK_THROWS_AS(
      simulation_game.step(simulation::FixedDelta::canonical(), reserved_batch({}, 500, 2)),
      simulation::SimulationValidationError);

  CHECK(simulation_game.snapshot() == before);
  CHECK(simulation_game.tick_sequence() == simulation::TickSequence::zero());
}

TEST_CASE("an entity pushed out of bounds and marked for despawn is removed rather than stopping "
          "the match",
          "[unit][simulation][game_simulation][world_event][validation]") {
  // Engine review finding 14. The commit used to validate bodies before applying this tick's
  // DespawnEvent removals, so an entity a system both pushed out of bounds and marked for despawn
  // stopped the match. Royale's elimination pairs exactly those two: an entity that left the safe
  // zone is frequently one a contact has just pushed past the arena edge. Removal precedes
  // validation now, so the question the commit asks is whether the world it is about to *publish*
  // is legal. Use a center outside the closed arena, not a permitted initial wall overlap.
  class EliminatingSystem final : public simulation::SimulationSystem {
  public:
    [[nodiscard]] std::string_view name() const noexcept override { return "eliminator"; }
    void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
      const simulation::EntityId entity = simulation::EntityId::create(1);
      const simulation::PhysicsBody* body = world.store<simulation::PhysicsBody>().find(entity);
      if (body == nullptr) {
        return;
      }
      world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
          entity, body->with_position(at(-1.0, 50.0)));
      world.emit(simulation::DespawnEvent{entity});
    }
  };

  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(simulation::SystemStage::kPostKernel,
                                     std::make_unique<const EliminatingSystem>()));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0, 4.0, 0.0), player(2, 80.0, 50.0)}, std::move(declared),
                  configuration(100.0, 100.0, 10.0, 4, 4));

  CHECK_NOTHROW(
      simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()));

  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  CHECK(snapshot.tick_sequence().value() == 1);
  REQUIRE(snapshot.entities().size() == 1);
  CHECK(snapshot.entities()[0] == simulation::EntityId::create(2));
}

TEST_CASE("a surviving body written out of bounds still fails the tick",
          "[unit][simulation][game_simulation][world_event][validation]") {
  // The other half of finding 14: reordering removal before validation must not weaken the check
  // for the bodies that actually survive to publication. A wall-overlapping center is legal;
  // this surviving center is genuinely outside the closed arena instead.
  class OutOfBoundsSurvivorSystem final : public simulation::SimulationSystem {
  public:
    [[nodiscard]] std::string_view name() const noexcept override { return "out_of_bounds_keeper"; }
    void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
      const simulation::EntityId entity = simulation::EntityId::create(1);
      const simulation::PhysicsBody* body = world.store<simulation::PhysicsBody>().find(entity);
      if (body == nullptr) {
        return;
      }
      world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
          entity, body->with_position(at(-1.0, 50.0)));
      world.emit(simulation::DespawnEvent{simulation::EntityId::create(2)});
    }
  };

  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(simulation::SystemStage::kPostKernel,
                                     std::make_unique<const OutOfBoundsSurvivorSystem>()));
  simulation::GameSimulation simulation_game =
      staged_game({player(1, 50.0, 50.0, 4.0, 0.0), player(2, 80.0, 50.0)}, std::move(declared),
                  configuration(100.0, 100.0, 10.0, 4, 4));
  const simulation::WorldSnapshot before = simulation_game.snapshot();

  CHECK_THROWS_AS(
      simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()),
      simulation::SimulationValidationError);

  CHECK(simulation_game.snapshot() == before);
}

TEST_CASE("GameSimulation reads every mode declaration once and publishes the name and kinds",
          "[unit][simulation][game_simulation][game_mode]") {
  testing::TestGameMode::Declaration declaration;
  declaration.name = "arena_brawl";
  simulation::GameSimulation simulation_game =
      mode_game({player(1, 50.0, 50.0)}, testing::spawn_point_map(2), std::move(declaration));

  CHECK(simulation_game.mode_name() == "arena_brawl");
  CHECK(simulation_game.accepted_command_kinds() == simulation::CommandKindMask::all());
  CHECK(simulation_game.contact_rules() == simulation::ContactRuleTable::built_in());
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  CHECK(snapshot.match().mode_name() == "arena_brawl");
}

TEST_CASE("a simulation with no declared mode runs the engine's own declarations",
          "[unit][simulation][game_simulation][game_mode]") {
  // `create(configuration, world)` is still the accepted seven-phase baseline: the engine declares
  // a spawn policy that never seats and an objective that never starts a match, so the match stays
  // in `lobby` and no phase transition is ever committed.
  simulation::GameSimulation simulation_game = game({player(1, 50.0, 50.0)});

  advance(simulation_game, 4);

  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  CHECK(simulation_game.mode_name() == simulation::GameSimulation::kEngineDefaultModeName);
  CHECK(snapshot.match().phase() == simulation::MatchPhase::kLobby);
  CHECK(snapshot.match().phase_started_tick() == simulation::TickSequence::zero());
  CHECK_FALSE(snapshot.match().outcome().is_decided());
  CHECK(snapshot.random_draw_counts() == simulation::RandomDrawCounts{});
}

TEST_CASE("a setup that declares both a mode and an explicit pipeline is rejected",
          "[unit][simulation][game_simulation][game_mode][validation]") {
  // Two declarations of one thing is exactly the ambiguity collapsing the four `create` overloads
  // into one setup value was meant to remove, so it is a named failure rather than a precedence
  // rule nobody would remember.
  CHECK_THROWS_AS(simulation::GameSimulation::create(
                      configuration(), simulation::GameWorld::create({player(1, 50.0, 50.0)}),
                      simulation::GameSimulationSetup::of_mode(
                          testing::spawn_point_map(1),
                          testing::TestGameMode::create(testing::TestGameMode::Declaration{}))
                          .with_systems(simulation::SystemPipeline::empty())),
                  simulation::SimulationValidationError);
}

TEST_CASE("a mode that cannot play the map rejects it at construction",
          "[unit][simulation][game_simulation][game_mode][map_definition][validation]") {
  testing::TestGameMode::Declaration declaration;
  declaration.required_spawn_point_count = 4;

  CHECK_THROWS_AS(mode_game({}, testing::spawn_point_map(2), std::move(declaration)),
                  simulation::SimulationValidationError);
}

TEST_CASE("a mode may not shadow the engine's lifecycle system",
          "[unit][simulation][game_simulation][game_mode][match_lifecycle]") {
  // The engine appends its own MatchLifecycleSystem last at kLifecycle and it is not removable, so
  // a mode declaring that name is rejected when the pipeline is built rather than silently
  // replacing the match machine.
  testing::TestGameMode::Declaration declaration;
  declaration.systems.push_back(testing::staged_no_op(
      simulation::SystemStage::kLifecycle, simulation::MatchLifecycleSystem::kSystemName));

  CHECK_THROWS_AS(mode_game({}, testing::spawn_point_map(1), std::move(declaration)),
                  simulation::SimulationValidationError);
}

TEST_CASE("a map whose spawn point cannot seat the configured disc is rejected at construction",
          "[unit][simulation][game_simulation][map_definition][spawn][validation]") {
  // A spawn point is content and a radius is configuration; this is the one place they meet, and
  // it turns a mid-match bounds failure into a startup rejection with a named cause.
  std::vector<simulation::MapDefinition::Marker> markers;
  markers.push_back(simulation::MapDefinition::Marker::spawn(at(2.0, 50.0)));
  simulation::MapDefinition edge_map = simulation::MapDefinition::create(
      "edge_spawn_map", simulation::ArenaBounds::create(100.0, 100.0), {}, std::move(markers),
      simulation::MapMetadata::none());

  CHECK_THROWS_AS(mode_game({}, std::move(edge_map), testing::TestGameMode::Declaration{},
                            configuration(100.0, 100.0, 10.0, 4, 4)),
                  simulation::SimulationValidationError);
}

namespace {

// A dynamic body that declares it crosses the arena instead of folding off its walls. Nothing else
// about it differs from an ordinary blob -- same mass, same restitution -- so it still takes the
// accepted equal-unit-mass equation whenever it meets one, and the only thing under test is the
// three places `BoundsBehavior::kCross` reaches.
[[nodiscard]] simulation::GameWorld::EntitySeed
crossing_player(const simulation::EntityId::Value id, const double x, const double y,
                const double velocity_x = 0.0, const double velocity_y = 0.0) {
  return simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(id),
      simulation::PhysicsBody::create(at(x, y), at(velocity_x, velocity_y), at(0.0, 0.0))
          .with_bounds_behavior(simulation::BoundsBehavior::kCross));
}

// Writes one entity's committed position to a point outside the arena, which is the only way a
// body reaches phase 10's bounds validation without phase 4 having already folded it.
class PushOutsideSystem final : public simulation::SimulationSystem {
public:
  [[nodiscard]] std::string_view name() const noexcept override { return "push_outside"; }
  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    const simulation::EntityId entity = simulation::EntityId::create(1);
    const simulation::PhysicsBody* body = world.store<simulation::PhysicsBody>().find(entity);
    if (body == nullptr) {
      return;
    }
    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
        entity, body->with_position(at(600.0, 250.0)));
  }
};

} // namespace

TEST_CASE("phase 4 folds a bounded body and passes an unbounded one straight through",
          "[unit][simulation][game_simulation][phases][bounds_behavior]") {
  // Same start, same velocity, same arena: the only difference is the declared bounds behaviour.
  // The bounded body reaches the wall on tick 1 and walks back inward; the crossing body keeps its
  // velocity and its heading and is well outside the arena rectangle four ticks later.
  simulation::GameSimulation simulation_game =
      game({player(1, 480.0, 100.0, 4000.0, 0.0), crossing_player(2, 480.0, 300.0, 4000.0, 0.0)});

  advance(simulation_game, 4);
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  check_vector(snapshot_body(snapshot, 1).position(), 460.0, 100.0);
  check_vector(snapshot_body(snapshot, 1).velocity(), -4000.0, 0.0);
  check_vector(snapshot_body(snapshot, 2).position(), 520.0, 300.0);
  check_vector(snapshot_body(snapshot, 2).velocity(), 4000.0, 0.0);
  // Not merely past the disc-centre interval: past the arena rectangle itself.
  CHECK_FALSE(simulation::ArenaBounds::create(500.0, 500.0)
                  .contains(snapshot_body(snapshot, 2).position()));
  CHECK(simulation_game.tick_sequence().value() == 4);
}

TEST_CASE("phase 10 accepts an unbounded body outside the arena and still rejects a bounded one",
          "[unit][simulation][game_simulation][bounds_behavior][validation]") {
  // The same system writes the same impossible-for-a-blob position in both worlds. A bounded body
  // fails the tick and commits nothing, exactly as it always has; a crossing body commits there,
  // because the interval is one it declared it is not held to.
  std::vector<simulation::SystemPipeline::StagedSystem> bounded_declared;
  bounded_declared.push_back(testing::staged(simulation::SystemStage::kPostKernel,
                                             std::make_unique<const PushOutsideSystem>()));
  simulation::GameSimulation bounded_game =
      staged_game({player(1, 250.0, 250.0)}, std::move(bounded_declared));
  const simulation::WorldSnapshot before = bounded_game.snapshot();

  CHECK_THROWS_AS(
      bounded_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()),
      simulation::SimulationValidationError);
  CHECK(bounded_game.snapshot() == before);

  std::vector<simulation::SystemPipeline::StagedSystem> crossing_declared;
  crossing_declared.push_back(testing::staged(simulation::SystemStage::kPostKernel,
                                              std::make_unique<const PushOutsideSystem>()));
  simulation::GameSimulation crossing_game =
      staged_game({crossing_player(1, 250.0, 250.0)}, std::move(crossing_declared));

  CHECK_NOTHROW(
      crossing_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()));
  const simulation::WorldSnapshot crossing_snapshot = crossing_game.snapshot();
  check_vector(snapshot_body(crossing_snapshot, 1).position(), 600.0, 250.0);
  CHECK(crossing_game.tick_sequence().value() == 1);
}

TEST_CASE("a bounded body commits the identical bodies whether or not a crossing body shares the "
          "world",
          "[unit][simulation][game_simulation][bounds_behavior][determinism]") {
  // The addition must be invisible to everything that existed before it. This is exact equality on
  // the committed PhysicsBody, not a tolerance: the bounded body's forty ticks of acceleration,
  // wall folding, and integration must be bit-for-bit what they are with no hazard in the world.
  simulation::GameSimulation alone = game({player(1, 480.0, 100.0, 4000.0, 0.0)});
  simulation::GameSimulation shared =
      game({player(1, 480.0, 100.0, 4000.0, 0.0), crossing_player(2, 480.0, 300.0, 4000.0, 0.0)});

  advance(alone, 40);
  advance(shared, 40);
  const simulation::WorldSnapshot alone_snapshot = alone.snapshot();
  const simulation::WorldSnapshot shared_snapshot = shared.snapshot();

  CHECK(snapshot_body(shared_snapshot, 1) == snapshot_body(alone_snapshot, 1));
  CHECK(shared_snapshot.entities().size() == 2);
}

TEST_CASE("a crossing body outside the wall still resolves a contact with the blob inside it",
          "[unit][simulation][game_simulation][bounds_behavior][spatial_grid]") {
  // The payoff of clamping the crossing body into the edge cells rather than leaving it out of the
  // index while it is outside. The two centres are fifteen apart with a radius of ten, so the discs
  // overlap and the pair is approaching; an unindexed body would have made this a missed contact
  // rather than a deferred one. Both bodies are baseline, so the equation applied is still the
  // accepted equal-unit-mass exchange.
  simulation::GameSimulation simulation_game =
      game({player(1, 485.0, 250.0, 0.0, 0.0), crossing_player(2, 500.0, 250.0, -400.0, 0.0)});
  simulation::GameSimulation without_hazard = game({player(1, 485.0, 250.0, 0.0, 0.0)});

  advance(simulation_game, 1);
  advance(without_hazard, 1);
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  const simulation::WorldSnapshot without_hazard_snapshot = without_hazard.snapshot();

  // The normal is exactly (1, 0), so the exchange hands the resting blob the hazard's speed.
  check_vector(snapshot_body(snapshot, 1).velocity(), -400.0, 0.0);
  check_vector(snapshot_body(snapshot, 2).velocity(), 0.0, 0.0);
  // The counterfactual: with no hazard in the world the blob does not move at all, so the velocity
  // above is the contact and nothing else.
  check_vector(snapshot_body(without_hazard_snapshot, 1).velocity(), 0.0, 0.0);
}

TEST_CASE("a variable body deflects a blob through the kernel while baseline pairs do not reach "
          "the general rule",
          "[unit][simulation][game_simulation][contact_rule_table][variable_impulse]") {
  // The row is reachable from a whole tick, not only from a direct call, and it is reachable only
  // by the pair it is predicated on. A fifty-to-one hazard barely slows while the blob is thrown;
  // the same arrangement with two baseline bodies exchanges velocities exactly as it always has.
  simulation::GameSimulation heavy_game =
      game({simulation::GameWorld::EntitySeed::create(
                simulation::EntityId::create(1),
                simulation::PhysicsBody::create(at(470.0, 250.0), at(400.0, 0.0), at(0.0, 0.0))
                    .with_mass(50.0)),
            player(2, 485.0, 250.0, 0.0, 0.0)});
  simulation::GameSimulation baseline_game =
      game({player(1, 470.0, 250.0, 400.0, 0.0), player(2, 485.0, 250.0, 0.0, 0.0)});

  advance(heavy_game, 1);
  advance(baseline_game, 1);
  const simulation::WorldSnapshot heavy_snapshot = heavy_game.snapshot();
  const simulation::WorldSnapshot baseline_snapshot = baseline_game.snapshot();

  // j = -(1 + 1) * (-400) / (1/50 + 1) = 800/1.02, so the heavy body sheds 800/51 of its 400.
  check_vector(snapshot_body(heavy_snapshot, 1).velocity(), 400.0 - (800.0 / 51.0), 0.0,
               simulation::kVelocityTolerance);
  check_vector(snapshot_body(heavy_snapshot, 2).velocity(), 40'000.0 / 51.0, 0.0,
               simulation::kVelocityTolerance);
  // The baseline pair took `elastic_disc`: a plain exchange of normal components, unchanged.
  check_vector(snapshot_body(baseline_snapshot, 1).velocity(), 0.0, 0.0);
  check_vector(snapshot_body(baseline_snapshot, 2).velocity(), 400.0, 0.0);
}

TEST_CASE("a large hazard collides at its own drawn edge through the whole tick",
          "[unit][simulation][game_simulation][contact_rule_table][variable_impulse][radius]") {
  // End to end through the three places that had to agree: the broad phase has to offer the pair,
  // phase 3's gate has to admit it, and the row has to resolve it. The centres are thirty-five
  // apart, which is outside the configured `2 * 10` and inside the hazard's own `26 + 10`, so every
  // one of the three had to measure per body for this to happen at all.
  simulation::GameSimulation hazard_game =
      game({simulation::GameWorld::EntitySeed::create(
                simulation::EntityId::create(1),
                simulation::PhysicsBody::create(at(250.0, 250.0), at(400.0, 0.0), at(0.0, 0.0))
                    .with_radius(26.0)
                    .with_mass(40.0)),
            player(2, 285.0, 250.0, 0.0, 0.0)});
  // The same arrangement with two configured-size blobs is not a contact and must stay one.
  simulation::GameSimulation baseline_game =
      game({player(1, 250.0, 250.0, 400.0, 0.0), player(2, 285.0, 250.0, 0.0, 0.0)});

  advance(hazard_game, 1);
  advance(baseline_game, 1);
  const simulation::WorldSnapshot hazard_snapshot = hazard_game.snapshot();
  const simulation::WorldSnapshot baseline_snapshot = baseline_game.snapshot();

  // j = -(1 + 1) * (-400) / (1/40 + 1) = 32000/41, so the hazard sheds 800/41 and the blob is
  // thrown at the whole of it.
  check_vector(snapshot_body(hazard_snapshot, 1).velocity(), 400.0 - (800.0 / 41.0), 0.0,
               simulation::kVelocityTolerance);
  check_vector(snapshot_body(hazard_snapshot, 2).velocity(), 32'000.0 / 41.0, 0.0,
               simulation::kVelocityTolerance);
  // The hazard keeps its declared radius through the response: a row may change only velocities.
  CHECK(snapshot_body(hazard_snapshot, 1).radius() == 26.0);
  // The counterfactual. Two configured-size blobs thirty-five apart never touched and still do not,
  // which is what "an addition" has to mean at the tick level.
  check_vector(snapshot_body(baseline_snapshot, 1).velocity(), 400.0, 0.0);
  check_vector(snapshot_body(baseline_snapshot, 2).velocity(), 0.0, 0.0);
}

TEST_CASE("the default drag scale reproduces the pre-change phase 1 bit-for-bit",
          "[unit][simulation][game_simulation][phases][drag][drag_scale][determinism]") {
  // The whole of the argument that a per-body drag coefficient is an *addition*, evaluated rather
  // than asserted. `reference` below is phase 1 exactly as it stood before a body could name a
  // scale: it hands `apply_velocity_drag` the configured `drag_per_second` and nothing else. The
  // kernel now hands it `drag_per_second * body.drag_scale()`, and every body that exists carries
  // `kDefaultDragScale`, which is `1.0`; multiplication by `1.0` is exact in binary64 for every
  // finite value, so the two must agree by **exact double equality** on every tick of the horizon.
  // A tolerance here would hide a real divergence that happened to start small, which is the same
  // reason `AcceptedBaselineTick` compares exactly.
  constexpr double drag_per_second = 2.0;
  constexpr std::size_t horizon = 1'000;
  const simulation::FixedDelta delta = simulation::FixedDelta::canonical();
  const simulation::Vector2 stored_acceleration = at(30.0, 15.0);
  const simulation::Vector2 launch = at(120.0, -80.0);
  simulation::GameSimulation simulation_game =
      game({player(1, 250.0, 250.0, launch.x(), launch.y(), stored_acceleration.x(),
                   stored_acceleration.y())},
           dragged_configuration(drag_per_second));
  REQUIRE(snapshot_body(simulation_game.snapshot(), 1).drag_scale() ==
          simulation::PhysicsBody::kDefaultDragScale);

  simulation::Vector2 reference = launch;
  std::size_t first_divergent_tick = 0;
  for (std::size_t tick = 1; tick <= horizon; ++tick) {
    reference = simulation::apply_velocity_drag(
        simulation::integrate_accelerated_velocity(reference, stored_acceleration, delta),
        drag_per_second, delta);
    simulation_game.step(delta, simulation::InputBatch::empty());
    const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
    if (first_divergent_tick == 0 && !(snapshot_body(snapshot, 1).velocity() == reference)) {
      first_divergent_tick = tick;
    }
  }

  CHECK(first_divergent_tick == 0);
  // The horizon is meaningful rather than decorative, and this is the check that says so rather
  // than trusting the loop count. Two and a half seconds of drag carry the body's x velocity from
  // its launch of 120 wu/s down the closed-form decay of the phase 1 recurrence,
  // `v_n = fixed + (v_0 - fixed) * f^n` with `f = 1 - d * dt`, most of the way onto the discrete
  // fixed point `a * (1 - d * dt) / d` this ADR documents. A scale that had leaked into the
  // arithmetic anywhere had that whole trajectory to show itself in.
  const double factor = 1.0 - (drag_per_second * delta.seconds());
  const double fixed_point = stored_acceleration.x() * factor / drag_per_second;
  CHECK(fixed_point == Catch::Approx(14.925).margin(simulation::kScalarTolerance));
  CHECK(reference.x() ==
        Catch::Approx(fixed_point +
                      ((launch.x() - fixed_point) * std::pow(factor, static_cast<double>(horizon))))
            .margin(simulation::kScalarTolerance));
  CHECK(reference.x() < 0.15 * launch.x());
  CHECK(simulation_game.tick_sequence().value() == horizon);
}

TEST_CASE("a body at zero drag scale keeps its velocity exactly and crosses an arena a dragged one "
          "cannot",
          "[unit][simulation][game_simulation][phases][drag][drag_scale]") {
  // Why the value exists at all. Phase 1's drag factor is geometric, so a body launched at `v` and
  // never thrusting again covers exactly `v / drag_per_second` world units in total -- at a drag of
  // two, a 400 wu/s object has a range of 200 wu and then parks. The two bodies here differ in
  // nothing but the declared scale, and they are the mechanic and the defect side by side.
  //
  // The coasting body's velocity is checked with exact equality, not a tolerance: at
  // `drag_scale = 0` the product `drag_per_second * 0.0` is exactly `+0.0`, the factor is exactly
  // `1.0`, and multiplication by `1.0` is the identity on every finite binary64 value. "Nearly its
  // launch velocity" would be a different and weaker claim.
  constexpr double drag_per_second = 2.0;
  constexpr std::size_t horizon = 1'000;
  const simulation::Vector2 launch = at(400.0, 0.0);
  simulation::GameSimulation simulation_game =
      game({simulation::GameWorld::EntitySeed::create(
                simulation::EntityId::create(1),
                simulation::PhysicsBody::create(at(250.0, 100.0), launch, at(0.0, 0.0))
                    .with_drag_scale(simulation::PhysicsBody::kMinimumDragScale)
                    .with_bounds_behavior(simulation::BoundsBehavior::kCross)),
            player(2, 250.0, 400.0, launch.x(), launch.y())},
           dragged_configuration(drag_per_second));

  std::size_t first_divergent_tick = 0;
  for (std::size_t tick = 1; tick <= horizon; ++tick) {
    simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
    const simulation::WorldSnapshot stepped = simulation_game.snapshot();
    if (first_divergent_tick == 0 && !(snapshot_body(stepped, 1).velocity() == launch)) {
      first_divergent_tick = tick;
    }
  }
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  CHECK(first_divergent_tick == 0);
  // Two and a half seconds at a constant 400 wu/s is a thousand world units of travel, so the
  // coasting body is a long way past the 500 wu arena it was launched into -- which is what a
  // hazard crossing the screen has to be able to do.
  CHECK(snapshot_body(snapshot, 1).position().x() > 1'000.0);
  // The counterfactual, and the defect this value was added to fix. The same launch, the same
  // configured drag, the same arena: a body at the default scale asymptotes onto `250 + 400 / 2`
  // and sits there as an obstacle for the rest of its life.
  CHECK(snapshot_body(snapshot, 2).position().x() < 450.0);
  CHECK(snapshot_body(snapshot, 2).position().x() > 445.0);
  CHECK(snapshot_body(snapshot, 2).velocity().x() < 0.01 * launch.x());
}

TEST_CASE("the drag scale multiplies the configured drag before the factor is formed",
          "[unit][simulation][game_simulation][phases][drag][drag_scale][determinism]") {
  // The operation order is the contract, so it is pinned rather than described. `4.0 * 0.5` is
  // exactly `2.0` in binary64, so a body feeling half of a drag of four must commit bit-identical
  // velocities to a body feeling all of a drag of two. Forming the factor first and scaling *that*
  // -- `0.5 * max(0, 1 - 4 * dt)` = 0.495 against `max(0, 1 - 2 * dt)` = 0.995 -- would fail this
  // on the first tick, which is what makes it a test of the order rather than of the arithmetic.
  constexpr std::size_t horizon = 400;
  simulation::GameSimulation halved_game =
      game({simulation::GameWorld::EntitySeed::create(
               simulation::EntityId::create(1),
               simulation::PhysicsBody::create(at(250.0, 250.0), at(120.0, -80.0), at(30.0, 15.0))
                   .with_drag_scale(0.5))},
           dragged_configuration(4.0));
  simulation::GameSimulation whole_game =
      game({player(1, 250.0, 250.0, 120.0, -80.0, 30.0, 15.0)}, dragged_configuration(2.0));

  advance(halved_game, horizon);
  advance(whole_game, horizon);
  const simulation::WorldSnapshot halved_snapshot = halved_game.snapshot();
  const simulation::WorldSnapshot whole_snapshot = whole_game.snapshot();

  CHECK(snapshot_body(halved_snapshot, 1).velocity() ==
        snapshot_body(whole_snapshot, 1).velocity());
  CHECK(snapshot_body(halved_snapshot, 1).position() ==
        snapshot_body(whole_snapshot, 1).position());
  // And the two worlds really were dragged: an undragged body would still be at its launch
  // velocity.
  CHECK_FALSE(snapshot_body(whole_snapshot, 1).velocity() == at(120.0, -80.0));
}

TEST_CASE("direct mode setup preserves the chained factory value and committed snapshots",
          "[unit][simulation][game_simulation][setup][determinism]") {
  const auto terrain = simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(500.0, 500.0), simulation::TerrainGround::kSolid, {},
      {simulation::TerrainHole::create("far_pit", at(400.0, 400.0), 5.0)});
  const auto map = simulation::MapDefinition::create("setup_proof", terrain, {}, {},
                                                     simulation::MapMetadata::none());
  auto direct = simulation::GameSimulationSetup::of_mode(map, testing::TestGameMode::create({}));
  auto chained = simulation::GameSimulationSetup::engine_defaults().with_map(map).with_mode(
      testing::TestGameMode::create({}));
  CHECK(direct.has_map() == chained.has_map());
  CHECK(direct.has_mode() == chained.has_mode());
  CHECK_FALSE(direct.has_systems());
  CHECK_FALSE(direct.has_contact_rules());
  auto direct_game = simulation::GameSimulation::create(
      configuration(), simulation::GameWorld::create({player(1, 100.0, 100.0, 17.0, -9.0)}),
      std::move(direct));
  auto chained_game = simulation::GameSimulation::create(
      configuration(), simulation::GameWorld::create({player(1, 100.0, 100.0, 17.0, -9.0)}),
      std::move(chained));
  CHECK(direct_game.snapshot() == chained_game.snapshot());
  for (std::size_t tick = 0; tick < 50; ++tick) {
    advance(direct_game, 1);
    advance(chained_game, 1);
    CHECK(direct_game.snapshot() == chained_game.snapshot());
  }
}
