#include "command_registry.hpp"
#include "commands/thrust_command.hpp"
#include "component_registry.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/lifetime_component.hpp"
#include "components/score_component.hpp"
#include "components/team_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "fixed_delta.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "map_definition.hpp"
#include "match_phase.hpp"
#include "match_snapshot.hpp"
#include "mode_match_state_registry.hpp"
#include "physics_body.hpp"
#include "player_snapshot.hpp"
#include "simulation_config.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "team_id.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

template <typename Value>
concept RvalueTerrainReadable = requires(Value&& value) { std::move(value).terrain(); };

static_assert(std::is_same_v<decltype(std::declval<const simulation::WorldSnapshot&>().terrain()),
                             const simulation::TerrainDefinition&>);
static_assert(!RvalueTerrainReadable<simulation::WorldSnapshot>);
static_assert(!RvalueTerrainReadable<const simulation::WorldSnapshot>);

struct PlayerFixture final {
  simulation::EntityId::Value entity_id;
  double position_x;
  double position_y;
  double velocity_x;
  double velocity_y;
  double acceleration_x;
  double acceleration_y;
};

constexpr PlayerFixture kLowerIdPlayer{2, 20.0, 21.0, 2.5, -3.5, 0.25, -0.5};
constexpr PlayerFixture kHigherIdPlayer{9, 90.0, 91.0, -4.5, 5.5, -0.75, 1.0};
constexpr double kSnapshotHoleRadius = 20.0;

// A non-rectangular authored map proves publication retains terrain rather than recreating the
// configuration's rectangular bounds. The hole is disjoint from the corridor's centreline.
[[nodiscard]] simulation::MapDefinition
snapshot_terrain_map(const double hole_radius = kSnapshotHoleRadius) {
  return simulation::MapDefinition::create(
      "snapshot_terrain",
      simulation::TerrainDefinition::create(
          simulation::ArenaBounds::create(500.0, 500.0), simulation::TerrainGround::kCorridors,
          {simulation::TerrainCorridor::create("road", 80.0,
                                               {simulation::Vector2::create(100.0, 250.0),
                                                simulation::Vector2::create(400.0, 250.0)})},
          {simulation::TerrainHole::create("pit", simulation::Vector2::create(200.0, 290.0),
                                           hole_radius)}),
      {}, {}, simulation::MapMetadata::none());
}

[[nodiscard]] simulation::SimulationConfig snapshot_configuration(const double width = 500.0,
                                                                  const double height = 500.0,
                                                                  const double radius = 1.0,
                                                                  const std::uint64_t columns = 10,
                                                                  const std::uint64_t rows = 10) {
  return simulation::SimulationConfig::create(
      width, height, radius, simulation::SimulationConfig::kRequiredTicksPerSecond, columns, rows);
}

[[nodiscard]] simulation::GameSimulation
simulation_from_world(simulation::GameWorld world,
                      simulation::SimulationConfig configuration = snapshot_configuration()) {
  return simulation::GameSimulation::create(configuration, std::move(world));
}

[[nodiscard]] simulation::GameWorld::EntitySeed seed_from_fixture(const PlayerFixture& fixture) {
  return simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(fixture.entity_id),
      simulation::PhysicsBody::create(
          simulation::Vector2::create(fixture.position_x, fixture.position_y),
          simulation::Vector2::create(fixture.velocity_x, fixture.velocity_y),
          simulation::Vector2::create(fixture.acceleration_x, fixture.acceleration_y)));
}

[[nodiscard]] simulation::GameWorld::EntitySeed
positioned_seed(const simulation::EntityId::Value entity_id, const double x, const double y) {
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  return simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(entity_id),
      simulation::PhysicsBody::create(simulation::Vector2::create(x, y), zero, zero));
}

} // namespace

TEST_CASE("TickSequence is exact, ordered, and bounded without unsigned wraparound",
          "[unit][simulation][snapshot]") {
  const simulation::TickSequence zero = simulation::TickSequence::zero();
  const simulation::TickSequence one = zero.next();
  const simulation::TickSequence maximum =
      simulation::TickSequence::create(simulation::TickSequence::kMaximumValue);

  CHECK(zero.value() == simulation::TickSequence::kMinimumValue);
  CHECK(one.value() == 1);
  CHECK(zero < one);
  CHECK(maximum.value() == simulation::TickSequence::kMaximumValue);
  CHECK_THROWS_AS(maximum.next(), simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::TickSequence::create(simulation::TickSequence::kMaximumValue + 1),
                  simulation::SimulationValidationError);
}

TEST_CASE("WorldSnapshot copies complete player data in canonical EntityId order",
          "[unit][simulation][snapshot]") {
  const simulation::GameWorld world = simulation::GameWorld::create(
      {seed_from_fixture(kHigherIdPlayer), seed_from_fixture(kLowerIdPlayer)});
  const simulation::WorldSnapshot snapshot = simulation_from_world(world).snapshot();

  REQUIRE(snapshot.players().size() == 2);
  CHECK(snapshot.tick_sequence() == simulation::TickSequence::zero());
  CHECK(snapshot.players()[0].entity_id().value() == kLowerIdPlayer.entity_id);
  CHECK(snapshot.players()[0].position() ==
        simulation::Vector2::create(kLowerIdPlayer.position_x, kLowerIdPlayer.position_y));
  CHECK(snapshot.players()[0].velocity() ==
        simulation::Vector2::create(kLowerIdPlayer.velocity_x, kLowerIdPlayer.velocity_y));
  CHECK(snapshot.players()[0].acceleration() ==
        simulation::Vector2::create(kLowerIdPlayer.acceleration_x, kLowerIdPlayer.acceleration_y));
  CHECK(snapshot.players()[1].entity_id().value() == kHigherIdPlayer.entity_id);
  CHECK(snapshot.players()[1].position() ==
        simulation::Vector2::create(kHigherIdPlayer.position_x, kHigherIdPlayer.position_y));
  CHECK(snapshot.players()[1].velocity() ==
        simulation::Vector2::create(kHigherIdPlayer.velocity_x, kHigherIdPlayer.velocity_y));
  CHECK(
      snapshot.players()[1].acceleration() ==
      simulation::Vector2::create(kHigherIdPlayer.acceleration_x, kHigherIdPlayer.acceleration_y));
}

TEST_CASE("WorldSnapshot separates the live physical limit from the maximum entity population",
          "[unit][simulation][snapshot]") {
  const simulation::WorldSnapshot empty_snapshot =
      simulation_from_world(simulation::GameWorld::create({})).snapshot();

  std::vector<simulation::GameWorld::EntitySeed> maximum_seeds;
  maximum_seeds.reserve(simulation::kMaximumEntityCount);
  for (std::size_t index = 0; index < simulation::kMaximumEntityCount; ++index) {
    const double x = 1.0 + (2.0 * static_cast<double>(index % 64));
    const double y = 1.0 + (2.0 * static_cast<double>(index / 64));
    maximum_seeds.push_back(
        positioned_seed(static_cast<simulation::EntityId::Value>(index + 1), x, y));
  }
  auto maximum_world = simulation::GameWorld::create(std::move(maximum_seeds));
  // Step 16 bounds live physical bodies independently of entity/snapshot storage. Preserve the
  // complete historical entity population as bodyless controller identities beyond that limit;
  // do not add a snapshot-only constructor or bypass live admission to retain 4096 moving bodies.
  for (std::size_t index = simulation::kMaximumMotionBodyCount;
       index < simulation::kMaximumEntityCount; ++index) {
    maximum_world.mutable_store<simulation::PhysicsBody>().erase(
        simulation::EntityId::create(static_cast<simulation::EntityId::Value>(index + 1)));
  }
  const simulation::WorldSnapshot maximum_snapshot =
      simulation_from_world(std::move(maximum_world),
                            snapshot_configuration(130.0, 130.0, 0.25, 64, 64))
          .snapshot();

  CHECK(empty_snapshot.players().empty());
  CHECK(empty_snapshot.tick_sequence() == simulation::TickSequence::zero());
  REQUIRE(maximum_snapshot.entities().size() == simulation::kMaximumEntityCount);
  REQUIRE(maximum_snapshot.components<simulation::Controllable>().size() ==
          simulation::kMaximumEntityCount);
  REQUIRE(maximum_snapshot.components<simulation::PhysicsBody>().size() ==
          simulation::kMaximumMotionBodyCount);
  REQUIRE(maximum_snapshot.players().size() == simulation::kMaximumMotionBodyCount);
  CHECK(maximum_snapshot.players().front().entity_id().value() == simulation::kMinimumEntityId);
  CHECK(maximum_snapshot.players().back().entity_id().value() ==
        simulation::kMaximumMotionBodyCount);
  CHECK(maximum_snapshot.entities().back().value() == simulation::kMaximumEntityCount);
  CHECK(maximum_snapshot.tick_sequence() == simulation::TickSequence::zero());
}

TEST_CASE("WorldSnapshot remains unchanged after the owning simulation commits a later tick",
          "[unit][simulation][snapshot]") {
  simulation::GameSimulation game =
      simulation_from_world(simulation::GameWorld::create({seed_from_fixture(kLowerIdPlayer)}));
  const simulation::WorldSnapshot older_snapshot = game.snapshot();
  game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  const simulation::WorldSnapshot newer_snapshot = game.snapshot();

  REQUIRE(older_snapshot.players().size() == 1);
  REQUIRE(newer_snapshot.players().size() == 1);
  CHECK(older_snapshot.players()[0].position() ==
        simulation::Vector2::create(kLowerIdPlayer.position_x, kLowerIdPlayer.position_y));
  CHECK(older_snapshot.players()[0].velocity() ==
        simulation::Vector2::create(kLowerIdPlayer.velocity_x, kLowerIdPlayer.velocity_y));
  CHECK(newer_snapshot.tick_sequence().value() == 1);
  CHECK(newer_snapshot.players()[0].position() != older_snapshot.players()[0].position());
}

TEST_CASE("WorldSnapshot shares the actual map terrain across ticks and simulation moves",
          "[unit][simulation][snapshot][terrain]") {
  simulation::GameSimulation game = simulation::GameSimulation::create(
      snapshot_configuration(), simulation::GameWorld::create({}),
      simulation::GameSimulationSetup::engine_defaults().with_map(snapshot_terrain_map()));
  const simulation::TerrainDefinition* const terrain = &game.map().terrain();
  const simulation::WorldSnapshot initial = game.snapshot();

  simulation::GameSimulation moved_game = std::move(game);
  moved_game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  const simulation::WorldSnapshot next = moved_game.snapshot();

  CHECK(&initial.terrain() == terrain);
  CHECK(&next.terrain() == terrain);
  CHECK(&moved_game.map().terrain() == terrain);
  CHECK(initial.tick_sequence() == simulation::TickSequence::zero());
  CHECK(next.tick_sequence() == simulation::TickSequence::create(1));
}

TEST_CASE("WorldSnapshot retains authored terrain after the simulation is destroyed",
          "[unit][simulation][snapshot][terrain]") {
  const simulation::TerrainDefinition* original_terrain = nullptr;
  const simulation::WorldSnapshot retained = [&original_terrain] {
    const simulation::GameSimulation game = simulation::GameSimulation::create(
        snapshot_configuration(), simulation::GameWorld::create({}),
        simulation::GameSimulationSetup::engine_defaults().with_map(snapshot_terrain_map()));
    original_terrain = &game.map().terrain();
    return game.snapshot();
  }();

  CHECK(&retained.terrain() == original_terrain);
  const simulation::MapDefinition expected = snapshot_terrain_map();
  CHECK(retained.terrain() == expected.terrain());
  CHECK(retained.terrain().ground() == simulation::TerrainGround::kCorridors);
  REQUIRE(retained.terrain().holes().size() == 1);
  CHECK(retained.terrain().holes().front().radius() == kSnapshotHoleRadius);
}

TEST_CASE("WorldSnapshot equality compares authored terrain rather than shared owner identity",
          "[unit][simulation][snapshot][terrain]") {
  const auto snapshot_of = [](simulation::MapDefinition map) {
    return simulation::GameSimulation::create(
               snapshot_configuration(), simulation::GameWorld::create({}),
               simulation::GameSimulationSetup::engine_defaults().with_map(std::move(map)))
        .snapshot();
  };
  const simulation::WorldSnapshot first = snapshot_of(snapshot_terrain_map());
  const simulation::WorldSnapshot equal = snapshot_of(snapshot_terrain_map());
  const simulation::WorldSnapshot changed =
      snapshot_of(snapshot_terrain_map(kSnapshotHoleRadius + 1.0));

  CHECK(&first.terrain() != &equal.terrain());
  CHECK(first == equal);
  CHECK(first != changed);
}

TEST_CASE("PlayerSnapshot retains signed-zero canonicality for copied motion vectors",
          "[unit][simulation][snapshot]") {
  const simulation::Vector2 canonical_zero = simulation::Vector2::create(-0.0, -0.0);
  const simulation::GameWorld world =
      simulation::GameWorld::create({simulation::GameWorld::EntitySeed::create(
          simulation::EntityId::create(1),
          simulation::PhysicsBody::create(simulation::Vector2::create(1.0, 1.0), canonical_zero,
                                          canonical_zero))});
  const simulation::WorldSnapshot snapshot = simulation_from_world(world).snapshot();

  REQUIRE(snapshot.players().size() == 1);
  CHECK(snapshot.players()[0].position() == simulation::Vector2::create(1.0, 1.0));
  CHECK_FALSE(std::signbit(snapshot.players()[0].velocity().x()));
  CHECK_FALSE(std::signbit(snapshot.players()[0].velocity().y()));
  CHECK_FALSE(std::signbit(snapshot.players()[0].acceleration().x()));
  CHECK_FALSE(std::signbit(snapshot.players()[0].acceleration().y()));
}

TEST_CASE("WorldSnapshot publishes every registered component store in ascending EntityId order",
          "[unit][simulation][snapshot][component_registry]") {
  simulation::GameWorld world =
      simulation::GameWorld::create({positioned_seed(9, 9.0, 9.0), positioned_seed(2, 2.0, 2.0)});
  world.mutable_store<simulation::Score>().insert_or_assign(simulation::EntityId::create(9),
                                                            simulation::Score{-4});
  world.mutable_store<simulation::Team>().insert_or_assign(
      simulation::EntityId::create(2), simulation::Team{simulation::TeamId::create(6)});
  world.mutable_store<simulation::Lifetime>().insert_or_assign(simulation::EntityId::create(2),
                                                               simulation::Lifetime{12});
  const simulation::WorldSnapshot snapshot = simulation_from_world(std::move(world)).snapshot();

  REQUIRE(snapshot.entities().size() == 2);
  CHECK(snapshot.entities()[0].value() == 2);
  CHECK(snapshot.entities()[1].value() == 9);
  REQUIRE(snapshot.components<simulation::PhysicsBody>().size() == 2);
  CHECK(snapshot.components<simulation::PhysicsBody>()[0].entity.value() == 2);
  CHECK(snapshot.components<simulation::PhysicsBody>()[1].entity.value() == 9);
  REQUIRE(snapshot.components<simulation::Controllable>().size() == 2);
  CHECK(snapshot.components<simulation::Controllable>()[0].value.controller_id.value() == 2);
  REQUIRE(snapshot.components<simulation::Score>().size() == 1);
  CHECK(snapshot.components<simulation::Score>()[0].entity.value() == 9);
  CHECK(snapshot.components<simulation::Score>()[0].value.points == -4);
  REQUIRE(snapshot.components<simulation::Team>().size() == 1);
  CHECK(snapshot.components<simulation::Team>()[0].value.team_id.value() == 6);
  REQUIRE(snapshot.components<simulation::Lifetime>().size() == 1);
  CHECK(snapshot.components<simulation::Lifetime>()[0].value.ticks_remaining == 12);
}

TEST_CASE("WorldSnapshot players project only entities carrying a body and a controller link",
          "[unit][simulation][snapshot]") {
  simulation::GameWorld world = simulation::GameWorld::create(
      {positioned_seed(2, 2.0, 2.0), positioned_seed(5, 5.0, 5.0), positioned_seed(9, 9.0, 9.0)});
  world.mutable_store<simulation::Controllable>().erase(simulation::EntityId::create(5));
  const simulation::WorldSnapshot snapshot = simulation_from_world(std::move(world)).snapshot();

  REQUIRE(snapshot.components<simulation::PhysicsBody>().size() == 3);
  REQUIRE(snapshot.players().size() == 2);
  CHECK(snapshot.players()[0].entity_id().value() == 2);
  CHECK(snapshot.players()[1].entity_id().value() == 9);
}

TEST_CASE("WorldSnapshot equality covers the tick, the roster, and every component store",
          "[unit][simulation][snapshot][component_registry]") {
  const simulation::WorldSnapshot snapshot =
      simulation_from_world(simulation::GameWorld::create({positioned_seed(2, 2.0, 2.0)}))
          .snapshot();
  simulation::GameWorld scored_world =
      simulation::GameWorld::create({positioned_seed(2, 2.0, 2.0)});
  scored_world.mutable_store<simulation::Score>().insert_or_assign(simulation::EntityId::create(2),
                                                                   simulation::Score{1});
  const simulation::WorldSnapshot scored_snapshot =
      simulation_from_world(std::move(scored_world)).snapshot();

  CHECK(snapshot ==
        simulation_from_world(simulation::GameWorld::create({positioned_seed(2, 2.0, 2.0)}))
            .snapshot());
  CHECK(snapshot != scored_snapshot);
}

TEST_CASE("WorldSnapshot derives its roster from the stores it publishes",
          "[unit][simulation][snapshot][entity_roster]") {
  // The published roster and the published components are two readings of one value: the roster is
  // computed from the copies the snapshot just made, so an entity that publishes a component and a
  // roster that omits it is not a case this file has to get right -- it cannot be expressed.
  simulation::GameWorld world = simulation::GameWorld::create({positioned_seed(2, 2.0, 2.0)});
  world.mutable_store<simulation::Score>().insert_or_assign(simulation::EntityId::create(40),
                                                            simulation::Score{7});
  const simulation::WorldSnapshot snapshot = simulation_from_world(std::move(world)).snapshot();

  REQUIRE(snapshot.entities().size() == 2);
  CHECK(snapshot.entities()[0].value() == 2);
  CHECK(snapshot.entities()[1].value() == 40);
  REQUIRE(snapshot.components<simulation::Score>().size() == 1);
  CHECK(snapshot.components<simulation::Score>()[0].entity.value() == 40);
}

TEST_CASE("WorldSnapshot publishes the controller link and never the recorded commands",
          "[unit][simulation][snapshot][command][disclosure]") {
  // `Controllable::commands_this_tick` is tick-local: it is one entity's live input for the tick
  // being committed, and publishing it would hand every reader a player's input for the tick it is
  // rendering. The kind declares what it publishes, so this is stripped at the boundary rather
  // than remembered at each call site.
  simulation::GameWorld world = simulation::GameWorld::create({positioned_seed(2, 2.0, 2.0)});
  simulation::Controllable* controllable =
      world.mutable_store<simulation::Controllable>().mutable_find(simulation::EntityId::create(2));
  REQUIRE(controllable != nullptr);
  controllable->normalized_thrust_intent = simulation::Vector2::create(0.6, 0.8);
  controllable->commands_this_tick.push_back(simulation::Command{simulation::ThrustCommand{
      simulation::EntityId::create(2), simulation::Vector2::create(1.0, 0.0)}});
  const simulation::WorldSnapshot snapshot = simulation_from_world(std::move(world)).snapshot();

  REQUIRE(snapshot.components<simulation::Controllable>().size() == 1);
  CHECK(snapshot.components<simulation::Controllable>()[0].value.controller_id.value() == 2);
  CHECK(snapshot.components<simulation::Controllable>()[0].value.commands_this_tick.empty());
  CHECK_FALSE(snapshot.components<simulation::Controllable>()[0]
                  .value.normalized_thrust_intent.has_value());
}

TEST_CASE("WorldSnapshot carries the match section and every named stream draw count",
          "[unit][simulation][snapshot][match_state][deterministic_random]") {
  const simulation::WorldSnapshot snapshot =
      simulation_from_world(simulation::GameWorld::create({positioned_seed(2, 2.0, 2.0)}))
          .snapshot();

  CHECK(snapshot.match().mode_name() == simulation::GameSimulation::kEngineDefaultModeName);
  CHECK(snapshot.match().phase() == simulation::MatchPhase::kLobby);
  CHECK(snapshot.match().phase_started_tick() == simulation::TickSequence::zero());
  CHECK(snapshot.match().running_started_tick() == simulation::TickSequence::zero());
  CHECK_FALSE(snapshot.match().outcome().is_decided());
  CHECK(simulation::mode_match_state_schema_id_of(snapshot.match().mode_state()) == "none");
  // A tick that drew nothing publishes zero for every stream, so a run that diverged in how many
  // draws it took on either stream diverges visibly at the first differing tick.
  CHECK(snapshot.random_draw_counts() == simulation::RandomDrawCounts{});
}
