#include "application_input_error.hpp"
#include "contact_effect_admission.hpp"
#include "map_loader.hpp"

#include "map_definition.hpp"
#include "physics_body.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "terrain_definition.hpp"

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
                                                    "height_world_units=100\n"
                                                    "\n"
                                                    "[terrain]\n"
                                                    "ground=solid\n";

constexpr std::string_view kCorridorDeclaration = "\n[terrain.corridor.road]\n"
                                                  "half_width_world_units=10\n"
                                                  "points_world_units=20,50;100,50;100,20\n";

constexpr std::string_view kHoleDeclaration = "\n[terrain.hole.pit]\n"
                                              "center_x_world_units=100\n"
                                              "center_y_world_units=50\n"
                                              "radius_world_units=5\n";

constexpr std::string_view kValidStaticBodies =
    "position_x_world_units,position_y_world_units,collision_layer,collision_mask,contact_effect_"
    "policy\n"
    "20,30,2,1,closing_impact\n";

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
  CHECK(map.terrain().ground() == simulation::TerrainGround::kSolid);
  CHECK(map.terrain().corridors().empty());
  CHECK(map.terrain().holes().empty());
  REQUIRE(map.metadata().find("display_name") != nullptr);
  CHECK(*map.metadata().find("display_name") == "Loader Arena");

  REQUIRE(map.static_bodies().size() == 1);
  CHECK(map.static_bodies()[0].body().is_static());
  CHECK(map.static_bodies()[0].body().position() == simulation::Vector2::create(20.0, 30.0));
  CHECK(map.static_bodies()[0].body().collision_layer() == 2);
  CHECK(map.static_bodies()[0].body().collision_mask() == 1);
  // A map declares no size: the radius is filled in by the world's seating from the configuration.
  CHECK(map.static_bodies()[0].body().radius() == simulation::PhysicsBody::kUndeclaredRadius);
  CHECK(map.static_bodies()[0].contact_effect_policy() ==
        simulation::ContactEffectPolicy::kClosingImpact);

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
        "position_x_world_units,position_y_world_units,collision_layer,collision_mask,contact_"
        "effect_policy");
  CHECK(MapLoader::expected_markers_header() ==
        "marker_kind,position_x_world_units,position_y_world_units,team_id");
}

TEST_CASE("map loader accepts per-object any-touch policy and refuses missing or unknown authoring",
          "[unit][application][map][contact_effect_admission]") {
  TemporaryApplicationInputWorkspace workspace;
  const auto any_touch =
      replace_once(std::string{kValidStaticBodies}, "closing_impact", "any_touch");
  const auto map = MapLoader::load(write_map_directory(workspace, "any-touch-map", any_touch));
  REQUIRE(map.static_bodies().size() == 1);
  CHECK(map.static_bodies()[0].contact_effect_policy() ==
        simulation::ContactEffectPolicy::kAnyTouch);
  CHECK(map.static_bodies()[0].body().position() == simulation::Vector2::create(20.0, 30.0));
  for (const std::string_view policy : {"", "touch", "Any_Touch", "center_entry"}) {
    TemporaryApplicationInputWorkspace invalid_workspace;
    const auto text = replace_once(std::string{kValidStaticBodies}, "closing_impact", policy);
    const auto directory = write_map_directory(invalid_workspace, "invalid-policy-map", text);
    CHECK_THROWS_AS(MapLoader::load(directory), simulation::SimulationValidationError);
  }
  const auto legacy_header =
      replace_once(std::string{kValidStaticBodies}, ",contact_effect_policy", "");
  const auto directory = write_map_directory(workspace, "legacy-policy-map", legacy_header);
  require_application_input_error_code([&] { static_cast<void>(MapLoader::load(directory)); },
                                       ApplicationInputErrorCode::kMapHeaderInvalid);
  const auto missing_policy = replace_once(std::string{kValidStaticBodies}, ",closing_impact", "");
  const auto missing_directory =
      write_map_directory(workspace, "missing-policy-map", missing_policy);
  require_application_input_error_code(
      [&] { static_cast<void>(MapLoader::load(missing_directory)); },
      ApplicationInputErrorCode::kMapColumnCountInvalid);
}

TEST_CASE("map loader builds named corridors and holes through terrain factories",
          "[unit][application][map][terrain]") {
  TemporaryApplicationInputWorkspace workspace;
  std::string configuration =
      replace_once(std::string{kValidMapConfiguration}, "ground=solid", "ground=corridors");
  configuration.append(kCorridorDeclaration);
  configuration.append(kHoleDeclaration);
  const std::string supported_bodies = replace_once(
      std::string{kValidStaticBodies}, "20,30,2,1,closing_impact", "20,50,2,1,closing_impact");
  const simulation::MapDefinition map = MapLoader::load(
      write_map_directory(workspace, kMapName, supported_bodies, kValidMarkers, configuration));

  CHECK(map.terrain().ground() == simulation::TerrainGround::kCorridors);
  CHECK(map.terrain().bounds() == map.bounds());
  REQUIRE(map.terrain().corridors().size() == 1);
  const simulation::TerrainCorridor& road = map.terrain().corridors()[0];
  CHECK(road.name() == "road");
  CHECK(road.half_width() == 10.0);
  REQUIRE(road.points().size() == 3);
  CHECK(road.points()[0] == simulation::Vector2::create(20.0, 50.0));
  CHECK(road.points()[1] == simulation::Vector2::create(100.0, 50.0));
  CHECK(road.points()[2] == simulation::Vector2::create(100.0, 20.0));
  REQUIRE(map.terrain().holes().size() == 1);
  CHECK(map.terrain().holes()[0].name() == "pit");
  CHECK(map.terrain().holes()[0].center() == simulation::Vector2::create(100.0, 50.0));
  CHECK(map.terrain().holes()[0].radius() == 5.0);
}

TEST_CASE("map loader accepts holes in solid ground and preserves family declaration order",
          "[unit][application][map][terrain]") {
  TemporaryApplicationInputWorkspace workspace;
  std::string configuration{kValidMapConfiguration};
  configuration.append(replace_once(std::string{kHoleDeclaration}, ".pit]", ".zeta]"));
  configuration.append(replace_once(std::string{kHoleDeclaration}, ".pit]", ".alpha]"));
  const simulation::MapDefinition map = MapLoader::load(
      write_map_directory(workspace, kMapName, kValidStaticBodies, kValidMarkers, configuration));

  CHECK(map.terrain().ground() == simulation::TerrainGround::kSolid);
  REQUIRE(map.terrain().holes().size() == 2);
  CHECK(map.terrain().holes()[0].name() == "zeta");
  CHECK(map.terrain().holes()[1].name() == "alpha");
}

TEST_CASE("map loader accepts point-pair whitespace and keeps names local to their family",
          "[unit][application][map][terrain]") {
  TemporaryApplicationInputWorkspace workspace;
  std::string configuration =
      replace_once(std::string{kValidMapConfiguration}, "ground=solid", "ground=corridors");
  configuration.append(replace_once(std::string{kCorridorDeclaration}, "20,50;100,50;100,20",
                                    " 20 , 50 ;\t100,50 ; 100,20 \t"));
  configuration.append(replace_once(std::string{kHoleDeclaration}, ".pit]", ".road]"));
  const std::string supported_bodies = replace_once(
      std::string{kValidStaticBodies}, "20,30,2,1,closing_impact", "20,50,2,1,closing_impact");
  const simulation::MapDefinition map = MapLoader::load(
      write_map_directory(workspace, kMapName, supported_bodies, kValidMarkers, configuration));

  REQUIRE(map.terrain().corridors().size() == 1);
  REQUIRE(map.terrain().holes().size() == 1);
  CHECK(map.terrain().corridors()[0].name() == map.terrain().holes()[0].name());
  REQUIRE(map.terrain().corridors()[0].points().size() == 3);
  CHECK(map.terrain().corridors()[0].points()[0] == simulation::Vector2::create(20.0, 50.0));
}

TEST_CASE("map loader requires explicit terrain and rejects unknown ground spellings",
          "[unit][application][map][terrain][validation]") {
  for (const std::string& configuration :
       {replace_once(std::string{kValidMapConfiguration}, "\n[terrain]\nground=solid\n", ""),
        replace_once(std::string{kValidMapConfiguration}, "ground=solid\n", ""),
        replace_once(std::string{kValidMapConfiguration}, "ground=solid", "ground=Solid"),
        replace_once(std::string{kValidMapConfiguration}, "ground=solid", "ground=void"),
        replace_once(std::string{kValidMapConfiguration}, "ground=solid", "ground="),
        replace_once(std::string{kValidMapConfiguration}, "ground=solid", "ground=solid=solid")}) {
    INFO(configuration);
    TemporaryApplicationInputWorkspace workspace;
    const auto directory =
        write_map_directory(workspace, kMapName, kValidStaticBodies, kValidMarkers, configuration);
    require_application_input_error_code([&] { static_cast<void>(MapLoader::load(directory)); },
                                         ApplicationInputErrorCode::kMapValueInvalid);
  }
}

TEST_CASE("map loader keeps terrain fixed and family section schemas closed",
          "[unit][application][map][terrain][validation]") {
  for (const std::string_view declaration :
       {"\n[terrain]\nground=solid\n", "ground=solid\n", "unknown=solid\n",
        "\n[terrain.unknown.road]\nwidth=10\n",
        "\n[terrain.corridor.]\nhalf_width_world_units=10\n",
        "\n[terrain.hole.]\nradius_world_units=5\n", "\n[terrain.corridor.road] trailing\n",
        "\n[terrain.corridor.road]\nhalf_width_world_units=10\n",
        "\n[terrain.corridor.road]\npoints_world_units=20,50;100,50\n",
        "\n[terrain.hole.pit]\ncenter_x_world_units=100\ncenter_y_world_units=50\n",
        "\n[terrain.hole.pit]\ncenter_x_world_units=100\nradius_world_units=5\n",
        "\n[terrain.hole.pit]\ncenter_y_world_units=50\nradius_world_units=5\n",
        "\n[terrain.hole.pit]\npoints_world_units=20,50;100,50\n"}) {
    INFO(declaration);
    TemporaryApplicationInputWorkspace workspace;
    const auto directory =
        write_map_directory(workspace, kMapName, kValidStaticBodies, kValidMarkers,
                            std::string{kValidMapConfiguration}.append(declaration));
    require_application_input_error_code([&] { static_cast<void>(MapLoader::load(directory)); },
                                         ApplicationInputErrorCode::kMapValueInvalid);
  }
}

TEST_CASE("map loader rejects repeated named sections and repeated family keys",
          "[unit][application][map][terrain][validation]") {
  for (const std::string& declarations :
       {std::string{kHoleDeclaration}.append(kHoleDeclaration),
        std::string{kCorridorDeclaration}.append(kCorridorDeclaration),
        std::string{kHoleDeclaration}.append("radius_world_units=5\n"),
        std::string{kCorridorDeclaration}.append("points_world_units=20,50;100,50\n"),
        std::string{kCorridorDeclaration}.append("radius_world_units=5\n")}) {
    INFO(declarations);
    TemporaryApplicationInputWorkspace workspace;
    const auto directory =
        write_map_directory(workspace, kMapName, kValidStaticBodies, kValidMarkers,
                            std::string{kValidMapConfiguration}.append(declarations));
    require_application_input_error_code([&] { static_cast<void>(MapLoader::load(directory)); },
                                         ApplicationInputErrorCode::kMapValueInvalid);
  }
}

TEST_CASE("map loader rejects malformed corridor point lists without partial acceptance",
          "[unit][application][map][terrain][validation]") {
  for (const std::string_view points :
       {"20,50;", ";20,50", "20,50;;100,50", "20,50,100,50", "20;100,50", "20,;100,50",
        ",50;100,50", "20,nan;100,50", "20,inf;100,50", "20,5x;100,50"}) {
    INFO(points);
    TemporaryApplicationInputWorkspace workspace;
    std::string configuration =
        replace_once(std::string{kValidMapConfiguration}, "ground=solid", "ground=corridors");
    configuration.append(
        replace_once(std::string{kCorridorDeclaration}, "20,50;100,50;100,20", points));
    const auto directory =
        write_map_directory(workspace, kMapName, kValidStaticBodies, kValidMarkers, configuration);
    require_application_input_error_code([&] { static_cast<void>(MapLoader::load(directory)); },
                                         ApplicationInputErrorCode::kMapValueInvalid);
  }
}

TEST_CASE("map loader reports the named terrain field for a malformed scalar",
          "[unit][application][map][terrain][validation]") {
  TemporaryApplicationInputWorkspace workspace;
  const auto directory = write_map_directory(
      workspace, kMapName, kValidStaticBodies, kValidMarkers,
      std::string{kValidMapConfiguration}.append(replace_once(
          std::string{kHoleDeclaration}, "radius_world_units=5", "radius_world_units=nan")));
  try {
    static_cast<void>(MapLoader::load(directory));
    FAIL("non-finite terrain radius must be rejected");
  } catch (const ApplicationInputError& error) {
    CHECK(error.error_code() == ApplicationInputErrorCode::kMapValueInvalid);
    CHECK(error.context() ==
          (directory / "map.cfg").string() + ":terrain.hole.pit.radius_world_units");
  }
}

TEST_CASE("map loader delegates terrain identity and geometry rules to simulation",
          "[unit][application][map][terrain][validation]") {
  const std::string corridor_configuration =
      replace_once(std::string{kValidMapConfiguration}, "ground=solid", "ground=corridors");
  for (const std::string& configuration :
       {corridor_configuration, std::string{kValidMapConfiguration}.append(kCorridorDeclaration),
        corridor_configuration + replace_once(std::string{kCorridorDeclaration}, ".road]", ".Bad]"),
        corridor_configuration + replace_once(std::string{kCorridorDeclaration},
                                              "half_width_world_units=10",
                                              "half_width_world_units=0"),
        corridor_configuration +
            replace_once(std::string{kCorridorDeclaration}, "20,50;100,50;100,20", "20,50"),
        corridor_configuration +
            replace_once(std::string{kCorridorDeclaration}, "20,50;100,50;100,20", "20,50;20,50"),
        corridor_configuration +
            replace_once(std::string{kCorridorDeclaration}, "20,50;100,50;100,20", "20,50;201,50"),
        std::string{kValidMapConfiguration} + replace_once(std::string{kHoleDeclaration},
                                                           "radius_world_units=5",
                                                           "radius_world_units=0"),
        std::string{kValidMapConfiguration} + replace_once(std::string{kHoleDeclaration},
                                                           "center_x_world_units=100",
                                                           "center_x_world_units=201")}) {
    INFO(configuration);
    TemporaryApplicationInputWorkspace workspace;
    const auto directory =
        write_map_directory(workspace, kMapName, kValidStaticBodies, kValidMarkers, configuration);
    CHECK_THROWS_AS(static_cast<void>(MapLoader::load(directory)),
                    simulation::SimulationValidationError);
  }
}

TEST_CASE("map loader delegates terrain count limits to simulation",
          "[unit][application][map][terrain][validation]") {
  TemporaryApplicationInputWorkspace workspace;
  std::string configuration{kValidMapConfiguration};
  for (std::size_t index = 0; index <= simulation::kMaximumTerrainHoleCount; ++index) {
    configuration.append(replace_once(std::string{kHoleDeclaration}, ".pit]",
                                      ".pit_" + std::to_string(index) + "]"));
  }
  const auto directory =
      write_map_directory(workspace, kMapName, kValidStaticBodies, kValidMarkers, configuration);
  try {
    static_cast<void>(MapLoader::load(directory));
    FAIL("excess terrain holes must be rejected by the domain");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kTerrainShapeLimitExceeded);
  }
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
  const std::filesystem::path wrong_columns = write_map_directory(
      workspace, "wrong-columns",
      replace_once(std::string{kValidStaticBodies}, "20,30,2,1,closing_impact", "20,30,2"));
  const std::filesystem::path bad_value =
      write_map_directory(workspace, "bad-value",
                          replace_once(std::string{kValidStaticBodies}, "20,30,2,1,closing_impact",
                                       "20,x,2,1,closing_impact"));
  const std::filesystem::path blank_row = write_map_directory(
      workspace, "blank-row",
      replace_once(std::string{kValidStaticBodies}, "20,30,2,1,closing_impact\n",
                   "\n20,30,2,1,closing_impact\n"));

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
