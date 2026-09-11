#include "arena_bounds.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "simulation_config.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "spawn_system.hpp"
#include "terrain_definition.hpp"
#include "terrain_queries.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

simulation::Vector2 point(double x, double y) { return simulation::Vector2::create(x, y); }
simulation::ArenaBounds bounds() { return simulation::ArenaBounds::create(100.0, 100.0); }
simulation::TerrainCorridor road(std::string name = "road") {
  return simulation::TerrainCorridor::create(std::move(name), 10.0,
                                             {point(20.0, 50.0), point(80.0, 50.0)});
}

template <class Factory>
void require_rejected(Factory&& factory, simulation::SimulationValidationCode code) {
  try {
    static_cast<void>(std::forward<Factory>(factory)());
    FAIL("invalid terrain was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() == code);
    CHECK(error.code().starts_with("SIMULATION.TERRAIN_"));
  }
}

} // namespace

TEST_CASE("terrain is immutable shared content with authored value equality",
          "[unit][simulation][terrain][definition]") {
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<simulation::TerrainDefinition>);
  const auto first = simulation::TerrainDefinition::create(
      bounds(), simulation::TerrainGround::kCorridors, {road()},
      {simulation::TerrainHole::create("pit", point(50.0, 50.0), 2.0)});
  const auto independent = simulation::TerrainDefinition::create(
      bounds(), simulation::TerrainGround::kCorridors, {road()},
      {simulation::TerrainHole::create("pit", point(50.0, 50.0), 2.0)});
  auto assigned = simulation::TerrainDefinition::solid(bounds());
  assigned = first;
  const auto copy = first;
  CHECK(first == independent);
  CHECK(first == copy);
  CHECK(first == assigned);
  CHECK(&first.bounds() == &copy.bounds());
  CHECK(first.corridors().data() == assigned.corridors().data());
  REQUIRE(first.find_corridor("road") != nullptr);
  CHECK(first.find_corridor("road")->half_width() == 10.0);
  CHECK(first.find_corridor("missing") == nullptr);
}

TEST_CASE("a map owns one terrain envelope and programmatic rectangles explicitly mean solid",
          "[unit][simulation][terrain][map_definition]") {
  const auto map =
      simulation::MapDefinition::create("solid", bounds(), {}, {}, simulation::MapMetadata::none());
  CHECK(map.terrain().ground() == simulation::TerrainGround::kSolid);
  CHECK(&map.bounds() == &map.terrain().bounds());
  CHECK(map.terrain().corridors().empty());
  CHECK(map.terrain().holes().empty());
  CHECK(simulation::terrain_supports_point(map.terrain(), point(0.0, 100.0)));
}

TEST_CASE("promoted arena bounds preserve the pre-terrain arithmetic exactly",
          "[unit][simulation][terrain][promotion]") {
  const auto area = simulation::ArenaBounds::create(100.0, 80.0);
  // Frozen pre-promotion expressions from map_definition.cpp at 7c1bec7. These are the old
  // oracle, not production alternatives; the value now lives in arena_bounds.cpp unchanged.
  for (const double x : {-1.0, 0.0, 0.5, 10.0, 50.0, 90.0, 100.0, 101.0}) {
    for (const double y : {-1.0, 0.0, 0.5, 10.0, 40.0, 70.0, 80.0, 81.0}) {
      const auto p = point(x, y);
      const bool previous_point = p.x() >= 0.0 && p.x() <= 100.0 && p.y() >= 0.0 && p.y() <= 80.0;
      CHECK(area.contains(p) == previous_point);
      for (const double radius : {0.0, 0.5, 10.0, 40.0, 50.0}) {
        const bool previous_disc =
            p.x() >= radius && p.x() <= 100.0 - radius && p.y() >= radius && p.y() <= 80.0 - radius;
        CHECK(area.contains_disc_center(p, radius) == previous_disc);
      }
    }
  }
}

TEST_CASE("terrain shape dimensions and names fail with named validation errors",
          "[unit][simulation][terrain][validation]") {
  for (const double invalid : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN(),
                               std::nextafter(simulation::kMaximumWorldDimension,
                                              std::numeric_limits<double>::infinity())}) {
    require_rejected(
        [&] {
          return simulation::TerrainCorridor::create("road", invalid,
                                                     {point(10.0, 50.0), point(90.0, 50.0)});
        },
        simulation::SimulationValidationCode::kTerrainDefinitionInvalid);
    require_rejected(
        [&] { return simulation::TerrainHole::create("pit", point(50.0, 50.0), invalid); },
        simulation::SimulationValidationCode::kTerrainDefinitionInvalid);
  }
  for (const std::string& invalid : {std::string{}, std::string{"Road"}, std::string{"road-name"},
                                     std::string(simulation::kMaximumKindNameLength + 1, 'a')}) {
    require_rejected([&] { return road(invalid); },
                     simulation::SimulationValidationCode::kTerrainDefinitionInvalid);
    require_rejected(
        [&] { return simulation::TerrainHole::create(invalid, point(50.0, 50.0), 2.0); },
        simulation::SimulationValidationCode::kTerrainDefinitionInvalid);
  }
}

TEST_CASE("terrain corridors reject absent degenerate and numerically lost segments",
          "[unit][simulation][terrain][validation]") {
  for (const auto& points :
       {std::vector<simulation::Vector2>{}, std::vector<simulation::Vector2>{point(10.0, 10.0)},
        std::vector<simulation::Vector2>{point(10.0, 10.0), point(10.0, 10.0)}}) {
    require_rejected([&] { return simulation::TerrainCorridor::create("road", 1.0, points); },
                     simulation::SimulationValidationCode::kTerrainDefinitionInvalid);
  }
  require_rejected(
      [] {
        return simulation::TerrainCorridor::create("road", 1.0,
                                                   {point(0.0, 0.0), point(1e-200, 0.0)});
      },
      simulation::SimulationValidationCode::kTerrainGeometryPrecisionLost);
}

TEST_CASE("terrain rejects ambiguous ground duplicate names and out of envelope authoring",
          "[unit][simulation][terrain][validation]") {
  for (const auto ground :
       {simulation::TerrainGround::kSolid, static_cast<simulation::TerrainGround>(99)}) {
    require_rejected(
        [&] { return simulation::TerrainDefinition::create(bounds(), ground, {road()}, {}); },
        simulation::SimulationValidationCode::kTerrainDefinitionInvalid);
  }
  require_rejected(
      [] {
        return simulation::TerrainDefinition::create(bounds(),
                                                     simulation::TerrainGround::kCorridors, {}, {});
      },
      simulation::SimulationValidationCode::kTerrainDefinitionInvalid);
  require_rejected(
      [] {
        return simulation::TerrainDefinition::create(
            bounds(), simulation::TerrainGround::kCorridors, {road(), road()}, {});
      },
      simulation::SimulationValidationCode::kTerrainDefinitionInvalid);
  const auto pit = simulation::TerrainHole::create("pit", point(50.0, 50.0), 2.0);
  require_rejected(
      [&] {
        return simulation::TerrainDefinition::create(bounds(), simulation::TerrainGround::kSolid,
                                                     {}, {pit, pit});
      },
      simulation::SimulationValidationCode::kTerrainDefinitionInvalid);
  require_rejected(
      [] {
        return simulation::TerrainDefinition::create(
            bounds(), simulation::TerrainGround::kSolid, {},
            {simulation::TerrainHole::create("pit", point(101.0, 50.0), 2.0)});
      },
      simulation::SimulationValidationCode::kTerrainGeometryOutOfBounds);
  require_rejected(
      [] {
        return simulation::TerrainDefinition::create(
            bounds(), simulation::TerrainGround::kCorridors,
            {simulation::TerrainCorridor::create("road", 1.0,
                                                 {point(-1.0, 50.0), point(50.0, 50.0)})},
            {});
      },
      simulation::SimulationValidationCode::kTerrainGeometryOutOfBounds);
}

TEST_CASE("terrain aggregate shape and point limits reject before boundary compilation",
          "[unit][simulation][terrain][limits]") {
  std::vector<simulation::TerrainCorridor> roads;
  for (std::size_t index = 0; index <= simulation::kMaximumTerrainCorridorCount; ++index) {
    roads.push_back(road("road_" + std::to_string(index)));
  }
  require_rejected(
      [&] {
        return simulation::TerrainDefinition::create(
            bounds(), simulation::TerrainGround::kCorridors, roads, {});
      },
      simulation::SimulationValidationCode::kTerrainShapeLimitExceeded);
  std::vector<simulation::TerrainHole> holes;
  for (std::size_t index = 0; index <= simulation::kMaximumTerrainHoleCount; ++index) {
    holes.push_back(
        simulation::TerrainHole::create("pit_" + std::to_string(index), point(50.0, 50.0), 2.0));
  }
  require_rejected(
      [&] {
        return simulation::TerrainDefinition::create(bounds(), simulation::TerrainGround::kSolid,
                                                     {}, holes);
      },
      simulation::SimulationValidationCode::kTerrainShapeLimitExceeded);
  std::vector<simulation::Vector2> points;
  for (std::size_t index = 0; index <= simulation::kMaximumTerrainPointCount; ++index) {
    points.push_back(point(static_cast<double>(index), 50.0));
  }
  require_rejected([&] { return simulation::TerrainCorridor::create("road", 1.0, points); },
                   simulation::SimulationValidationCode::kTerrainShapeLimitExceeded);
}

TEST_CASE("terrain startup rejects a supported center whose configured disc crosses a hole",
          "[unit][simulation][terrain][spawn_system]") {
  const auto terrain = simulation::TerrainDefinition::create(
      bounds(), simulation::TerrainGround::kSolid, {},
      {simulation::TerrainHole::create("pit", point(50.0, 50.0), 10.0)});
  const auto map = simulation::MapDefinition::create(
      "terrain_spawn", terrain, {}, {simulation::MapDefinition::Marker::spawn(point(61.0, 50.0))},
      simulation::MapMetadata::none());
  CHECK(simulation::terrain_supports_point(terrain, point(61.0, 50.0)));
  const auto too_large = simulation::SimulationConfig::create(
      100.0, 100.0, 2.0, simulation::SimulationConfig::kRequiredTicksPerSecond, 4, 4);
  const auto fits = simulation::SimulationConfig::create(
      100.0, 100.0, 0.5, simulation::SimulationConfig::kRequiredTicksPerSecond, 4, 4);
  CHECK_THROWS_AS(simulation::require_spawn_points_are_seatable(too_large, map),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::GameWorld::create(too_large, map, 1),
                  simulation::SimulationValidationError);
  CHECK_NOTHROW(simulation::GameWorld::create(fits, map, 1));
}

TEST_CASE("map validation rejects a static body center on void",
          "[unit][simulation][terrain][map_definition]") {
  const auto terrain = simulation::TerrainDefinition::create(
      bounds(), simulation::TerrainGround::kSolid, {},
      {simulation::TerrainHole::create("pit", point(50.0, 50.0), 10.0)});
  const auto body = simulation::PhysicsBody::create_static(point(50.0, 50.0));
  require_rejected(
      [&] {
        return simulation::MapDefinition::create("unsupported_obstacle", terrain, {body}, {},
                                                 simulation::MapMetadata::none());
      },
      simulation::SimulationValidationCode::kTerrainGeometryOutOfBounds);
}
