#include "game_mode_registry.hpp"
#include "map_loader.hpp"
#include "match_configuration.hpp"
#include "match_startup_validation.hpp"
#include "race/race_course.hpp"

#include "map_definition.hpp"
#include "physics_body.hpp"
#include "simulation_config.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <numbers>
#include <span>
#include <string>
#include <vector>

namespace {

namespace application = blob_royale::application;
namespace simulation = blob_royale::simulation;

constexpr double kArenaWidth = 960.0;
constexpr double kArenaHeight = 640.0;
constexpr double kPlayerRadius = 10.0;
constexpr std::size_t kSpawnPointCount = 32;

// `docs/architecture/0005-royale-mode.md` section "Spawning":
// `ring_radius = 0.75 * (min(width, height) / 2 - player_radius)`, which for this arena and a
// 10 wu player radius is 232.5 wu. The markers are decimal literals in the CSV rather than
// arithmetic, so this test is what pins the literals to the documented formula.
constexpr double kDocumentedRingRadius = 232.5;

[[nodiscard]] std::filesystem::path shipped_map_directory() {
  return std::filesystem::path{BLOB_ROYALE_MAPS_DIRECTORY} / "arena-960x640";
}

[[nodiscard]] simulation::SimulationConfig shipped_configuration() {
  return simulation::SimulationConfig::create(kArenaWidth, kArenaHeight, kPlayerRadius,
                                              simulation::SimulationConfig::kRequiredTicksPerSecond,
                                              16, 16);
}

} // namespace

TEST_CASE("the shipped arena-960x640 map loads through the production map loader",
          "[fixtures][map]") {
  const simulation::MapDefinition map = application::MapLoader::load(shipped_map_directory());

  CHECK(map.name() == "arena-960x640");
  CHECK(map.bounds().width() == kArenaWidth);
  CHECK(map.bounds().height() == kArenaHeight);
  // `static_bodies.csv` is header-only: this arena has no obstacles.
  CHECK(map.static_bodies().empty());
  REQUIRE(map.metadata().find("display_name") != nullptr);
  CHECK(*map.metadata().find("display_name") == "Arena 960x640");
}

TEST_CASE("the shipped arena publishes 32 spawn points on the documented ring",
          "[fixtures][map][royale]") {
  const simulation::MapDefinition map = application::MapLoader::load(shipped_map_directory());
  const std::span<const simulation::MapDefinition::Marker> spawn_points = map.spawn_points();

  REQUIRE(spawn_points.size() == kSpawnPointCount);
  CHECK(map.markers().size() == kSpawnPointCount);

  const double center_x = kArenaWidth / 2.0;
  const double center_y = kArenaHeight / 2.0;
  for (std::size_t index = 0; index < spawn_points.size(); ++index) {
    INFO("spawn point " << index);
    const simulation::Vector2& position = spawn_points[index].position;
    // Every point sits on the documented ring...
    CHECK(std::hypot(position.x() - center_x, position.y() - center_y) ==
          Catch::Approx(kDocumentedRingRadius).margin(1e-6));
    // ...at the documented angle, with slot 0 on +x and the index increasing counter-clockwise.
    const double angle =
        2.0 * std::numbers::pi * static_cast<double>(index) / static_cast<double>(kSpawnPointCount);
    CHECK(position.x() ==
          Catch::Approx(center_x + kDocumentedRingRadius * std::cos(angle)).margin(1e-6));
    CHECK(position.y() ==
          Catch::Approx(center_y + kDocumentedRingRadius * std::sin(angle)).margin(1e-6));
    CHECK_FALSE(spawn_points[index].team.has_value());
  }
}

TEST_CASE("the shipped arena and the shipped roster fit the snapshot entity bound",
          "[fixtures][map][match]") {
  const simulation::MapDefinition map = application::MapLoader::load(shipped_map_directory());
  const application::MatchConfiguration match = application::MatchConfiguration::create(
      "royale", "arena-960x640", BLOB_ROYALE_MAPS_DIRECTORY, 1, 4,
      application::MatchConfiguration::parse_bot_roster("wanderer:2"));

  // An empty hazard span, which is the no-hazard case and the exact bound this was before hazards
  // existed. The shipped deployment *does* declare hazards now, and that the real table fits is
  // asserted against the real configuration in `deployment_fixture_tests.cpp` rather than against
  // this hand-built roster, so the two tests cannot disagree about what is deployed.
  CHECK_NOTHROW(application::require_match_fits_snapshot_bound(match, map, {}));
  CHECK_NOTHROW(application::require_map_matches_published_world(shipped_configuration(), map));
}

TEST_CASE("the shipped hills-960x640 map loads through the production map loader",
          "[fixtures][map][king_of_the_hill]") {
  const blob_royale::simulation::MapDefinition map = blob_royale::application::MapLoader::load(
      std::filesystem::path{BLOB_ROYALE_MAPS_DIRECTORY} / "hills-960x640");

  CHECK(map.name() == "hills-960x640");
  CHECK(map.bounds().width() == 960.0);
  CHECK(map.bounds().height() == 640.0);
  CHECK(map.static_bodies().empty());
  // Eight spawn points, every fourth slot of the arena's ring, and the four stops of the tour.
  CHECK(map.spawn_points().size() == 8);
  std::size_t hill_count = 0;
  for (const blob_royale::simulation::MapDefinition::Marker& marker : map.markers()) {
    if (marker.kind == "hill") {
      ++hill_count;
    }
  }
  CHECK(hill_count == 4);
  CHECK_NOTHROW(
      blob_royale::gameplay::GameModeRegistry::create("king_of_the_hill")->validate_map(map));
}

TEST_CASE(
    "the shipped circuit loads its derived course, four-seat grid and two colliding obstacles",
    "[fixtures][map][race]") {
  namespace gameplay = blob_royale::gameplay;
  const simulation::MapDefinition map = application::MapLoader::load(
      std::filesystem::path{BLOB_ROYALE_MAPS_DIRECTORY} / "circuit-960x640");
  const gameplay::RaceConfiguration configuration = gameplay::RaceConfiguration::defaults();
  const gameplay::RaceCourse course = gameplay::RaceCourse::create(map, configuration);
  const auto* road = map.terrain().find_corridor(configuration.road());
  REQUIRE(road != nullptr);
  CHECK(course.track().data() == road->points().data());
  CHECK(course.track_half_width() == road->half_width());
  CHECK(map.name() == "circuit-960x640");
  REQUIRE(map.metadata().find("display_name") != nullptr);
  CHECK(*map.metadata().find("display_name") == "Circuit 960x640");
  CHECK(map.bounds().width() == kArenaWidth);
  CHECK(map.bounds().height() == kArenaHeight);
  CHECK_NOTHROW(application::require_map_matches_published_world(shipped_configuration(), map));

  const simulation::Vector2 start =
      simulation::Vector2::create(kArenaWidth / 6.0, 3.0 * kArenaHeight / 4.0);
  const simulation::Vector2 bend = simulation::Vector2::create(3.0 * kArenaWidth / 4.0, start.y());
  const simulation::Vector2 end = simulation::Vector2::create(bend.x(), kArenaHeight / 4.0);
  const simulation::Vector2 first_gate =
      simulation::Vector2::create(3.0 * kArenaWidth / 8.0, start.y());
  const simulation::Vector2 finish =
      simulation::Vector2::create(bend.x(), 5.0 * kArenaHeight / 16.0);
  CHECK(std::vector<simulation::Vector2>(course.track().begin(), course.track().end()) ==
        std::vector<simulation::Vector2>{start, bend, end});
  CHECK(
      std::vector<simulation::Vector2>(course.checkpoints().begin(), course.checkpoints().end()) ==
      std::vector<simulation::Vector2>{first_gate, bend, finish});
  REQUIRE(map.spawn_points().size() == 4);
  CHECK(map.markers().size() == 7);
  for (const auto& marker : map.markers()) {
    CHECK(marker.kind != "track");
  }
  for (std::size_t index = 0; index < map.spawn_points().size(); ++index) {
    const double x = start.x() + static_cast<double>(index / 2) * course.track_half_width();
    const double y = start.y() + (index % 2 == 0 ? -0.5 : 0.5) * course.track_half_width();
    CHECK(map.spawn_points()[index].position == simulation::Vector2::create(x, y));
    CHECK_FALSE(map.spawn_points()[index].team.has_value());
  }

  const double obstacle_offset = course.track_half_width() - course.checkpoint_radius();
  REQUIRE(map.static_bodies().size() == 2);
  CHECK(map.static_bodies()[0].position() ==
        simulation::Vector2::create(kArenaWidth / 2.0, start.y() - obstacle_offset));
  CHECK(map.static_bodies()[1].position() ==
        simulation::Vector2::create(bend.x() + obstacle_offset, (bend.y() + finish.y()) / 2.0));
  for (const simulation::PhysicsBody& obstacle : map.static_bodies()) {
    CHECK(obstacle.is_static());
    CHECK(obstacle.collision_layer() == simulation::PhysicsBody::kDefaultCollisionLayer);
    CHECK(obstacle.collision_mask() == simulation::PhysicsBody::kDefaultCollisionMask);
    CHECK(course.distance_to_centreline(obstacle.position()) + kPlayerRadius <
          course.track_half_width());
  }
  const auto mode = gameplay::GameModeRegistry::create("race");
  CHECK_NOTHROW(mode->validate_map(map));
  CHECK_NOTHROW(application::require_lobby_fits_map(*mode, 4, map));
}
