#include "map_loader.hpp"
#include "race/race_configuration.hpp"
#include "race/race_course.hpp"
#include "terrain_queries.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>

namespace application = blob_royale::application;
namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;

namespace {

// Independent frozen loop from ba75500:src/gameplay/race/race_course.cpp. After Step 6 the
// production course delegates, so the proof must retain the old written arithmetic here.
double frozen_legacy_distance_to_centreline(const std::span<const simulation::Vector2> track,
                                            const simulation::Vector2& point) {
  double nearest_distance = std::numeric_limits<double>::infinity();
  for (std::size_t index = 1; index < track.size(); ++index) {
    const simulation::Vector2& a = track[index - 1];
    const simulation::Vector2& b = track[index];
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double wx = point.x() - a.x();
    const double wy = point.y() - a.y();
    double t = (wx * dx + wy * dy) / (dx * dx + dy * dy);
    if (t < 0.0) {
      t = 0.0;
    } else if (t > 1.0) {
      t = 1.0;
    }
    const double cx = a.x() + (dx * t);
    const double cy = a.y() + (dy * t);
    const double ex = point.x() - cx;
    const double ey = point.y() - cy;
    const double distance = std::sqrt(ex * ex + ey * ey);
    if (distance < nearest_distance) {
      nearest_distance = distance;
    }
  }
  return nearest_distance;
}

} // namespace

TEST_CASE("terrain corridor promotion matches the existing circuit course bit for bit",
          "[unit][application][terrain][race][promotion]") {
  const auto repository =
      std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
  const auto map = application::MapLoader::load(repository / "maps" / "circuit-960x640");
  const auto* road = map.terrain().find_corridor("road");
  REQUIRE(road != nullptr);
  auto section = gameplay::RaceConfiguration::default_section();
  section.road = "road";
  const auto course =
      gameplay::RaceCourse::create(map, gameplay::RaceConfiguration::create(section));
  REQUIRE(course.track().size() == road->points().size());
  for (std::size_t index = 0; index < course.track().size(); ++index) {
    CHECK(course.track()[index] == road->points()[index]);
  }

  const auto compare = [&](const simulation::Vector2& point) {
    const double legacy = frozen_legacy_distance_to_centreline(road->points(), point);
    const double canonical = simulation::corridor_distance_to_centreline(*road, point);
    CHECK(std::bit_cast<std::uint64_t>(legacy) == std::bit_cast<std::uint64_t>(canonical));
    CHECK(std::bit_cast<std::uint64_t>(legacy) ==
          std::bit_cast<std::uint64_t>(course.distance_to_centreline(point)));
  };
  for (const auto& point : road->points()) {
    compare(point);
  }
  for (const auto& marker : map.markers()) {
    compare(marker.position);
  }
  // Includes bends, endpoints, outside-envelope points, and fractional projections. The old
  // gameplay implementation above remains independent after the Step 6 delegation.
  for (double x = -70.0; x <= 1030.0; x += 17.25) {
    for (double y = -70.0; y <= 710.0; y += 13.5) {
      compare(simulation::Vector2::create(x, y));
    }
  }
}
