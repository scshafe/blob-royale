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

TEST_CASE("equal-distance contacts resolve once in lexicographic EntityId order",
          "[unit][simulation][game_simulation][collision][order]") {
  simulation::GameSimulation simulation_game = game(equal_distance_players(true));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
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

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
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

TEST_CASE("player crossing during integration receives no swept collision impulse",
          "[unit][simulation][game_simulation][collision][discrete]") {
  simulation::GameSimulation simulation_game =
      game({player(1, 30.0, 50.0, 10'000.0, 0.0), player(2, 70.0, 50.0, -10'000.0, 0.0)},
           configuration(100.0, 100.0, 5.0, 4, 4));

  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  check_vector(snapshot_player(snapshot, 1).position(), 55.0, 50.0);
  check_vector(snapshot_player(snapshot, 1).velocity(), 10'000.0, 0.0);
  check_vector(snapshot_player(snapshot, 2).position(), 45.0, 50.0);
  check_vector(snapshot_player(snapshot, 2).velocity(), -10'000.0, 0.0);
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

TEST_CASE("migrated wall fixture reaches the lower wall inward on tick 2000",
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

// canonical: accepted_baseline_tick -- the seven-phase tick this kernel must still reproduce.
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

TEST_CASE("an empty pipeline, zero drag, and an empty batch reproduce the accepted fixture horizon "
          "bit-for-bit",
          "[unit][simulation][game_simulation][fixture][horizon][determinism]") {
  simulation::GameSimulation kernel_game = game(migrated_pair_fixture_players());
  AcceptedBaselineTick baseline(configuration(),
                                simulation::GameWorld::create(migrated_pair_fixture_players()));
  REQUIRE(kernel_game.configuration().drag_per_second() == 0.0);

  std::size_t first_divergent_tick = 0;
  for (std::size_t tick = 1; tick <= kMigratedPairFixtureHorizon; ++tick) {
    kernel_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
    baseline.step();
    if (first_divergent_tick == 0 &&
        !bodies_are_bit_identical(kernel_game.snapshot(), baseline.bodies())) {
      first_divergent_tick = tick;
    }
  }

  CHECK(first_divergent_tick == 0);
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
  declared.push_back(testing::staged(
      simulation::SystemStage::kPreKernel,
      std::make_unique<const testing::EventEmittingSystem>(
          "pre_kernel_emitter", std::vector<simulation::WorldEvent>{
                                    simulation::EliminationEvent{simulation::EntityId::create(1)},
                                    simulation::ScoreEvent{simulation::EntityId::create(1), 4}})));
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
  // Phase 10 validates before it removes, replaces, or increments, so a system that wrote an
  // impossible body leaves the previous commit exactly as it was.
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
          entity, body->with_position(simulation::Vector2::create(1.0, 50.0)));
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
                        simulation::PhysicsBody::kDefaultRadius,
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
                                      simulation::PhysicsBody::kDefaultRadius,
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
      at(50.0, 50.0), at(7.0, -3.0), at(11.0, 13.0), simulation::PhysicsBody::kDefaultRadius,
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
  REQUIRE(simulation_game.contact_rules().size() == 2);
  CHECK(simulation_game.contact_rules().rows()[0].name() ==
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
  // is legal.
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
          entity, body->with_position(at(1.0, 50.0)));
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
  // for the bodies that actually survive to publication.
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
          entity, body->with_position(at(1.0, 50.0)));
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
  CHECK(simulation_game.contact_rules().size() == 2);
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
  CHECK(snapshot.random_draw_count() == 0);
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
