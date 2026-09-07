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
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "entity_id_reservation.hpp"
#include "fixed_delta.hpp"
#include "game_simulation.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
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
      simulation::SystemPipeline::create(std::move(declared_systems)));
}

[[nodiscard]] simulation::InputBatch batch(std::vector<simulation::Command> commands) {
  return simulation::InputBatch::create(std::move(commands), simulation::CommandKindMask::all(),
                                        simulation::EntityIdReservation::none());
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
  simulation::GameSimulation simulation_game =
      game({player(1, 50.0, 50.0), player(2, 200.0, 50.0)});

  simulation_game.step(simulation::FixedDelta::canonical(), batch({thrust_command(1, 0.5, -0.25)}));
  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();

  const simulation::Controllable& recorded = controllable_of(snapshot, 1);
  REQUIRE(recorded.commands_this_tick.size() == 1);
  CHECK(recorded.commands_this_tick[0] == thrust_command(1, 0.5, -0.25));
  CHECK(controllable_of(snapshot, 2).commands_this_tick.empty());
  // The kernel records and does not interpret: no acceleration and no velocity moved.
  check_vector(snapshot_player(snapshot, 1).acceleration(), 0.0, 0.0);
  check_vector(snapshot_player(snapshot, 1).velocity(), 0.0, 0.0);
}

TEST_CASE("phase 0 clears the previous tick's recorded commands",
          "[unit][simulation][game_simulation][phases][command]") {
  simulation::GameSimulation simulation_game = game({player(1, 50.0, 50.0)});

  simulation_game.step(simulation::FixedDelta::canonical(), batch({thrust_command(1, 1.0, 0.0)}));
  REQUIRE(controllable_of(simulation_game.snapshot(), 1).commands_this_tick.size() == 1);
  simulation_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  CHECK(controllable_of(simulation_game.snapshot(), 1).commands_this_tick.empty());
}

TEST_CASE("phase 0 ignores a command that disagrees with the committed world",
          "[unit][simulation][game_simulation][phases][command]") {
  // A despawn for an entity that does not exist and a thrust recorded for an entity that is not
  // live are ignored rather than failing the tick: the command source is a network session, and a
  // hard failure would let one client stop the match.
  simulation::GameSimulation simulation_game = game({player(1, 50.0, 50.0, 4.0, 0.0)});
  const simulation::WorldSnapshot before = simulation_game.snapshot();

  CHECK_NOTHROW(simulation_game.step(simulation::FixedDelta::canonical(),
                                     batch({despawn_command(9), thrust_command(9, 1.0, 0.0)})));

  const simulation::WorldSnapshot snapshot = simulation_game.snapshot();
  CHECK(snapshot.tick_sequence().value() == 1);
  REQUIRE(snapshot.entities().size() == 1);
  CHECK(controllable_of(snapshot, 1).commands_this_tick.empty());
  check_vector(snapshot_player(snapshot, 1).position(), 50.01, 50.0);
  CHECK(before.entities().size() == 1);
}

TEST_CASE("a spawn command is a no-op until the Step 19 spawn policy seats it",
          "[unit][simulation][game_simulation][phases][command][spawn]") {
  // Seating an unseated entity needs the map's spawn markers and the mode's SpawnPolicy, which
  // arrive in Step 19. Until then a spawn is applied as nothing at all rather than seated at a
  // position no declaration chose, and this test is what turns red the day seating lands without
  // a horizon being re-examined.
  simulation::GameSimulation spawning_game = game({player(1, 50.0, 50.0, 4.0, -2.0)});
  simulation::GameSimulation quiet_game = game({player(1, 50.0, 50.0, 4.0, -2.0)});

  CHECK_NOTHROW(spawning_game.step(simulation::FixedDelta::canonical(),
                                   batch({spawn_command(7), spawn_command(8)})));
  quiet_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  const simulation::WorldSnapshot spawned = spawning_game.snapshot();
  CHECK(spawned == quiet_game.snapshot());
  CHECK(spawned.entities().size() == 1);
  CHECK(controllable_of(spawned, 1).commands_this_tick.empty());
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
