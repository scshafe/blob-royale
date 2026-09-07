#include "entity_id.hpp"
#include "entity_id_allocator.hpp"
#include "entity_id_reservation.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "physics_body.hpp"
#include "runtime_limits.hpp"
#include "simulation_config.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace runtime = blob_royale::runtime;
namespace simulation = blob_royale::simulation;

namespace {

constexpr double kWorldWidth = 100.0;
constexpr double kWorldHeight = 80.0;
constexpr double kPlayerRadius = 1.0;
constexpr std::uint64_t kTickRate = simulation::SimulationConfig::kRequiredTicksPerSecond;
constexpr std::uint64_t kGridColumns = 10;
constexpr std::uint64_t kGridRows = 8;
constexpr std::uint64_t kStaticBodyCount = 3;
constexpr std::uint64_t kSeededHighEntityId = 9;
constexpr std::uint64_t kSeededLowEntityId = 3;
constexpr std::uint64_t kWorldSeed = 20260907;

[[nodiscard]] simulation::SimulationConfig simulation_config_fixture() {
  return simulation::SimulationConfig::create(kWorldWidth, kWorldHeight, kPlayerRadius, kTickRate,
                                              kGridColumns, kGridRows);
}

[[nodiscard]] simulation::MapDefinition map_with_static_bodies_fixture() {
  std::vector<simulation::PhysicsBody> static_bodies;
  static_bodies.reserve(kStaticBodyCount);
  for (std::uint64_t index = 0; index < kStaticBodyCount; ++index) {
    static_bodies.push_back(simulation::PhysicsBody::create_static(
        simulation::Vector2::create(10.0 + static_cast<double>(index) * 10.0, 10.0)));
  }
  return simulation::MapDefinition::create(
      "entity_id_allocator_floor_map", simulation::ArenaBounds::create(kWorldWidth, kWorldHeight),
      std::move(static_bodies), {}, simulation::MapMetadata::none());
}

[[nodiscard]] simulation::GameWorld::EntitySeed player_fixture(const std::uint64_t entity_id) {
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  return simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(entity_id),
      simulation::PhysicsBody::create(simulation::Vector2::create(20.0, 20.0), zero, zero));
}

} // namespace

TEST_CASE("EntityIdAllocator opens at the named id and issues from it",
          "[unit][runtime][entity_id_allocator]") {
  runtime::EntityIdAllocator allocator =
      runtime::EntityIdAllocator::create(simulation::EntityId::create(42));

  REQUIRE(allocator.next_entity_id().value() == 42);
  REQUIRE(allocator.reserved_entity_id_count() == 0);
}

TEST_CASE("EntityIdAllocator hands every tick a non-empty block",
          "[unit][runtime][entity_id_allocator]") {
  runtime::EntityIdAllocator allocator = runtime::EntityIdAllocator::create(
      simulation::EntityId::create(simulation::kMinimumEntityId));

  const simulation::EntityIdReservation reservation = allocator.reserve_for_tick(0);

  // Royale creates its zone entity from the tick's reservation on its first running tick, so a
  // spawn-free tick still needs an id to draw and never receives EntityIdReservation::none().
  REQUIRE_FALSE(reservation.empty());
  REQUIRE_FALSE(reservation == simulation::EntityIdReservation::none());
  REQUIRE(reservation.count() == simulation::kSystemCreatedEntityHeadroom);
}

TEST_CASE("EntityIdAllocator reserves the spawn count plus the system headroom",
          "[unit][runtime][entity_id_allocator]") {
  runtime::EntityIdAllocator allocator =
      runtime::EntityIdAllocator::create(simulation::EntityId::create(100));

  const simulation::EntityIdReservation reservation = allocator.reserve_for_tick(4);

  REQUIRE(reservation.first_entity_id().value() == 100);
  REQUIRE(reservation.count() == 4 + simulation::kSystemCreatedEntityHeadroom);
  REQUIRE(allocator.next_entity_id().value() == 100 + 4 + simulation::kSystemCreatedEntityHeadroom);
}

TEST_CASE("EntityIdAllocator issues monotonic non-overlapping blocks across ticks",
          "[unit][runtime][entity_id_allocator]") {
  runtime::EntityIdAllocator allocator = runtime::EntityIdAllocator::create(
      simulation::EntityId::create(simulation::kMinimumEntityId));

  const simulation::EntityIdReservation first = allocator.reserve_for_tick(2);
  const simulation::EntityIdReservation second = allocator.reserve_for_tick(0);
  const simulation::EntityIdReservation third = allocator.reserve_for_tick(1);

  REQUIRE(first.first_entity_id().value() == simulation::kMinimumEntityId);
  REQUIRE(second.first_entity_id().value() == first.first_entity_id().value() + first.count());
  REQUIRE(third.first_entity_id().value() == second.first_entity_id().value() + second.count());
  REQUIRE_FALSE(second.contains(first.first_entity_id()));
  REQUIRE_FALSE(third.contains(second.first_entity_id()));
  REQUIRE(allocator.reserved_entity_id_count() == first.count() + second.count() + third.count());
}

TEST_CASE("EntityIdAllocator opens above the map's whole static-body block",
          "[unit][runtime][entity_id_allocator]") {
  const simulation::MapDefinition map = map_with_static_bodies_fixture();
  // The world seats nothing, so only the map can raise the floor; without the map rule the cursor
  // would open at kMinimumEntityId and the first spawn would overwrite the first wall.
  simulation::GameSimulation game_simulation = simulation::GameSimulation::create(
      simulation_config_fixture(), simulation::GameWorld::create({}),
      simulation::GameSimulationSetup::engine_defaults().with_map(map));

  runtime::EntityIdAllocator allocator =
      runtime::EntityIdAllocator::above_committed_state(game_simulation);

  REQUIRE(allocator.next_entity_id().value() == simulation::kMinimumEntityId + kStaticBodyCount);
}

TEST_CASE("EntityIdAllocator opens above every id a map already seated",
          "[unit][runtime][entity_id_allocator]") {
  const simulation::SimulationConfig configuration = simulation_config_fixture();
  simulation::MapDefinition map = map_with_static_bodies_fixture();
  simulation::GameWorld world = simulation::GameWorld::create(configuration, map, kWorldSeed);
  simulation::GameSimulation game_simulation = simulation::GameSimulation::create(
      configuration, std::move(world),
      simulation::GameSimulationSetup::engine_defaults().with_map(map));

  runtime::EntityIdAllocator allocator =
      runtime::EntityIdAllocator::above_committed_state(game_simulation);

  const simulation::WorldSnapshot committed = game_simulation.snapshot();
  REQUIRE(allocator.next_entity_id().value() == simulation::kMinimumEntityId + kStaticBodyCount);
  REQUIRE_FALSE(committed.entities().empty());
  REQUIRE(committed.entities().back().value() < allocator.next_entity_id().value());
}

TEST_CASE("EntityIdAllocator opens above a scenario-seeded roster",
          "[unit][runtime][entity_id_allocator]") {
  // A scenario CSV names its own ids, so the committed roster and not the map is the higher floor.
  simulation::GameSimulation game_simulation = simulation::GameSimulation::create(
      simulation_config_fixture(),
      simulation::GameWorld::create(
          {player_fixture(kSeededHighEntityId), player_fixture(kSeededLowEntityId)}));

  runtime::EntityIdAllocator allocator =
      runtime::EntityIdAllocator::above_committed_state(game_simulation);

  REQUIRE(allocator.next_entity_id().value() == kSeededHighEntityId + 1);
}

TEST_CASE("EntityIdAllocator fails hard rather than issuing an id past the maximum",
          "[unit][runtime][entity_id_allocator]") {
  runtime::EntityIdAllocator allocator = runtime::EntityIdAllocator::create(
      simulation::EntityId::create(simulation::kMaximumEntityId));

  REQUIRE_THROWS_AS(allocator.reserve_for_tick(1), simulation::SimulationValidationError);

  // The refused call left the cursor untouched, so the last legal block is still issuable.
  const simulation::EntityIdReservation last = allocator.reserve_for_tick(0);
  REQUIRE(last.first_entity_id().value() == simulation::kMaximumEntityId);
  REQUIRE_THROWS_AS(allocator.reserve_for_tick(0), simulation::SimulationValidationError);
}

TEST_CASE("EntityIdAllocator refuses a block wider than the world's seat count",
          "[unit][runtime][entity_id_allocator]") {
  runtime::EntityIdAllocator allocator = runtime::EntityIdAllocator::create(
      simulation::EntityId::create(simulation::kMinimumEntityId));

  REQUIRE_THROWS_AS(allocator.reserve_for_tick(simulation::kMaximumEntityIdReservationCount),
                    simulation::SimulationValidationError);
}
