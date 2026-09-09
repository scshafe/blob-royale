#include "king_of_the_hill/hill_geometry.hpp"

#include "gameplay_test_fixture.hpp"

#include "map_definition.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::Vector2 point(const double x, const double y) {
  return simulation::Vector2::create(x, y);
}

const std::array<simulation::Vector2, 3> kTour{point(100.0, 100.0), point(300.0, 100.0),
                                               point(300.0, 500.0)};

} // namespace

TEST_CASE("one marker never moves, whatever the tour's timing",
          "[unit][gameplay][king_of_the_hill][hill]") {
  const std::array<simulation::Vector2, 1> lone{point(480.0, 320.0)};
  for (const std::uint64_t elapsed : {0ULL, 1ULL, 4'799ULL, 4'800ULL, 1'000'000ULL}) {
    CHECK(gameplay::hill_center(lone, 4'800, 1'600, elapsed) == point(480.0, 320.0));
  }
}

TEST_CASE("the hill dwells, glides with the division before the multiplication, and cycles",
          "[unit][gameplay][king_of_the_hill][hill]") {
  // D = 100, T = 40: each stop is 140 ticks, and three markers make a 420-tick tour.
  CHECK(gameplay::hill_center(kTour, 100, 40, 0) == kTour[0]);
  CHECK(gameplay::hill_center(kTour, 100, 40, 99) == kTour[0]);
  // The first glide tick is at fraction 0, which is the marker itself.
  CHECK(gameplay::hill_center(kTour, 100, 40, 100) == kTour[0]);
  // Halfway through the glide is exactly halfway between the markers, because (20 / 40) is exact
  // and the product of an exact half is exact.
  CHECK(gameplay::hill_center(kTour, 100, 40, 120) == point(200.0, 100.0));
  CHECK(gameplay::hill_center(kTour, 100, 40, 130) == point(250.0, 100.0));
  // The next stop begins at the next marker.
  CHECK(gameplay::hill_center(kTour, 100, 40, 140) == kTour[1]);
  CHECK(gameplay::hill_center(kTour, 100, 40, 280) == kTour[2]);
  // The last marker glides back to the first: the tour is a cycle.
  CHECK(gameplay::hill_center(kTour, 100, 40, 400) == point(200.0, 300.0));
  CHECK(gameplay::hill_center(kTour, 100, 40, 420) == kTour[0]);
  CHECK(gameplay::hill_center(kTour, 100, 40, 420 + 120) == point(200.0, 100.0));
}

TEST_CASE("a travel of zero is a hop, and a dwell of zero never stops",
          "[unit][gameplay][king_of_the_hill][hill]") {
  CHECK(gameplay::hill_center(kTour, 100, 0, 99) == kTour[0]);
  CHECK(gameplay::hill_center(kTour, 100, 0, 100) == kTour[1]);
  CHECK(gameplay::hill_center(kTour, 100, 0, 299) == kTour[2]);
  CHECK(gameplay::hill_center(kTour, 100, 0, 300) == kTour[0]);

  CHECK(gameplay::hill_center(kTour, 0, 40, 0) == kTour[0]);
  CHECK(gameplay::hill_center(kTour, 0, 40, 20) == point(200.0, 100.0));
  CHECK(gameplay::hill_center(kTour, 0, 40, 40) == kTour[1]);
}

TEST_CASE("hill markers are read in declared order and every other kind is ignored",
          "[unit][gameplay][king_of_the_hill][hill]") {
  std::vector<simulation::MapDefinition::Marker> markers;
  markers.push_back(simulation::MapDefinition::Marker::spawn(point(50.0, 50.0)));
  markers.push_back(simulation::MapDefinition::Marker::create(
      "hill", point(300.0, 100.0), std::nullopt, simulation::MapMetadata::none()));
  markers.push_back(simulation::MapDefinition::Marker::create(
      "checkpoint", point(400.0, 100.0), std::nullopt, simulation::MapMetadata::none()));
  markers.push_back(simulation::MapDefinition::Marker::create(
      "hill", point(100.0, 100.0), std::nullopt, simulation::MapMetadata::none()));
  const simulation::MapDefinition map = simulation::MapDefinition::create(
      "hill_marker_map", simulation::ArenaBounds::create(960.0, 640.0), {}, std::move(markers),
      simulation::MapMetadata::none());

  CHECK(gameplay::hill_markers(map) ==
        std::vector<simulation::Vector2>{point(300.0, 100.0), point(100.0, 100.0)});
  CHECK(gameplay::hill_markers(blob_royale::testing::gameplay_map(4)).empty());
}
