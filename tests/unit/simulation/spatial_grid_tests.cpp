#include "candidate_pair.hpp"
#include "cell_coord.hpp"
#include "component_store.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "physics_body.hpp"
#include "simulation_config.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "spatial_grid.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::SimulationConfig
grid_configuration(const double world_width, const double world_height, const double player_radius,
                   const std::uint64_t column_count, const std::uint64_t row_count) {
  return simulation::SimulationConfig::create(world_width, world_height, player_radius,
                                              simulation::SimulationConfig::kRequiredTicksPerSecond,
                                              column_count, row_count);
}

[[nodiscard]] simulation::GameWorld::EntitySeed
stationary_player(const simulation::EntityId::Value id, const double x, const double y) {
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  return simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(id),
      simulation::PhysicsBody::create(simulation::Vector2::create(x, y), zero, zero));
}

[[nodiscard]] std::vector<simulation::EntityId::Value>
member_values(const std::span<const simulation::EntityId> members) {
  std::vector<simulation::EntityId::Value> values;
  values.reserve(members.size());
  for (const simulation::EntityId id : members) {
    values.push_back(id.value());
  }
  return values;
}

using PairValues = std::pair<simulation::EntityId::Value, simulation::EntityId::Value>;

[[nodiscard]] std::vector<PairValues>
pair_values(const std::span<const simulation::CandidatePair> pairs) {
  std::vector<PairValues> values;
  values.reserve(pairs.size());
  for (const simulation::CandidatePair& pair : pairs) {
    values.emplace_back(pair.lower_id().value(), pair.higher_id().value());
  }
  return values;
}

[[nodiscard]] bool contains_pair(const std::span<const simulation::CandidatePair> pairs,
                                 const simulation::EntityId first,
                                 const simulation::EntityId second) {
  const simulation::CandidatePair expected = simulation::CandidatePair::create(first, second);
  return std::binary_search(pairs.begin(), pairs.end(), expected);
}

} // namespace

TEST_CASE("CellCoord comparison is canonical row-major order", "[unit][simulation][spatial_grid]") {
  const simulation::CellCoord first_row_last_column = simulation::CellCoord::create(0, 7);
  const simulation::CellCoord second_row_first_column = simulation::CellCoord::create(1, 0);
  const simulation::CellCoord second_row_second_column = simulation::CellCoord::create(1, 1);

  CHECK(first_row_last_column < second_row_first_column);
  CHECK(second_row_first_column < second_row_second_column);
}

TEST_CASE("CandidatePair canonicalizes ID order and rejects a repeated ID",
          "[unit][simulation][spatial_grid]") {
  const simulation::CandidatePair pair = simulation::CandidatePair::create(
      simulation::EntityId::create(9), simulation::EntityId::create(2));

  CHECK(pair.lower_id().value() == 2);
  CHECK(pair.higher_id().value() == 9);
  CHECK_THROWS_AS(simulation::CandidatePair::create(simulation::EntityId::create(4),
                                                    simulation::EntityId::create(4)),
                  simulation::SimulationValidationError);
}

TEST_CASE("SpatialGrid home cells use positive internal sides and closed outer maxima",
          "[unit][simulation][spatial_grid]") {
  const simulation::SpatialGrid grid = simulation::SpatialGrid::create(
      grid_configuration(100.0, 80.0, 5.0, 4, 4), simulation::GameWorld::create({}));

  CHECK(grid.home_cell(simulation::Vector2::create(0.0, 0.0)) ==
        simulation::CellCoord::create(0, 0));
  CHECK(grid.home_cell(simulation::Vector2::create(25.0, 20.0)) ==
        simulation::CellCoord::create(1, 1));
  CHECK(grid.home_cell(simulation::Vector2::create(100.0, 80.0)) ==
        simulation::CellCoord::create(3, 3));
  CHECK_THROWS_AS(grid.home_cell(simulation::Vector2::create(-0.01, 10.0)),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(grid.cell_members(simulation::CellCoord::create(4, 0)),
                  simulation::SimulationValidationError);
}

TEST_CASE("SpatialGrid includes exact internal AABB edges and corners on both sides",
          "[unit][simulation][spatial_grid]") {
  const simulation::GameWorld world = simulation::GameWorld::create(
      {stationary_player(9, 30.0, 30.0), stationary_player(2, 20.0, 20.0)});
  const simulation::SpatialGrid grid =
      simulation::SpatialGrid::create(grid_configuration(100.0, 100.0, 5.0, 4, 4), world);
  const std::vector<simulation::EntityId::Value> expected_ids{2, 9};

  CHECK(member_values(grid.cell_members(simulation::CellCoord::create(0, 0))) == expected_ids);
  CHECK(member_values(grid.cell_members(simulation::CellCoord::create(0, 1))) == expected_ids);
  CHECK(member_values(grid.cell_members(simulation::CellCoord::create(1, 0))) == expected_ids);
  CHECK(member_values(grid.cell_members(simulation::CellCoord::create(1, 1))) == expected_ids);
  CHECK(pair_values(grid.candidate_pairs()) == std::vector<PairValues>{{2, 9}});
}

TEST_CASE("SpatialGrid keeps outer-corner discs in the first and last cells",
          "[unit][simulation][spatial_grid]") {
  const simulation::GameWorld world = simulation::GameWorld::create(
      {stationary_player(8, 95.0, 95.0), stationary_player(3, 5.0, 5.0)});
  const simulation::SpatialGrid grid =
      simulation::SpatialGrid::create(grid_configuration(100.0, 100.0, 5.0, 4, 4), world);

  CHECK(member_values(grid.cell_members(simulation::CellCoord::create(0, 0))) ==
        std::vector<simulation::EntityId::Value>{3});
  CHECK(member_values(grid.cell_members(simulation::CellCoord::create(3, 3))) ==
        std::vector<simulation::EntityId::Value>{8});
}

TEST_CASE("SpatialGrid preserves closed-boundary coverage for non-divisible dimensions",
          "[unit][simulation][spatial_grid]") {
  constexpr double world_width = 10.0;
  constexpr double world_height = 7.0;
  constexpr double radius = 0.5;
  const double column_boundary = world_width / 3.0;
  const double row_boundary = world_height / 2.0;
  const simulation::GameWorld world = simulation::GameWorld::create(
      {stationary_player(11, column_boundary + radius, row_boundary + radius)});
  const simulation::SpatialGrid grid = simulation::SpatialGrid::create(
      grid_configuration(world_width, world_height, radius, 3, 2), world);

  CHECK(member_values(grid.cell_members(simulation::CellCoord::create(0, 0))) ==
        std::vector<simulation::EntityId::Value>{11});
  CHECK(member_values(grid.cell_members(simulation::CellCoord::create(0, 1))) ==
        std::vector<simulation::EntityId::Value>{11});
  CHECK(member_values(grid.cell_members(simulation::CellCoord::create(1, 0))) ==
        std::vector<simulation::EntityId::Value>{11});
  CHECK(member_values(grid.cell_members(simulation::CellCoord::create(1, 1))) ==
        std::vector<simulation::EntityId::Value>{11});
}

TEST_CASE("SpatialGrid candidates include every touching neighboring-cell pair",
          "[unit][simulation][spatial_grid]") {
  constexpr double radius = 5.0;
  const simulation::GameWorld world = simulation::GameWorld::create(
      {stationary_player(40, 80.0, 80.0), stationary_player(30, 25.0, 25.0),
       stationary_player(10, 15.0, 15.0), stationary_player(20, 25.0, 15.0)});
  const simulation::SpatialGrid grid =
      simulation::SpatialGrid::create(grid_configuration(100.0, 100.0, radius, 10, 10), world);

  CHECK(grid.home_cell(simulation::Vector2::create(15.0, 15.0)) ==
        simulation::CellCoord::create(1, 1));
  CHECK(grid.home_cell(simulation::Vector2::create(25.0, 15.0)) ==
        simulation::CellCoord::create(1, 2));
  std::size_t contact_count = 0;
  const std::span<const simulation::ComponentStore<simulation::PhysicsBody>::Entry> bodies =
      world.store<simulation::PhysicsBody>().entries();
  for (std::size_t left_index = 0; left_index < bodies.size(); ++left_index) {
    for (std::size_t right_index = left_index + 1; right_index < bodies.size(); ++right_index) {
      const simulation::ComponentStore<simulation::PhysicsBody>::Entry& left = bodies[left_index];
      const simulation::ComponentStore<simulation::PhysicsBody>::Entry& right = bodies[right_index];
      const double x_distance = right.value.position().x() - left.value.position().x();
      const double y_distance = right.value.position().y() - left.value.position().y();
      if (std::hypot(x_distance, y_distance) <= 2.0 * radius) {
        ++contact_count;
        CHECK(contains_pair(grid.candidate_pairs(), left.entity, right.entity));
      }
    }
  }
  CHECK(contact_count == 2);
}

TEST_CASE("SpatialGrid preserves tolerance-level contacts across an exact cell boundary",
          "[unit][simulation][spatial_grid][tolerance]") {
  constexpr double radius = 10.0;
  constexpr double boundary = 100.0;
  constexpr double offset = simulation::kPositionTolerance / 4.0;
  const simulation::EntityId first_id = simulation::EntityId::create(1);
  const simulation::EntityId second_id = simulation::EntityId::create(2);
  const simulation::GameWorld world = simulation::GameWorld::create(
      {stationary_player(first_id.value(), 250.0, boundary - radius - offset),
       stationary_player(second_id.value(), 250.0, boundary + radius + offset)});

  const simulation::SpatialGrid grid =
      simulation::SpatialGrid::create(grid_configuration(500.0, 500.0, radius, 10, 10), world);

  CHECK(contains_pair(grid.candidate_pairs(), first_id, second_id));
}

TEST_CASE("SpatialGrid includes relative-tolerance contacts at large-scale boundaries",
          "[unit][simulation][spatial_grid][tolerance]") {
  constexpr double world_extent = 1'000'000'000.0;
  constexpr double radius = 100'000'000.0;
  constexpr double boundary = world_extent / 2.0;
  constexpr double offset = 0.00005;
  const simulation::EntityId first_id = simulation::EntityId::create(1);
  const simulation::EntityId second_id = simulation::EntityId::create(2);
  const simulation::GameWorld world = simulation::GameWorld::create(
      {stationary_player(first_id.value(), boundary, boundary - radius - offset),
       stationary_player(second_id.value(), boundary, boundary + radius + offset)});

  const simulation::SpatialGrid grid = simulation::SpatialGrid::create(
      grid_configuration(world_extent, world_extent, radius, 2, 2), world);

  CHECK(contains_pair(grid.candidate_pairs(), first_id, second_id));
}

TEST_CASE("SpatialGrid candidate order and membership are stable across shuffled construction",
          "[unit][simulation][spatial_grid]") {
  const simulation::SimulationConfig configuration = grid_configuration(100.0, 100.0, 10.0, 2, 2);
  const simulation::GameWorld first_world = simulation::GameWorld::create(
      {stationary_player(4, 40.0, 40.0), stationary_player(1, 43.0, 43.0),
       stationary_player(3, 41.0, 41.0), stationary_player(2, 42.0, 42.0)});
  const simulation::GameWorld second_world = simulation::GameWorld::create(
      {stationary_player(2, 42.0, 42.0), stationary_player(3, 41.0, 41.0),
       stationary_player(1, 43.0, 43.0), stationary_player(4, 40.0, 40.0)});
  const simulation::SpatialGrid first_grid =
      simulation::SpatialGrid::create(configuration, first_world);
  const simulation::SpatialGrid second_grid =
      simulation::SpatialGrid::create(configuration, second_world);
  const std::vector<PairValues> expected_pairs{{1, 2}, {1, 3}, {1, 4}, {2, 3}, {2, 4}, {3, 4}};

  CHECK(pair_values(first_grid.candidate_pairs()) == expected_pairs);
  CHECK(pair_values(second_grid.candidate_pairs()) == expected_pairs);
  CHECK(std::adjacent_find(first_grid.candidate_pairs().begin(),
                           first_grid.candidate_pairs().end()) ==
        first_grid.candidate_pairs().end());
  CHECK(member_values(first_grid.cell_members(simulation::CellCoord::create(0, 0))) ==
        std::vector<simulation::EntityId::Value>{1, 2, 3, 4});
}

TEST_CASE("SpatialGrid rejects player centers that cannot contain the configured disc",
          "[unit][simulation][spatial_grid]") {
  const simulation::SimulationConfig configuration = grid_configuration(100.0, 100.0, 5.0, 4, 4);

  CHECK_THROWS_AS(
      simulation::SpatialGrid::create(
          configuration, simulation::GameWorld::create({stationary_player(1, 4.99, 50.0)})),
      simulation::SimulationValidationError);
  CHECK_THROWS_AS(
      simulation::SpatialGrid::create(
          configuration, simulation::GameWorld::create({stationary_player(1, 50.0, 95.01)})),
      simulation::SimulationValidationError);
}

TEST_CASE("SpatialGrid rebuild returns complete replacement membership",
          "[unit][simulation][spatial_grid]") {
  const simulation::SimulationConfig configuration = grid_configuration(100.0, 100.0, 5.0, 2, 2);
  const simulation::SpatialGrid original = simulation::SpatialGrid::create(
      configuration, simulation::GameWorld::create({stationary_player(1, 10.0, 10.0)}));
  const simulation::SpatialGrid rebuilt =
      original.rebuilt(simulation::GameWorld::create({stationary_player(2, 90.0, 90.0)}));

  CHECK(member_values(original.cell_members(simulation::CellCoord::create(0, 0))) ==
        std::vector<simulation::EntityId::Value>{1});
  CHECK(rebuilt.cell_members(simulation::CellCoord::create(0, 0)).empty());
  CHECK(member_values(rebuilt.cell_members(simulation::CellCoord::create(1, 1))) ==
        std::vector<simulation::EntityId::Value>{2});
}

TEST_CASE("SpatialGrid rejects unsafe total cell membership before cell allocation",
          "[unit][simulation][spatial_grid]") {
  const simulation::SimulationConfig configuration =
      grid_configuration(100.0, 100.0, 49.9, 1'000, 1'000);
  std::vector<simulation::GameWorld::EntitySeed> players;
  players.reserve(17);
  for (simulation::EntityId::Value id = 1; id <= 17; ++id) {
    players.push_back(stationary_player(id, 50.0, 50.0));
  }

  CHECK_THROWS_AS(simulation::SpatialGrid::create(
                      configuration, simulation::GameWorld::create(std::move(players))),
                  simulation::SimulationValidationError);
}

TEST_CASE("SimulationConfig rejects a derived cell extent that cannot be represented",
          "[unit][simulation][spatial_grid][configuration]") {
  const double minimum = std::numeric_limits<double>::denorm_min();

  CHECK_THROWS_AS(grid_configuration(minimum * 4.0, minimum * 4.0, minimum, 1'000, 1'000),
                  simulation::SimulationValidationError);
}

TEST_CASE("SpatialGrid rejects unsafe duplicate candidate traversal before pair allocation",
          "[unit][simulation][spatial_grid]") {
  const simulation::SimulationConfig configuration = grid_configuration(100.0, 100.0, 49.9, 3, 3);
  std::vector<simulation::GameWorld::EntitySeed> players;
  players.reserve(simulation::kMaximumPlayerCount);
  for (std::size_t index = 0; index < simulation::kMaximumPlayerCount; ++index) {
    players.push_back(
        stationary_player(static_cast<simulation::EntityId::Value>(index + 1), 50.0, 50.0));
  }

  CHECK_THROWS_AS(simulation::SpatialGrid::create(
                      configuration, simulation::GameWorld::create(std::move(players))),
                  simulation::SimulationValidationError);
}

namespace {

[[nodiscard]] simulation::GameWorld::EntitySeed static_body(const simulation::EntityId::Value id,
                                                            const double x, const double y) {
  return simulation::GameWorld::EntitySeed::create_static(
      simulation::EntityId::create(id),
      simulation::PhysicsBody::create_static(simulation::Vector2::create(x, y)));
}

} // namespace

TEST_CASE("the grid partitions the map's arena rather than the configuration's world size",
          "[unit][simulation][spatial_grid][map_definition]") {
  // The configuration keeps publishing 500x500 for protocol v1 while the map declares the arena
  // the kernel uses, so the cells this grid partitions come from the map.
  const simulation::GameWorld world =
      simulation::GameWorld::create({stationary_player(1, 5.0, 5.0)});
  const simulation::SpatialGrid grid =
      simulation::SpatialGrid::create(grid_configuration(500.0, 500.0, 1.0, 4, 4),
                                      simulation::ArenaBounds::create(100.0, 100.0), world);

  CHECK(grid.bounds() == simulation::ArenaBounds::create(100.0, 100.0));
  CHECK(grid.cell_width() == 25.0);
  CHECK(grid.cell_height() == 25.0);
  CHECK(grid.home_cell(simulation::Vector2::create(100.0, 100.0)) ==
        simulation::CellCoord::create(3, 3));
  CHECK_THROWS_AS(grid.home_cell(simulation::Vector2::create(120.0, 50.0)),
                  simulation::SimulationValidationError);
}

TEST_CASE("the two-argument grid synthesizes its arena from the configuration",
          "[unit][simulation][spatial_grid][map_definition]") {
  // Every caller written before maps existed lands here, which is why no accepted fixture had to
  // change: the synthesized arena is exactly the configured world rectangle.
  const simulation::GameWorld world =
      simulation::GameWorld::create({stationary_player(1, 5.0, 5.0)});
  const simulation::SimulationConfig configuration = grid_configuration(100.0, 80.0, 1.0, 4, 4);

  CHECK(simulation::SpatialGrid::create(configuration, world) ==
        simulation::SpatialGrid::create(configuration, simulation::ArenaBounds::create(100.0, 80.0),
                                        world));
}

TEST_CASE("a static body is indexed and may sit outside the disc-centre interval",
          "[unit][simulation][spatial_grid][static_body]") {
  // `reflect_static` can only see a wall the broad phase offered it, so a static body has to be a
  // member; and a wall on the arena edge is legal content, so it is exempt from the interval a
  // moving disc is held inside.
  const simulation::ArenaBounds bounds = simulation::ArenaBounds::create(100.0, 100.0);
  const simulation::GameWorld world =
      simulation::GameWorld::create({stationary_player(1, 14.0, 12.0), static_body(2, 2.0, 12.0)});

  const simulation::SpatialGrid grid =
      simulation::SpatialGrid::create(grid_configuration(100.0, 100.0, 5.0, 4, 4), bounds, world);

  REQUIRE(bounds.contains(simulation::Vector2::create(2.0, 12.0)));
  REQUIRE_FALSE(bounds.contains_disc_center(simulation::Vector2::create(2.0, 12.0), 5.0));
  CHECK(pair_values(grid.candidate_pairs()) == std::vector<PairValues>{{1, 2}});
}

TEST_CASE("a static body centre outside the arena rectangle is rejected",
          "[unit][simulation][spatial_grid][static_body][validation]") {
  const simulation::GameWorld world = simulation::GameWorld::create({static_body(1, -1.0, 12.0)});

  CHECK_THROWS_AS(simulation::SpatialGrid::create(grid_configuration(100.0, 100.0, 5.0, 4, 4),
                                                  simulation::ArenaBounds::create(100.0, 100.0),
                                                  world),
                  simulation::SimulationValidationError);
}
