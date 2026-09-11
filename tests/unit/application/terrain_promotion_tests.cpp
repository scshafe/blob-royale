#include "map_loader.hpp"
#include "race/race_configuration.hpp"
#include "race/race_course.hpp"
#include "terrain_queries.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstdint>
#include <filesystem>

namespace application = blob_royale::application;
namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;

TEST_CASE("terrain corridor promotion matches the existing circuit course bit for bit",
          "[unit][application][terrain][race][promotion]") {
  const auto repository =
      std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
  const auto map = application::MapLoader::load(repository / "maps" / "circuit-960x640");
  const auto* road = map.terrain().find_corridor("road");
  REQUIRE(road != nullptr);
  auto section = gameplay::RaceConfiguration::default_section();
  section.track_half_width_world_units = road->half_width();
  const auto course =
      gameplay::RaceCourse::create(map, gameplay::RaceConfiguration::create(section));
  REQUIRE(course.track().size() == road->points().size());
  for (std::size_t index = 0; index < course.track().size(); ++index) {
    CHECK(course.track()[index] == road->points()[index]);
  }

  const auto compare = [&](const simulation::Vector2& point) {
    const double legacy = course.distance_to_centreline(point);
    const double canonical = simulation::corridor_distance_to_centreline(*road, point);
    CHECK(std::bit_cast<std::uint64_t>(legacy) == std::bit_cast<std::uint64_t>(canonical));
  };
  for (const auto& marker : map.markers()) {
    compare(marker.position);
  }
  // Includes bends, endpoints, outside-envelope points, and fractional projections. The old
  // gameplay implementation remains the independent reader until the separate Step 6 cutover.
  for (double x = -70.0; x <= 1030.0; x += 17.25) {
    for (double y = -70.0; y <= 710.0; y += 13.5) {
      compare(simulation::Vector2::create(x, y));
    }
  }
}
