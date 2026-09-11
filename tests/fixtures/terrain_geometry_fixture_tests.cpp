#include "terrain_definition.hpp"
#include "terrain_queries.hpp"

#include <boost/json.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] boost::json::value read_geometry_goldens() {
  const auto path =
      std::filesystem::path{BLOB_ROYALE_TERRAIN_FIXTURE_DIRECTORY} / "geometry-goldens.json";
  std::ifstream input{path, std::ios::binary};
  if (!input.is_open()) {
    throw std::runtime_error{"failed to open shared terrain goldens: " + path.string()};
  }
  const std::string content{std::istreambuf_iterator<char>{input},
                            std::istreambuf_iterator<char>{}};
  if (input.bad()) {
    throw std::runtime_error{"failed to read shared terrain goldens: " + path.string()};
  }
  return boost::json::parse(content);
}

[[nodiscard]] simulation::Vector2 fixture_point(const boost::json::value& value) {
  return simulation::Vector2::create(boost::json::value_to<double>(value.as_object().at("x")),
                                     boost::json::value_to<double>(value.as_object().at("y")));
}

// Test-only JSON projection; production validation and geometry remain the canonical factories.
// The browser consumes the same authored data, never this C++ projection or a second support rule.
[[nodiscard]] simulation::TerrainDefinition fixture_terrain(const boost::json::value& value) {
  const auto& bounds = value.as_object().at("bounds");
  const auto arena = simulation::ArenaBounds::create(
      boost::json::value_to<double>(bounds.as_object().at("width_world_units")),
      boost::json::value_to<double>(bounds.as_object().at("height_world_units")));
  const auto& ground = value.as_object().at("ground").as_string();
  if (ground != "solid" && ground != "corridors") {
    throw std::runtime_error{"unknown ground in shared terrain goldens"};
  }
  std::vector<simulation::TerrainCorridor> corridors;
  for (const auto& row : value.as_object().at("corridors").as_array()) {
    std::vector<simulation::Vector2> points;
    for (const auto& point : row.as_object().at("points").as_array()) {
      points.push_back(fixture_point(point));
    }
    corridors.push_back(simulation::TerrainCorridor::create(
        boost::json::value_to<std::string>(row.as_object().at("name")),
        boost::json::value_to<double>(row.as_object().at("half_width")), std::move(points)));
  }
  std::vector<simulation::TerrainHole> holes;
  for (const auto& row : value.as_object().at("holes").as_array()) {
    holes.push_back(simulation::TerrainHole::create(
        boost::json::value_to<std::string>(row.as_object().at("name")),
        fixture_point(row.as_object().at("center")),
        boost::json::value_to<double>(row.as_object().at("radius"))));
  }
  return simulation::TerrainDefinition::create(
      arena,
      ground == "solid" ? simulation::TerrainGround::kSolid : simulation::TerrainGround::kCorridors,
      std::move(corridors), std::move(holes));
}

} // namespace

TEST_CASE("shared browser terrain goldens validate and preserve Boolean support",
          "[fixtures][terrain][protocol]") {
  const auto document = read_geometry_goldens();
  const auto& cases = document.as_object().at("cases").as_array();
  REQUIRE(cases.size() == 6);
  for (const auto& entry : cases) {
    DYNAMIC_SECTION(entry.as_object().at("name")) {
      const auto terrain = fixture_terrain(entry.as_object().at("terrain"));
      REQUIRE_FALSE(entry.as_object().at("probes").as_array().empty());
      for (const auto& probe : entry.as_object().at("probes").as_array()) {
        INFO(probe);
        CHECK(simulation::terrain_supports_point(terrain,
                                                 fixture_point(probe.as_object().at("point"))) ==
              probe.as_object().at("supported").as_bool());
      }
    }
  }
}
