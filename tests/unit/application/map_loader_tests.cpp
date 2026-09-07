#include "application_input_error.hpp"
#include "map_loader.hpp"

#include "map_definition.hpp"
#include "physics_body.hpp"
#include "simulation_validation_error.hpp"

#include "application_input_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace blob_royale::application {
namespace {

using test_fixture::replace_once;
using test_fixture::require_application_input_error_code;
using test_fixture::TemporaryApplicationInputWorkspace;

constexpr std::string_view kMapName = "loader-arena";

constexpr std::string_view kValidMapConfiguration = "[map]\n"
                                                    "name=loader-arena\n"
                                                    "display_name=Loader Arena\n"
                                                    "\n"
                                                    "[bounds]\n"
                                                    "width_world_units=200\n"
                                                    "height_world_units=100\n";

constexpr std::string_view kValidStaticBodies =
    "position_x_world_units,position_y_world_units,collision_layer,collision_mask\n"
    "20,30,2,1\n";

constexpr std::string_view kValidMarkers =
    "marker_kind,position_x_world_units,position_y_world_units,team_id\n"
    "spawn,50,50,\n"
    "spawn,150,50,\n"
    "flag_home,100,20,3\n";

// Writes one authored map directory, with each of its three files individually replaceable.
//
// `map.cfg`'s `name` is rewritten to the directory name, because a map that declares a different
// name is its own rejection and every test below is about a different rule. The one test that is
// about the mismatch writes the configuration itself.
[[nodiscard]] std::filesystem::path
write_map_directory(const TemporaryApplicationInputWorkspace& workspace,
                    const std::string_view directory_name,
                    const std::string_view static_bodies = kValidStaticBodies,
                    const std::string_view markers = kValidMarkers,
                    const std::string_view map_configuration = kValidMapConfiguration) {
  const std::filesystem::path directory = workspace.create_directory(directory_name);
  static_cast<void>(
      workspace.write_file(std::string{directory_name} + "/map.cfg",
                           map_configuration.find("name=loader-arena") == std::string_view::npos
                               ? std::string{map_configuration}
                               : replace_once(std::string{map_configuration}, "name=loader-arena",
                                              "name=" + std::string{directory_name})));
  static_cast<void>(
      workspace.write_file(std::string{directory_name} + "/static_bodies.csv", static_bodies));
  static_cast<void>(workspace.write_file(std::string{directory_name} + "/markers.csv", markers));
  return directory;
}

} // namespace

TEST_CASE("map loader reads the three authored files into one validated map",
          "[unit][application][map]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path directory = write_map_directory(workspace, kMapName);

  const simulation::MapDefinition map = MapLoader::load(directory);

  CHECK(map.name() == kMapName);
  CHECK(map.bounds().width() == 200.0);
  CHECK(map.bounds().height() == 100.0);
  REQUIRE(map.metadata().find("display_name") != nullptr);
  CHECK(*map.metadata().find("display_name") == "Loader Arena");

  REQUIRE(map.static_bodies().size() == 1);
  CHECK(map.static_bodies()[0].is_static());
  CHECK(map.static_bodies()[0].position() == simulation::Vector2::create(20.0, 30.0));
  CHECK(map.static_bodies()[0].collision_layer() == 2);
  CHECK(map.static_bodies()[0].collision_mask() == 1);
  // A map declares no size: the radius is filled in by the world's seating from the configuration.
  CHECK(map.static_bodies()[0].radius() == simulation::PhysicsBody::kUndeclaredRadius);

  // Markers keep their authored order, and `spawn_points()` is the derived projection of the ones
  // the engine itself understands.
  REQUIRE(map.markers().size() == 3);
  CHECK(map.markers()[2].kind == "flag_home");
  REQUIRE(map.markers()[2].team.has_value());
  CHECK(map.markers()[2].team->value() == 3);
  REQUIRE(map.spawn_points().size() == 2);
  CHECK(map.spawn_points()[0].position == simulation::Vector2::create(50.0, 50.0));
  CHECK_FALSE(map.spawn_points()[0].team.has_value());
}

TEST_CASE("map loader publishes the two accepted CSV headers", "[unit][application][map]") {
  CHECK(MapLoader::expected_static_bodies_header() ==
        "position_x_world_units,position_y_world_units,collision_layer,collision_mask");
  CHECK(MapLoader::expected_markers_header() ==
        "marker_kind,position_x_world_units,position_y_world_units,team_id");
}

TEST_CASE("map loader rejects a map whose declared name is not its directory name",
          "[unit][application][map][validation]") {
  // Two names for one map is two ways to refer to it, and the one the wire publishes would then be
  // able to disagree with the one `[match] map=` selected.
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path directory = workspace.create_directory("mismatched-arena");
  static_cast<void>(workspace.write_file("mismatched-arena/map.cfg", kValidMapConfiguration));
  static_cast<void>(workspace.write_file("mismatched-arena/static_bodies.csv", kValidStaticBodies));
  static_cast<void>(workspace.write_file("mismatched-arena/markers.csv", kValidMarkers));

  require_application_input_error_code([&] { static_cast<void>(MapLoader::load(directory)); },
                                       ApplicationInputErrorCode::kMapNameMismatch);
}

TEST_CASE("map loader rejects an absent map directory", "[unit][application][map][validation]") {
  TemporaryApplicationInputWorkspace workspace;

  require_application_input_error_code(
      [&] { static_cast<void>(MapLoader::load(workspace.absent_path("no-such-map"))); },
      ApplicationInputErrorCode::kMapFileMissing);
}

TEST_CASE("map loader rejects a map configuration with a missing key, unknown key, or unknown "
          "section",
          "[unit][application][map][validation]") {
  TemporaryApplicationInputWorkspace workspace;

  const std::filesystem::path missing_key = write_map_directory(
      workspace, "missing-key", kValidStaticBodies, kValidMarkers,
      replace_once(std::string{kValidMapConfiguration}, "display_name=Loader Arena\n", ""));
  const std::filesystem::path unknown_key =
      write_map_directory(workspace, "unknown-key", kValidStaticBodies, kValidMarkers,
                          replace_once(std::string{kValidMapConfiguration},
                                       "display_name=Loader Arena", "author=cole"));
  const std::filesystem::path unknown_section = write_map_directory(
      workspace, "unknown-section", kValidStaticBodies, kValidMarkers,
      replace_once(std::string{kValidMapConfiguration}, "[bounds]", "[boundary]"));

  for (const std::filesystem::path& directory : {missing_key, unknown_key, unknown_section}) {
    INFO("map directory " << directory.string());
    require_application_input_error_code([&] { static_cast<void>(MapLoader::load(directory)); },
                                         ApplicationInputErrorCode::kMapValueInvalid);
  }
}

TEST_CASE("map loader rejects a CSV whose header is not the accepted one",
          "[unit][application][map][validation]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path bodies = write_map_directory(
      workspace, "bad-body-header",
      replace_once(std::string{kValidStaticBodies}, "collision_layer", "radius_world_units"));
  const std::filesystem::path markers =
      write_map_directory(workspace, "bad-marker-header", kValidStaticBodies,
                          replace_once(std::string{kValidMarkers}, "marker_kind", "kind"));

  for (const std::filesystem::path& directory : {bodies, markers}) {
    INFO("map directory " << directory.string());
    require_application_input_error_code([&] { static_cast<void>(MapLoader::load(directory)); },
                                         ApplicationInputErrorCode::kMapHeaderInvalid);
  }
}

TEST_CASE("map loader rejects a CSV row with the wrong column count or an unparsable value",
          "[unit][application][map][validation]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path wrong_columns =
      write_map_directory(workspace, "wrong-columns",
                          replace_once(std::string{kValidStaticBodies}, "20,30,2,1", "20,30,2"));
  const std::filesystem::path bad_value =
      write_map_directory(workspace, "bad-value",
                          replace_once(std::string{kValidStaticBodies}, "20,30,2,1", "20,x,2,1"));
  const std::filesystem::path blank_row = write_map_directory(
      workspace, "blank-row",
      replace_once(std::string{kValidStaticBodies}, "20,30,2,1\n", "\n20,30,2,1\n"));

  require_application_input_error_code([&] { static_cast<void>(MapLoader::load(wrong_columns)); },
                                       ApplicationInputErrorCode::kMapColumnCountInvalid);
  require_application_input_error_code([&] { static_cast<void>(MapLoader::load(bad_value)); },
                                       ApplicationInputErrorCode::kMapValueInvalid);
  require_application_input_error_code([&] { static_cast<void>(MapLoader::load(blank_row)); },
                                       ApplicationInputErrorCode::kMapRowEmpty);
}

TEST_CASE("map loader lets the simulation reject content it owns",
          "[unit][application][map][validation]") {
  // An out-of-bounds marker is a `MapDefinition::create` rule, not a parsing rule, so the loader
  // does not re-derive it: the typed simulation error propagates with its own code.
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path directory = write_map_directory(
      workspace, "out-of-bounds", kValidStaticBodies,
      replace_once(std::string{kValidMarkers}, "spawn,50,50,", "spawn,500,50,"));

  CHECK_THROWS_AS(static_cast<void>(MapLoader::load(directory)),
                  simulation::SimulationValidationError);
}

} // namespace blob_royale::application
