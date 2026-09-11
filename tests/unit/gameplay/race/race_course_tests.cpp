#include "race/race_course.hpp"

#include "gameplay_validation_error.hpp"
#include "map_definition.hpp"
#include "race/race_configuration.hpp"
#include "terrain_queries.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::Vector2 point(const double x, const double y) {
  return simulation::Vector2::create(x, y);
}

const std::vector<simulation::Vector2> kBentTrack{point(100.0, 100.0), point(300.0, 100.0),
                                                  point(300.0, 400.0)};
const std::vector<simulation::Vector2> kCheckpoints{point(200.0, 100.0), point(300.0, 400.0)};
const std::vector<simulation::Vector2> kSpawnPoints{point(100.0, 100.0)};

[[nodiscard]] simulation::MapDefinition::Marker marker(const std::string_view kind,
                                                       const simulation::Vector2& position) {
  return simulation::MapDefinition::Marker::create(std::string(kind), position, std::nullopt,
                                                   simulation::MapMetadata::none());
}

[[nodiscard]] simulation::MapDefinition
course_map(const std::string& name, const std::span<const simulation::Vector2> track = kBentTrack,
           const std::span<const simulation::Vector2> checkpoints = kCheckpoints,
           const std::span<const simulation::Vector2> spawn_points = kSpawnPoints,
           const double half_width = 70.0, const std::string& road_name = "road") {
  std::vector<simulation::MapDefinition::Marker> markers;
  for (const simulation::Vector2& position : checkpoints) {
    markers.push_back(marker(gameplay::RaceCourse::kCheckpointMarkerKind, position));
  }
  for (const simulation::Vector2& position : spawn_points) {
    markers.push_back(simulation::MapDefinition::Marker::spawn(position));
  }
  const auto terrain = simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(960.0, 640.0), simulation::TerrainGround::kCorridors,
      {simulation::TerrainCorridor::create(road_name, half_width, {track.begin(), track.end()})},
      {});
  return simulation::MapDefinition::create(name, terrain, {}, std::move(markers),
                                           simulation::MapMetadata::none());
}

[[nodiscard]] gameplay::RaceCourse bent_course() {
  return gameplay::RaceCourse::create(course_map("race_bent_course"),
                                      gameplay::RaceConfiguration::defaults());
}

void require_map_rejected(const simulation::MapDefinition& map,
                          const gameplay::GameplayValidationCode expected_code) {
  try {
    static_cast<void>(gameplay::RaceCourse::create(map, gameplay::RaceConfiguration::defaults()));
    FAIL("an invalid race course was accepted");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() == expected_code);
    CHECK(error.detail().find(map.name()) != std::string::npos);
    CHECK(error.code().starts_with("GAMEPLAY.RACE_MAP_"));
  }
}

} // namespace

TEST_CASE("race course distance clamps points beyond both endpoints",
          "[unit][gameplay][race][race_course]") {
  const gameplay::RaceCourse course = bent_course();
  // Each endpoint makes a 30-40-50 triangle. Extending a segment instead of clamping its
  // projection would incorrectly return 40 or 30.
  CHECK(course.distance_to_centreline(point(70.0, 140.0)) == 50.0);
  CHECK(course.distance_to_centreline(point(330.0, 440.0)) == 50.0);
}

TEST_CASE("race course distance is zero on either segment and at their shared node",
          "[unit][gameplay][race][race_course]") {
  const gameplay::RaceCourse course = bent_course();
  CHECK(course.distance_to_centreline(point(200.0, 100.0)) == 0.0);
  CHECK(course.distance_to_centreline(point(300.0, 250.0)) == 0.0);
  CHECK(course.distance_to_centreline(point(300.0, 100.0)) == 0.0);
}

TEST_CASE("race course distance takes the nearer segment at a bend",
          "[unit][gameplay][race][race_course]") {
  const gameplay::RaceCourse course = bent_course();
  CHECK(course.distance_to_centreline(point(280.0, 150.0)) == 20.0);
  CHECK(course.distance_to_centreline(point(250.0, 120.0)) == 20.0);
}

TEST_CASE("race course preserves authored track and checkpoint order while ignoring other kinds",
          "[unit][gameplay][race][race_course]") {
  const std::vector<simulation::Vector2> expected_track{point(300.0, 400.0), point(300.0, 100.0),
                                                        point(100.0, 100.0)};
  const std::vector<simulation::Vector2> expected_checkpoints{point(300.0, 200.0),
                                                              point(150.0, 100.0)};
  std::vector<simulation::MapDefinition::Marker> markers{
      marker("hill", point(800.0, 500.0)), marker("checkpoint", expected_checkpoints[0]),
      simulation::MapDefinition::Marker::spawn(point(300.0, 400.0)),
      marker("checkpoint", expected_checkpoints[1])};
  const auto terrain = simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(960.0, 640.0), simulation::TerrainGround::kCorridors,
      {simulation::TerrainCorridor::create("road", 60.0, expected_track)}, {});
  const simulation::MapDefinition map = simulation::MapDefinition::create(
      "race_declared_order", terrain, {}, std::move(markers), simulation::MapMetadata::none());
  gameplay::RaceConfiguration::Section section = gameplay::RaceConfiguration::default_section();
  section.checkpoint_radius_world_units = 25.0;
  const gameplay::RaceCourse course =
      gameplay::RaceCourse::create(map, gameplay::RaceConfiguration::create(section));

  CHECK(std::vector<simulation::Vector2>(course.track().begin(), course.track().end()) ==
        expected_track);
  CHECK(std::vector<simulation::Vector2>(course.checkpoints().begin(),
                                         course.checkpoints().end()) == expected_checkpoints);
  CHECK(course.track_half_width() == 60.0);
  CHECK(course.checkpoint_radius() == 25.0);
}

TEST_CASE("race course rejects an absent named terrain road and names the map",
          "[unit][gameplay][race][race_course][validation]") {
  require_map_rejected(simulation::MapDefinition::create(
                           "race_solid_ground", simulation::ArenaBounds::create(960.0, 640.0), {},
                           {marker("checkpoint", kCheckpoints[0]),
                            simulation::MapDefinition::Marker::spawn(kSpawnPoints[0])},
                           simulation::MapMetadata::none()),
                       gameplay::GameplayValidationCode::kRaceMapRoadMissing);
  require_map_rejected(
      course_map("race_other_road", kBentTrack, kCheckpoints, kSpawnPoints, 70.0, "other"),
      gameplay::GameplayValidationCode::kRaceMapRoadMissing);
}

TEST_CASE("race road node validation belongs to the terrain factory",
          "[unit][gameplay][race][race_course][validation]") {
  for (const auto& track : std::vector<std::vector<simulation::Vector2>>{
           {},
           {point(100.0, 100.0)},
           {point(100.0, 100.0), point(300.0, 100.0), point(300.0, 100.0)}}) {
    try {
      static_cast<void>(course_map("race_invalid_road", track));
      FAIL("invalid road geometry was accepted before race binding");
    } catch (const simulation::SimulationValidationError& error) {
      CHECK(error.validation_code() ==
            simulation::SimulationValidationCode::kTerrainDefinitionInvalid);
    }
  }
}

TEST_CASE("race course rejects no checkpoint marker and names the map",
          "[unit][gameplay][race][race_course][validation]") {
  require_map_rejected(course_map("race_no_checkpoint", kBentTrack, {}),
                       gameplay::GameplayValidationCode::kRaceMapWithoutCheckpoint);
}

TEST_CASE("race course rejects a checkpoint centre outside the corridor and names the map",
          "[unit][gameplay][race][race_course][validation]") {
  const std::vector<simulation::Vector2> checkpoints{point(200.0, 170.000001)};
  require_map_rejected(course_map("race_outside_checkpoint", kBentTrack, checkpoints),
                       gameplay::GameplayValidationCode::kRaceMapCheckpointOutsideCorridor);
}

TEST_CASE("race course rejects a spawn point outside the corridor and names the map",
          "[unit][gameplay][race][race_course][validation]") {
  const std::vector<simulation::Vector2> spawn_points{point(100.0, 170.000001)};
  require_map_rejected(course_map("race_outside_spawn", kBentTrack, kCheckpoints, spawn_points),
                       gameplay::GameplayValidationCode::kRaceMapSpawnPointOutsideCorridor);
}

TEST_CASE("race course rejects no spawn marker and names the map",
          "[unit][gameplay][race][race_course][validation]") {
  require_map_rejected(course_map("race_no_spawn", kBentTrack, kCheckpoints, {}),
                       gameplay::GameplayValidationCode::kRaceMapWithoutSpawnPoint);
}

TEST_CASE("race course accepts checkpoint and spawn centres on the closed corridor boundary",
          "[unit][gameplay][race][race_course][validation]") {
  // The gate centre, not its complete disc, is constrained by ADR 0007's six map rules.
  const std::vector<simulation::Vector2> checkpoints{point(200.0, 170.0)};
  const std::vector<simulation::Vector2> spawn_points{point(100.0, 170.0)};
  CHECK_NOTHROW(gameplay::RaceCourse::create(
      course_map("race_boundary_centres", kBentTrack, checkpoints, spawn_points),
      gameplay::RaceConfiguration::defaults()));
}

TEST_CASE("race course permits nonconsecutive repeated nodes and a single finish gate",
          "[unit][gameplay][race][race_course][validation]") {
  const std::vector<simulation::Vector2> track{point(100.0, 100.0), point(300.0, 100.0),
                                               point(100.0, 100.0)};
  const std::vector<simulation::Vector2> checkpoints{point(200.0, 100.0)};
  CHECK_NOTHROW(gameplay::RaceCourse::create(course_map("race_returning_track", track, checkpoints),
                                             gameplay::RaceConfiguration::defaults()));
}

TEST_CASE("race course retains its terrain after a temporary map and copies are destroyed",
          "[unit][gameplay][race][race_course][ownership]") {
  const auto course = bent_course();
  const auto copied_then_moved = [&course] {
    auto copied = course;
    auto moved = std::move(copied);
    return moved;
  }();
  CHECK(copied_then_moved == course);
  CHECK(copied_then_moved.track().data() == course.track().data());
  CHECK(copied_then_moved.road_name().data() == course.road_name().data());
  CHECK(copied_then_moved.road_name() == "road");
  CHECK(std::vector<simulation::Vector2>(course.track().begin(), course.track().end()) ==
        kBentTrack);
  CHECK(copied_then_moved.distance_to_centreline(point(280.0, 150.0)) == 20.0);
  CHECK(copied_then_moved.track_half_width() == 70.0);
}

TEST_CASE("race course selects its configured corridor among multiple authored roads",
          "[unit][gameplay][race][race_course][ownership]") {
  const auto terrain = simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(960.0, 640.0), simulation::TerrainGround::kCorridors,
      {simulation::TerrainCorridor::create("road", 80.0,
                                           {point(100.0, 500.0), point(300.0, 500.0)}),
       simulation::TerrainCorridor::create("race_route", 60.0, kBentTrack)},
      {});
  const auto map = simulation::MapDefinition::create(
      "race_selected_road", terrain, {},
      {marker("checkpoint", kCheckpoints[0]), marker("checkpoint", kCheckpoints[1]),
       simulation::MapDefinition::Marker::spawn(kSpawnPoints[0])},
      simulation::MapMetadata::none());
  auto section = gameplay::RaceConfiguration::default_section();
  section.road = "race_route";
  const auto course =
      gameplay::RaceCourse::create(map, gameplay::RaceConfiguration::create(section));
  REQUIRE(map.terrain().find_corridor("race_route") != nullptr);
  CHECK(course.road_name() == "race_route");
  CHECK(course.road_name().data() == map.terrain().find_corridor("race_route")->name().data());
  CHECK(course.track().data() == map.terrain().find_corridor("race_route")->points().data());
  CHECK(course.track_half_width() == 60.0);
  CHECK(course.distance_to_centreline(kCheckpoints[0]) == 0.0);
  require_map_rejected(map, gameplay::GameplayValidationCode::kRaceMapCheckpointOutsideCorridor);
}

TEST_CASE("race course checks gate radius against the resolved terrain width",
          "[unit][gameplay][race][race_course][validation]") {
  const auto map = course_map("race_gate_width");
  auto section = gameplay::RaceConfiguration::default_section();
  section.checkpoint_radius_world_units = 70.0;
  CHECK_NOTHROW(gameplay::RaceCourse::create(map, gameplay::RaceConfiguration::create(section)));
  section.checkpoint_radius_world_units =
      std::nextafter(70.0, std::numeric_limits<double>::infinity());
  const auto configuration = gameplay::RaceConfiguration::create(section);
  try {
    static_cast<void>(gameplay::RaceCourse::create(map, configuration));
    FAIL("gate radius wider than the named road was accepted");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() == gameplay::GameplayValidationCode::kRaceScalarOutOfRange);
    CHECK(error.context() == "race_course.create.checkpoint_radius");
    CHECK(error.detail().find(map.name()) != std::string::npos);
    CHECK(error.detail().find(configuration.road()) != std::string::npos);
  }
}

TEST_CASE("race binding keeps exact corridor admission separate from terrain support",
          "[unit][gameplay][race][race_course][validation]") {
  const auto terrain = simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(960.0, 640.0), simulation::TerrainGround::kCorridors,
      {simulation::TerrainCorridor::create("road", 70.0, kBentTrack)},
      {simulation::TerrainHole::create("pit", kCheckpoints[0], 10.0)});
  const auto map =
      simulation::MapDefinition::create("race_hole_does_not_change_binding", terrain, {},
                                        {marker("checkpoint", kCheckpoints[0]),
                                         simulation::MapDefinition::Marker::spawn(kSpawnPoints[0])},
                                        simulation::MapMetadata::none());
  CHECK_FALSE(simulation::terrain_supports_point(terrain, kCheckpoints[0]));
  CHECK_NOTHROW(gameplay::RaceCourse::create(map, gameplay::RaceConfiguration::defaults()));
  const std::vector<simulation::Vector2> outside{
      point(200.0, std::nextafter(170.0, std::numeric_limits<double>::infinity()))};
  require_map_rejected(course_map("race_no_startup_tolerance", kBentTrack, outside),
                       gameplay::GameplayValidationCode::kRaceMapCheckpointOutsideCorridor);
}
