#include "map_definition.hpp"
#include "physics_body.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "team_id.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::ArenaBounds bounds(const double width = 500.0,
                                             const double height = 400.0) {
  return simulation::ArenaBounds::create(width, height);
}

[[nodiscard]] simulation::Vector2 point(const double x, const double y) {
  return simulation::Vector2::create(x, y);
}

[[nodiscard]] simulation::StaticBodyDeclaration wall(const double x, const double y) {
  return simulation::StaticBodyDeclaration::create(
      simulation::PhysicsBody::create_static(point(x, y)),
      simulation::ContactEffectPolicy::kClosingImpact);
}

[[nodiscard]] simulation::MapDefinition::Marker marker(std::string kind, const double x,
                                                       const double y) {
  return simulation::MapDefinition::Marker::create(std::move(kind), point(x, y), std::nullopt,
                                                   simulation::MapMetadata::none());
}

[[nodiscard]] simulation::MapDefinition
map(std::vector<simulation::StaticBodyDeclaration> static_bodies = {},
    std::vector<simulation::MapDefinition::Marker> markers = {},
    simulation::MapMetadata metadata = simulation::MapMetadata::none()) {
  return simulation::MapDefinition::create("arena-500x400", bounds(), std::move(static_bodies),
                                           std::move(markers), std::move(metadata));
}

[[nodiscard]] simulation::SimulationValidationCode
rejection_code_of(const simulation::SimulationValidationError& error) noexcept {
  return error.validation_code();
}

} // namespace

TEST_CASE("ArenaBounds is the closed rectangle anchored at the origin",
          "[unit][simulation][map_definition][arena_bounds]") {
  const simulation::ArenaBounds arena = bounds(500.0, 400.0);

  CHECK(arena.width() == 500.0);
  CHECK(arena.height() == 400.0);
  CHECK(arena.contains(point(0.0, 0.0)));
  CHECK(arena.contains(point(500.0, 400.0)));
  CHECK(arena.contains(point(250.0, 200.0)));
  CHECK_FALSE(arena.contains(point(500.000001, 200.0)));
  CHECK_FALSE(arena.contains(point(250.0, 400.000001)));
}

TEST_CASE("ArenaBounds separates the disc-centre interval from the rectangle",
          "[unit][simulation][map_definition][arena_bounds]") {
  // The disc-centre interval is what phase 4 folds into and what a dynamic body must commit
  // inside; the rectangle is the weaker rule a static body's centre obeys. A wall's centre on the
  // arena edge is legal content and is not a legal player centre, and that difference is the whole
  // point of having two predicates.
  const simulation::ArenaBounds arena = bounds(500.0, 400.0);

  CHECK(arena.contains(point(0.0, 200.0)));
  CHECK_FALSE(arena.contains_disc_center(point(0.0, 200.0), 10.0));
  CHECK(arena.contains_disc_center(point(10.0, 10.0), 10.0));
  CHECK(arena.contains_disc_center(point(490.0, 390.0), 10.0));
  CHECK_FALSE(arena.contains_disc_center(point(9.999, 200.0), 10.0));
  CHECK_FALSE(arena.contains_disc_center(point(490.001, 200.0), 10.0));
}

TEST_CASE("ArenaBounds rejects a non-finite or out-of-range extent",
          "[unit][simulation][map_definition][arena_bounds][validation]") {
  CHECK_THROWS_AS(simulation::ArenaBounds::create(0.0, 400.0),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::ArenaBounds::create(500.0, -1.0),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::ArenaBounds::create(simulation::kMaximumWorldDimension + 1.0, 400.0),
                  simulation::SimulationValidationError);
}

TEST_CASE("MapMetadata canonicalizes to ascending key order and finds by key",
          "[unit][simulation][map_definition][map_metadata]") {
  const simulation::MapMetadata metadata =
      simulation::MapMetadata::create({{"theme", "ice"}, {"author", "owner"}, {"difficulty", "3"}});

  REQUIRE(metadata.size() == 3);
  CHECK(metadata.entries()[0].key == "author");
  CHECK(metadata.entries()[1].key == "difficulty");
  CHECK(metadata.entries()[2].key == "theme");
  REQUIRE(metadata.find("theme") != nullptr);
  CHECK(*metadata.find("theme") == "ice");
  CHECK(metadata.find("absent") == nullptr);
}

TEST_CASE("MapMetadata compares equal whatever order it was authored in",
          "[unit][simulation][map_definition][map_metadata]") {
  CHECK(simulation::MapMetadata::create({{"a", "1"}, {"b", "2"}}) ==
        simulation::MapMetadata::create({{"b", "2"}, {"a", "1"}}));
  CHECK(simulation::MapMetadata::none().empty());
}

TEST_CASE("MapMetadata rejects a duplicate key, an invalid key, and an empty value",
          "[unit][simulation][map_definition][map_metadata][validation]") {
  CHECK_THROWS_AS(simulation::MapMetadata::create({{"theme", "ice"}, {"theme", "fire"}}),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::MapMetadata::create({{"Theme", "ice"}}),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::MapMetadata::create({{"", "ice"}}),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::MapMetadata::create({{"theme", ""}}),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(
      simulation::MapMetadata::create(
          {{"theme", std::string(simulation::kMaximumMapMetadataValueLength + 1, 'x')}}),
      simulation::SimulationValidationError);
}

TEST_CASE("MapDefinition publishes its name, bounds, bodies, markers, and metadata",
          "[unit][simulation][map_definition]") {
  const simulation::MapDefinition arena =
      map({wall(0.0, 200.0), wall(500.0, 200.0)},
          {marker("spawn", 100.0, 100.0), marker("flag_home", 250.0, 200.0)},
          simulation::MapMetadata::create({{"theme", "ice"}}));

  CHECK(arena.name() == "arena-500x400");
  CHECK(arena.bounds() == bounds());
  CHECK(arena.static_bodies().size() == 2);
  CHECK(arena.markers().size() == 2);
  REQUIRE(arena.metadata().find("theme") != nullptr);
  CHECK(*arena.metadata().find("theme") == "ice");
}

TEST_CASE("spawn_points is the ordered projection of the markers of kind spawn",
          "[unit][simulation][map_definition][spawn_points]") {
  // Markers are the one authoring concept; spawn_points is derived, never separately authored, so
  // it keeps declared order and holds nothing a marker did not declare.
  const simulation::MapDefinition arena =
      map({}, {marker("flag_home", 20.0, 20.0), marker("spawn", 100.0, 100.0),
               marker("hill_center", 250.0, 200.0), marker("spawn", 300.0, 300.0)});

  REQUIRE(arena.markers().size() == 4);
  REQUIRE(arena.spawn_points().size() == 2);
  CHECK(arena.spawn_points()[0].position == point(100.0, 100.0));
  CHECK(arena.spawn_points()[1].position == point(300.0, 300.0));
  for (const simulation::MapDefinition::Marker& spawn_point : arena.spawn_points()) {
    CHECK(spawn_point.kind == simulation::MapDefinition::kSpawnMarkerKind);
  }
}

TEST_CASE("a map with no spawn marker projects an empty spawn point list",
          "[unit][simulation][map_definition][spawn_points]") {
  // Absence is a defined result, not a failure: whether a mode can play a map with no spawn point
  // is that mode's `validate_map` question, not the map value's.
  const simulation::MapDefinition arena = map({}, {marker("flag_home", 20.0, 20.0)});

  CHECK(arena.spawn_points().empty());
}

TEST_CASE("a marker carries an optional team and its own metadata",
          "[unit][simulation][map_definition][marker]") {
  const simulation::MapDefinition::Marker team_spawn = simulation::MapDefinition::Marker::create(
      "spawn", point(40.0, 40.0), simulation::TeamId::create(2),
      simulation::MapMetadata::create({{"facing", "n"}}));

  REQUIRE(team_spawn.team.has_value());
  CHECK(*team_spawn.team == simulation::TeamId::create(2));
  REQUIRE(team_spawn.metadata.find("facing") != nullptr);
  CHECK(*team_spawn.metadata.find("facing") == "n");
  CHECK_FALSE(simulation::MapDefinition::Marker::spawn(point(40.0, 40.0)).team.has_value());
}

TEST_CASE("MapDefinition rejects a marker kind outside the snake_case vocabulary",
          "[unit][simulation][map_definition][validation]") {
  CHECK_THROWS_AS(simulation::MapDefinition::Marker::create(
                      "Spawn", point(10.0, 10.0), std::nullopt, simulation::MapMetadata::none()),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::MapDefinition::Marker::create("", point(10.0, 10.0), std::nullopt,
                                                            simulation::MapMetadata::none()),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::MapDefinition::Marker::create(
                      "2spawn", point(10.0, 10.0), std::nullopt, simulation::MapMetadata::none()),
                  simulation::SimulationValidationError);
}

TEST_CASE("MapDefinition rejects a dynamic body in its static body list",
          "[unit][simulation][map_definition][validation]") {
  // A dynamic body in the static list would be integrated by the kernel from a position the map
  // chose and never re-chose, so it is a rejection with a named cause rather than a promotion.
  const simulation::PhysicsBody dynamic_body =
      simulation::PhysicsBody::create(point(100.0, 100.0), point(0.0, 0.0), point(0.0, 0.0));

  try {
    static_cast<void>(map({simulation::StaticBodyDeclaration::create(
        dynamic_body, simulation::ContactEffectPolicy::kClosingImpact)}));
    FAIL("a dynamic body in the static body list must be rejected");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(rejection_code_of(error) ==
          simulation::SimulationValidationCode::kMapStaticBodyNotStatic);
  }
}

TEST_CASE("MapDefinition admits a static body on the arena edge and rejects one outside it",
          "[unit][simulation][map_definition][validation]") {
  // The wall on the edge is the obvious obstacle and must be representable; a body whose centre is
  // outside the rectangle can never be reached by a disc phase 4 keeps inside it.
  CHECK_NOTHROW(map({wall(0.0, 0.0), wall(500.0, 400.0)}));

  try {
    static_cast<void>(map({wall(500.5, 200.0)}));
    FAIL("a static body centre outside the arena rectangle must be rejected");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(rejection_code_of(error) ==
          simulation::SimulationValidationCode::kMapStaticBodyOutOfBounds);
  }
}

TEST_CASE("MapDefinition rejects a marker outside the arena rectangle",
          "[unit][simulation][map_definition][validation]") {
  try {
    static_cast<void>(map({}, {marker("spawn", -1.0, 10.0)}));
    FAIL("a marker outside the arena rectangle must be rejected");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(rejection_code_of(error) == simulation::SimulationValidationCode::kMapMarkerOutOfBounds);
  }
}

TEST_CASE("MapDefinition rejects an unusable name",
          "[unit][simulation][map_definition][validation]") {
  CHECK_THROWS_AS(
      simulation::MapDefinition::create("", bounds(), {}, {}, simulation::MapMetadata::none()),
      simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::MapDefinition::create("arena 500", bounds(), {}, {},
                                                    simulation::MapMetadata::none()),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(
      simulation::MapDefinition::create(std::string(simulation::kMaximumMapNameLength + 1, 'a'),
                                        bounds(), {}, {}, simulation::MapMetadata::none()),
      simulation::SimulationValidationError);
}

TEST_CASE("MapDefinition rejects more static bodies or markers than the world has seats",
          "[unit][simulation][map_definition][validation]") {
  std::vector<simulation::StaticBodyDeclaration> bodies;
  bodies.reserve(simulation::kMaximumMapStaticBodyCount + 1);
  for (std::size_t index = 0; index <= simulation::kMaximumMapStaticBodyCount; ++index) {
    bodies.push_back(wall(100.0, 100.0));
  }
  CHECK_THROWS_AS(map(std::move(bodies)), simulation::SimulationValidationError);

  std::vector<simulation::MapDefinition::Marker> markers;
  markers.reserve(simulation::kMaximumMapMarkerCount + 1);
  for (std::size_t index = 0; index <= simulation::kMaximumMapMarkerCount; ++index) {
    markers.push_back(marker("spawn", 100.0, 100.0));
  }
  CHECK_THROWS_AS(map({}, std::move(markers)), simulation::SimulationValidationError);
}

TEST_CASE("the bare arena is the degenerate map every pre-map caller synthesizes",
          "[unit][simulation][map_definition]") {
  const simulation::MapDefinition bare =
      simulation::MapDefinition::bare_arena(bounds(960.0, 640.0));

  CHECK(bare.bounds() == bounds(960.0, 640.0));
  CHECK(bare.static_bodies().empty());
  CHECK(bare.markers().empty());
  CHECK(bare.spawn_points().empty());
  CHECK(bare.metadata().empty());
  CHECK(bare == simulation::MapDefinition::bare_arena(bounds(960.0, 640.0)));
  CHECK_FALSE(bare == simulation::MapDefinition::bare_arena(bounds(960.0, 641.0)));
}
