#include "fixtures/terrain_provenance_fixture.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "terrain_boundary.hpp"
#include "terrain_definition.hpp"
#include "terrain_queries.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace simulation = blob_royale::simulation;
namespace provenance = blob_royale::testing::terrain_provenance_fixture;

namespace {

[[nodiscard]] simulation::Vector2 point(const double x, const double y) {
  return simulation::Vector2::create(x, y);
}

[[nodiscard]] simulation::TerrainDefinition
solid_with_holes(std::vector<simulation::TerrainHole> holes) {
  return simulation::TerrainDefinition::create(simulation::ArenaBounds::create(1000.0, 1000.0),
                                               simulation::TerrainGround::kSolid, {},
                                               std::move(holes));
}

// Exact effective radii isolate interval topology from the independently tested tolerance policy.
[[nodiscard]] simulation::TerrainHole hole(const std::string& name, const double x, const double y,
                                           const double effective_radius) {
  return simulation::TerrainHole::create(name, point(x, y),
                                         effective_radius + simulation::kPositionTolerance);
}

[[nodiscard]] simulation::TerrainCorridor road(const std::string& name,
                                               const double effective_half_width,
                                               std::vector<simulation::Vector2> points) {
  return simulation::TerrainCorridor::create(
      name, effective_half_width - simulation::kPositionTolerance, std::move(points));
}

[[nodiscard]] simulation::TerrainDefinition
roads(std::vector<simulation::TerrainCorridor> corridors,
      std::vector<simulation::TerrainHole> holes = {}) {
  return simulation::TerrainDefinition::create(simulation::ArenaBounds::create(1000.0, 1000.0),
                                               simulation::TerrainGround::kCorridors,
                                               std::move(corridors), std::move(holes));
}

void check_interval(const simulation::TerrainSupportInterval& interval, const double begin,
                    const double end) {
  CHECK(interval.begin.value() == Catch::Approx(begin).margin(1e-14));
  CHECK(interval.end.value() == Catch::Approx(end).margin(1e-14));
}

} // namespace

TEST_CASE("solid terrain support and clearance use the exact closed envelope",
          "[unit][simulation][terrain]") {
  const auto terrain =
      simulation::TerrainDefinition::solid(simulation::ArenaBounds::create(100.0, 80.0));
  CHECK(simulation::terrain_supports_point(terrain, point(0.0, 0.0)));
  CHECK_FALSE(simulation::terrain_supports_point(terrain, point(-1e-12, 40.0)));
  CHECK(simulation::terrain_supports_disc(terrain, point(10.0, 40.0), 10.0));
  CHECK_FALSE(simulation::terrain_supports_disc(terrain, point(10.0, 40.0), 10.001));
  const auto nearest = simulation::nearest_supported_point(terrain, point(-10.0, 40.0));
  REQUIRE(nearest);
  CHECK(nearest->point == point(0.0, 40.0));
  CHECK(nearest->distance == 10.0);
}

TEST_CASE("terrain road edges and hole rims preserve the named position tolerance",
          "[unit][simulation][terrain]") {
  const auto terrain = roads({simulation::TerrainCorridor::create(
                                 "road", 10.0, {point(100.0, 100.0), point(300.0, 100.0)})},
                             {simulation::TerrainHole::create("hole", point(200.0, 100.0), 5.0)});
  CHECK(simulation::terrain_supports_point(terrain, point(150.0, 110.0)));
  CHECK(simulation::terrain_supports_point(terrain, point(150.0, 110.0 + 0.5e-9)));
  CHECK_FALSE(simulation::terrain_supports_point(terrain, point(150.0, 110.0 + 2e-9)));
  CHECK(simulation::terrain_supports_point(terrain, point(205.0, 100.0)));
  CHECK(simulation::terrain_supports_point(terrain, point(205.0 - 0.5e-9, 100.0)));
  CHECK_FALSE(simulation::terrain_supports_point(terrain, point(205.0 - 2e-9, 100.0)));
}

TEST_CASE("swept support detects a narrow hole even when the motion relands on ground",
          "[unit][simulation][terrain]") {
  const auto terrain = solid_with_holes({hole("narrow", 100.0, 100.0, 1.0)});
  const auto intervals =
      simulation::swept_support_intervals(terrain, point(0.0, 100.0), point(200.0, 0.0));
  REQUIRE(intervals.size() == 2);
  check_interval(intervals[0], 0.0, 0.495);
  check_interval(intervals[1], 0.505, 1.0);
  const auto exit = simulation::first_support_exit(terrain, point(0.0, 100.0), point(200.0, 0.0));
  REQUIRE(exit);
  CHECK(exit->value() == 0.495);
}

TEST_CASE("a hole tangent retains support across the whole sweep", "[unit][simulation][terrain]") {
  const auto terrain = solid_with_holes({hole("round", 100.0, 100.0, 10.0)});
  const auto intervals =
      simulation::swept_support_intervals(terrain, point(80.0, 110.0), point(40.0, 0.0));
  REQUIRE(intervals.size() == 1);
  check_interval(intervals[0], 0.0, 1.0);
  CHECK_FALSE(simulation::first_support_exit(terrain, point(80.0, 110.0), point(40.0, 0.0)));
}

TEST_CASE("hole roots are open but a clipped interior endpoint is removed",
          "[unit][simulation][terrain]") {
  const auto terrain = solid_with_holes({hole("round", 100.0, 100.0, 10.0)});
  const auto outward =
      simulation::swept_support_intervals(terrain, point(100.0, 100.0), point(20.0, 0.0));
  REQUIRE(outward.size() == 1);
  check_interval(outward[0], 0.5, 1.0);
  const auto inward =
      simulation::swept_support_intervals(terrain, point(80.0, 100.0), point(20.0, 0.0));
  REQUIRE(inward.size() == 1);
  check_interval(inward[0], 0.0, 0.5);
  CHECK(simulation::first_support_exit(terrain, point(100.0, 100.0), point(20.0, 0.0)) ==
        simulation::MotionTime::start());
}

TEST_CASE("starting on a hole rim inward exits at zero while ending on the rim stays supported",
          "[unit][simulation][terrain]") {
  const auto terrain = solid_with_holes({hole("round", 100.0, 100.0, 10.0)});
  const auto through =
      simulation::swept_support_intervals(terrain, point(90.0, 100.0), point(20.0, 0.0));
  REQUIRE(through.size() == 2);
  check_interval(through[0], 0.0, 0.0);
  check_interval(through[1], 1.0, 1.0);
  CHECK(simulation::first_support_exit(terrain, point(90.0, 100.0), point(20.0, 0.0)) ==
        simulation::MotionTime::start());
  CHECK_FALSE(simulation::first_support_exit(terrain, point(80.0, 100.0), point(10.0, 0.0)));
}

TEST_CASE("stationary support preserves hole rims but rejects hole interiors",
          "[unit][simulation][terrain]") {
  const auto terrain = solid_with_holes({hole("round", 100.0, 100.0, 10.0)});
  const auto rim =
      simulation::swept_support_intervals(terrain, point(90.0, 100.0), point(0.0, 0.0));
  REQUIRE(rim.size() == 1);
  check_interval(rim[0], 0.0, 1.0);
  CHECK(simulation::swept_support_intervals(terrain, point(100.0, 100.0), point(0.0, 0.0)).empty());
}

TEST_CASE("capsule union intervals merge overlaps and exact touching endpoints",
          "[unit][simulation][terrain][terrain_provenance]") {
  const auto terrain = roads({road("left", 10.0, {point(50.0, 100.0), point(100.0, 100.0)}),
                              road("right", 10.0, {point(120.0, 100.0), point(170.0, 100.0)})});
  const auto intervals =
      simulation::swept_support_intervals(terrain, point(0.0, 100.0), point(200.0, 0.0));
  REQUIRE(intervals.size() == 1);
  check_interval(intervals[0], 0.2, 0.9);

  // These distinct capsules touch at one point. A transverse sweep must retain a singleton,
  // rather than treating their common tangent as either a gap or a finite-width passage.
  const auto transverse =
      simulation::swept_support_intervals(terrain, point(110.0, 80.0), point(0.0, 40.0));
  REQUIRE(transverse.size() == 1);
  CHECK(transverse[0].begin.value() == 0.5);
  CHECK(transverse[0].end.value() == 0.5);
  const auto clearance = simulation::disc_clearance(terrain, point(110.0, 100.0));
  REQUIRE(clearance);
  CHECK(clearance->distance == 0.0);
  CHECK(simulation::terrain_supports_disc(terrain, point(110.0, 100.0), 0.0));
  CHECK_FALSE(simulation::terrain_supports_disc(terrain, point(110.0, 100.0),
                                                std::numeric_limits<double>::denorm_min()));
}

TEST_CASE("overlapping holes subtract their union without resurrecting an internal rim",
          "[unit][simulation][terrain]") {
  const auto terrain =
      solid_with_holes({hole("left", 90.0, 100.0, 20.0), hole("right", 110.0, 100.0, 20.0)});
  CHECK_FALSE(simulation::terrain_supports_point(terrain, point(110.0, 100.0)));
  const auto intervals =
      simulation::swept_support_intervals(terrain, point(0.0, 100.0), point(200.0, 0.0));
  REQUIRE(intervals.size() == 2);
  check_interval(intervals[0], 0.0, 0.35);
  check_interval(intervals[1], 0.65, 1.0);
}

TEST_CASE("disc clearance spans a road union even when neither individual road contains the disc",
          "[unit][simulation][terrain]") {
  const auto terrain = roads({road("horizontal", 10.0, {point(50.0, 100.0), point(150.0, 100.0)}),
                              road("vertical", 10.0, {point(100.0, 50.0), point(100.0, 150.0)})});
  const auto clearance = simulation::disc_clearance(terrain, point(100.0, 100.0));
  REQUIRE(clearance);
  CHECK(clearance->distance == Catch::Approx(std::sqrt(200.0)).margin(1e-12));
  CHECK(simulation::terrain_supports_disc(terrain, point(100.0, 100.0), 12.0));
  CHECK_FALSE(simulation::terrain_supports_disc(terrain, point(100.0, 100.0), 15.0));
}

TEST_CASE("opposed coincident road sides are an internal seam and do not reduce disc clearance",
          "[unit][simulation][terrain]") {
  const auto terrain = roads({road("lower", 20.0, {point(50.0, 70.0), point(200.0, 70.0)}),
                              road("upper", 20.0, {point(50.0, 110.0), point(200.0, 110.0)})});
  const auto clearance = simulation::disc_clearance(terrain, point(125.0, 90.0));
  REQUIRE(clearance);
  CHECK(clearance->distance == 40.0);
  CHECK(simulation::terrain_supports_disc(terrain, point(125.0, 90.0), 35.0));
}

TEST_CASE("coincident positive and negative arcs preserve supported rim-only spans",
          "[unit][simulation][terrain][terrain_provenance]") {
  const auto terrain = roads({road("road", 20.0, {point(100.0, 100.0), point(200.0, 100.0)})},
                             {hole("cut", 100.0, 100.0, 20.0)});
  CHECK(simulation::terrain_supports_point(terrain, point(80.0, 100.0)));
  const auto nearest = simulation::nearest_supported_point(terrain, point(70.0, 100.0));
  REQUIRE(nearest);
  CHECK(nearest->point == point(80.0, 100.0));
  const auto clearance = simulation::disc_clearance(terrain, point(80.0, 100.0));
  REQUIRE(clearance);
  CHECK(clearance->distance == 0.0);
  const auto& boundary = simulation::detail::TerrainQueryAccess::boundary(terrain);
  CHECK(std::any_of(boundary.spans.begin(), boundary.spans.end(), [](const auto& span) {
    return span.supported_side == 0 &&
           std::holds_alternative<simulation::detail::TerrainArcSpan>(span.geometry);
  }));
  CHECK_FALSE(simulation::terrain_supports_disc(terrain, point(80.0, 100.0),
                                                std::numeric_limits<double>::denorm_min()));
}

TEST_CASE("a subtracted rectangle can retain isolated supported corners",
          "[unit][simulation][terrain][terrain_provenance]") {
  const auto terrain = simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(6.0, 8.0), simulation::TerrainGround::kSolid, {},
      {hole("cover", 3.0, 4.0, 5.0)});
  CHECK(simulation::terrain_supports_point(terrain, point(0.0, 0.0)));
  CHECK_FALSE(simulation::terrain_supports_point(terrain, point(3.0, 4.0)));
  const auto nearest = simulation::nearest_supported_point(terrain, point(3.0, 4.0));
  REQUIRE(nearest);
  CHECK(nearest->point == point(0.0, 0.0));
  CHECK(nearest->distance == 5.0);
  const auto clearance = simulation::disc_clearance(terrain, point(0.0, 0.0));
  REQUIRE(clearance);
  CHECK(clearance->distance == 0.0);
  const auto& boundary = simulation::detail::TerrainQueryAccess::boundary(terrain);
  CHECK(boundary.spans.empty());
  CHECK(boundary.isolated_points.size() == 4);
  for (const auto& corner : {point(0.0, 0.0), point(6.0, 0.0), point(0.0, 8.0), point(6.0, 8.0)}) {
    CHECK(std::any_of(boundary.isolated_points.begin(), boundary.isolated_points.end(),
                      [&corner](const auto& isolated) { return isolated.point == corner; }));
    CHECK(simulation::terrain_supports_point(terrain, corner));
    CHECK_FALSE(simulation::terrain_supports_disc(terrain, corner,
                                                  std::numeric_limits<double>::denorm_min()));
  }
}

TEST_CASE("fully subtracted terrain returns no nearest point or clearance",
          "[unit][simulation][terrain]") {
  const auto terrain = solid_with_holes({hole("cover", 500.0, 500.0, 900.0)});
  CHECK_FALSE(simulation::nearest_supported_point(terrain, point(500.0, 500.0)));
  CHECK_FALSE(simulation::disc_clearance(terrain, point(500.0, 500.0)));
  CHECK(
      simulation::swept_support_intervals(terrain, point(0.0, 500.0), point(1000.0, 0.0)).empty());
}

TEST_CASE("terrain clearance rejects invalid radii without substituting a usable answer",
          "[unit][simulation][terrain]") {
  const auto terrain =
      simulation::TerrainDefinition::solid(simulation::ArenaBounds::create(100.0, 100.0));
  for (const double radius :
       {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(),
        simulation::kMaximumPhysicalComponentMagnitude * 2.0}) {
    CHECK_THROWS_AS(simulation::terrain_supports_disc(terrain, point(50.0, 50.0), radius),
                    simulation::SimulationValidationError);
  }
}

TEST_CASE("translated axis hole clearance rounds its witness toward supported ground",
          "[unit][simulation][terrain][witness_rounding]") {
  const auto terrain =
      solid_with_holes({simulation::TerrainHole::create("translated", point(50.0, 50.0), 10.0)});
  const double radius = 10.0 - simulation::kPositionTolerance;
  const auto query = point(61.0, 50.0);
  const auto raw_projection = point(50.0 + radius, 50.0);
  REQUIRE_FALSE(simulation::terrain_supports_point(terrain, raw_projection));
  const auto clearance = simulation::disc_clearance(terrain, query);
  REQUIRE(clearance);
  CHECK(clearance->point == point(std::nextafter(raw_projection.x(), query.x()), 50.0));
  CHECK(simulation::terrain_supports_point(terrain, clearance->point));
  CHECK(clearance->distance <= query.x() - raw_projection.x());
  CHECK(clearance->distance == Catch::Approx(11.0 - radius).margin(1e-13));
  CHECK(simulation::terrain_supports_disc(terrain, query, 0.5));
}

TEST_CASE("diagonal hole clearance keeps the selected nearer arc and decreases witness distance",
          "[unit][simulation][terrain][witness_rounding]") {
  const auto terrain =
      solid_with_holes({simulation::TerrainHole::create("translated", point(50.0, 50.0), 10.0)});
  const double radius = 10.0 - simulation::kPositionTolerance;
  const auto query = point(61.0, 55.0);
  const double length = std::sqrt(11.0 * 11.0 + 5.0 * 5.0);
  const auto raw_projection =
      point(50.0 + radius * (11.0 / length), 50.0 + radius * (5.0 / length));
  REQUIRE_FALSE(simulation::terrain_supports_point(terrain, raw_projection));
  const auto clearance = simulation::disc_clearance(terrain, query);
  REQUIRE(clearance);
  CHECK(clearance->point == point(std::nextafter(raw_projection.x(), query.x()),
                                  std::nextafter(raw_projection.y(), query.y())));
  CHECK(simulation::terrain_supports_point(terrain, clearance->point));
  const double raw_x = query.x() - raw_projection.x();
  const double raw_y = query.y() - raw_projection.y();
  CHECK(clearance->distance <= std::sqrt(raw_x * raw_x + raw_y * raw_y));
  CHECK(clearance->distance == Catch::Approx(length - radius).margin(1e-13));
  CHECK(clearance->distance < 3.0); // The envelope and every other arc endpoint are farther away.
}

TEST_CASE("nearest recovery from a diagonal hole uses its supported side not the void query",
          "[unit][simulation][terrain][witness_rounding]") {
  const auto terrain =
      solid_with_holes({simulation::TerrainHole::create("translated", point(50.0, 50.0), 10.0)});
  const double radius = 10.0 - simulation::kPositionTolerance;
  const auto query = point(51.0, 51.0);
  const double component = radius * (1.0 / std::sqrt(2.0));
  const auto raw_projection = point(50.0 + component, 50.0 + component);
  REQUIRE_FALSE(simulation::terrain_supports_point(terrain, raw_projection));
  const auto nearest = simulation::nearest_supported_point(terrain, query);
  REQUIRE(nearest);
  CHECK(nearest->point ==
        point(std::nextafter(raw_projection.x(), std::numeric_limits<double>::infinity()),
              std::nextafter(raw_projection.y(), std::numeric_limits<double>::infinity())));
  CHECK(simulation::terrain_supports_point(terrain, nearest->point));
  CHECK(nearest->distance == Catch::Approx(radius - std::sqrt(2.0)).margin(1e-13));
}

TEST_CASE(
    "a supported point one representable step from a rounded hole rim has zero safe clearance",
    "[unit][simulation][terrain][witness_rounding]") {
  const auto terrain =
      solid_with_holes({simulation::TerrainHole::create("translated", point(50.0, 50.0), 10.0)});
  const double radius = 10.0 - simulation::kPositionTolerance;
  const auto query = point(std::nextafter(50.0 + radius, 61.0), 50.0);
  REQUIRE(simulation::terrain_supports_point(terrain, query));
  const auto clearance = simulation::disc_clearance(terrain, query);
  REQUIRE(clearance);
  CHECK(clearance->point == query);
  CHECK(clearance->distance == 0.0);
  CHECK(simulation::terrain_supports_disc(terrain, query, 0.0));
  CHECK_FALSE(
      simulation::terrain_supports_disc(terrain, query, std::numeric_limits<double>::denorm_min()));
}

TEST_CASE("nearest recovery at overlapping hole intersections follows the Boolean exterior wedge",
          "[unit][simulation][terrain][witness_rounding][vertex_sector]") {
  const auto terrain =
      solid_with_holes({simulation::TerrainHole::create("left", point(120.0, 128.0), 10.0),
                        simulation::TerrainHole::create("right", point(136.0, 128.0), 10.0)});
  const auto query = point(128.0, 130.0);
  REQUIRE_FALSE(simulation::terrain_supports_point(terrain, query));
  const auto nearest = simulation::nearest_supported_point(terrain, query);
  REQUIRE(nearest);
  CHECK(simulation::terrain_supports_point(terrain, nearest->point));
  CHECK(nearest->point.x() == 128.0);
  CHECK(nearest->point.y() == Catch::Approx(134.0).margin(2e-9));
  CHECK(nearest->distance == Catch::Approx(4.0).margin(2e-9));
}

TEST_CASE("oblique overlapping hole recovery keeps its representable displacement in the supported "
          "sector",
          "[unit][simulation][terrain][witness_rounding][vertex_sector]") {
  const auto terrain =
      solid_with_holes({simulation::TerrainHole::create("left", point(120.0, 128.0), 10.0),
                        simulation::TerrainHole::create("right", point(135.2, 139.4), 10.0)});
  const auto query = point(127.0, 134.5);
  REQUIRE_FALSE(simulation::terrain_supports_point(terrain, query));
  const auto nearest = simulation::nearest_supported_point(terrain, query);
  REQUIRE(nearest);
  CHECK(simulation::terrain_supports_point(terrain, nearest->point));
  CHECK(nearest->point.x() < query.x());
  CHECK(nearest->point.y() > query.y());
  CHECK(nearest->distance == Catch::Approx(std::sqrt(100.0 - 9.5 * 9.5) - 1.0).margin(4e-9));
}

TEST_CASE("mixed envelope and hole recovery satisfies both incident boundary halfspaces",
          "[unit][simulation][terrain][witness_rounding][vertex_sector]") {
  const auto terrain = simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(256.0, 256.0), simulation::TerrainGround::kSolid, {},
      {simulation::TerrainHole::create("edge", point(251.0, 128.0), 10.0)});
  const auto query = point(256.0, 133.0);
  REQUIRE_FALSE(simulation::terrain_supports_point(terrain, query));
  const auto nearest = simulation::nearest_supported_point(terrain, query);
  REQUIRE(nearest);
  CHECK(simulation::terrain_supports_point(terrain, nearest->point));
  CHECK(nearest->point.x() <= 256.0);
  CHECK(nearest->point.y() > query.y());
  CHECK(nearest->distance == Catch::Approx(std::sqrt(75.0) - 5.0).margin(4e-9));
}

TEST_CASE("narrow rotated hole recovery preserves the sector direction beyond coordinate signs",
          "[unit][simulation][terrain][witness_rounding][vertex_sector]") {
  const auto terrain = simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(256.0, 256.0), simulation::TerrainGround::kSolid, {},
      {simulation::TerrainHole::create("first", point(40.0, 30.0), 101.0),
       simulation::TerrainHole::create("second", point(160.0, 190.0), 101.0)});
  const auto query = point(92.0, 116.0);
  REQUIRE_FALSE(simulation::terrain_supports_point(terrain, query));
  const auto nearest = simulation::nearest_supported_point(terrain, query);
  REQUIRE(nearest);
  CHECK(simulation::terrain_supports_point(terrain, nearest->point));
  const double radius = 101.0 - simulation::kPositionTolerance;
  const double vertex_height = std::sqrt(radius * radius - 10000.0);
  CHECK(nearest->point.x() == Catch::Approx(100.0 - 0.8 * vertex_height).margin(1e-12));
  CHECK(nearest->point.y() == Catch::Approx(110.0 + 0.6 * vertex_height).margin(1e-12));
  CHECK(nearest->distance == Catch::Approx(vertex_height - 10.0).margin(1e-12));
}

TEST_CASE("terrain provenance admits original sloped and fractional authored smooth joins",
          "[unit][simulation][terrain][terrain_provenance][terrain_provenance_admission]") {
  for (const auto& course : provenance::oblique_courses()) {
    DYNAMIC_SECTION(course.name) {
      const auto terrain = simulation::TerrainDefinition::create(
          simulation::ArenaBounds::create(provenance::kRaceWidth, provenance::kRaceHeight),
          simulation::TerrainGround::kCorridors,
          {simulation::TerrainCorridor::create("road", provenance::kAuthoredRoadHalfWidth,
                                               course.points)},
          {});
      const auto* authored = terrain.find_corridor("road");
      REQUIRE(authored != nullptr);
      CHECK(authored->half_width() == 80.0);
      CHECK(std::vector(authored->points().begin(), authored->points().end()) == course.points);

      const auto& begin = course.points.front();
      const auto& end = course.points.back();
      const auto middle = point((begin.x() + end.x()) * 0.5, (begin.y() + end.y()) * 0.5);
      CHECK(simulation::terrain_supports_point(terrain, middle));
      CHECK(simulation::terrain_supports_disc(terrain, middle, 80.0));
      const auto clearance = simulation::disc_clearance(terrain, middle);
      REQUIRE(clearance);
      CHECK(simulation::terrain_supports_point(terrain, clearance->point));
      CHECK(clearance->distance ==
            Catch::Approx(80.0 + simulation::kPositionTolerance).epsilon(0.0).margin(1e-10));

      // Query the authored side/cap join from its outward normal, not an easier replacement map.
      const double dx = end.x() - begin.x();
      const double dy = end.y() - begin.y();
      const double length = std::sqrt(dx * dx + dy * dy);
      const auto outside =
          point(begin.x() - 100.0 * (dy / length), begin.y() + 100.0 * (dx / length));
      REQUIRE_FALSE(simulation::terrain_supports_point(terrain, outside));
      const auto nearest = simulation::nearest_supported_point(terrain, outside);
      REQUIRE(nearest);
      CHECK(simulation::terrain_supports_point(terrain, nearest->point));
      CHECK(nearest->distance ==
            Catch::Approx(20.0 - simulation::kPositionTolerance).epsilon(0.0).margin(1e-10));
    }
  }
}

TEST_CASE("terrain provenance admits original clipped corner width ten and radius fifteen",
          "[unit][simulation][terrain][terrain_provenance][terrain_provenance_admission]") {
  const auto terrain = provenance::original_clipped_corner();
  REQUIRE(terrain.corridors().size() == 1);
  REQUIRE(terrain.holes().size() == 1);
  CHECK(terrain.corridors().front().half_width() == 10.0);
  CHECK(terrain.holes().front().radius() == 15.0);
  for (const auto& probe : provenance::original_clipped_corner_probes()) {
    INFO(probe.point.x() << "," << probe.point.y());
    CHECK(simulation::terrain_supports_point(terrain, probe.point) == probe.supported);
  }

  const auto nearest = simulation::nearest_supported_point(terrain, point(50.0, 11.0));
  REQUIRE(nearest);
  CHECK(simulation::terrain_supports_point(terrain, nearest->point));
  CHECK(nearest->point.x() == 50.0);
  CHECK(nearest->point.y() ==
        Catch::Approx(10.0 + simulation::kPositionTolerance).epsilon(0.0).margin(1e-12));
  const auto clearance = simulation::disc_clearance(terrain, point(50.0, 5.0));
  REQUIRE(clearance);
  CHECK(clearance->distance == 5.0);
  CHECK(simulation::terrain_supports_disc(terrain, point(50.0, 5.0), 5.0));
  CHECK_FALSE(simulation::terrain_supports_disc(terrain, point(50.0, 5.0), 5.01));
}

TEST_CASE("terrain provenance preserves an unrelated crossing at an authored side cap join",
          "[unit][simulation][terrain][terrain_provenance]") {
  const auto terrain = provenance::unrelated_crossing_at_join();
  for (const auto& probe : provenance::crossing_probes()) {
    INFO(probe.point.x() << "," << probe.point.y());
    CHECK(simulation::terrain_supports_point(terrain, probe.point) == probe.supported);
  }
  // The hole's independent constraint must survive at the road's known smooth join. From the
  // hole center every supported point is at least radius ten away, and this contact attains it.
  const auto nearest =
      simulation::nearest_supported_point(terrain, provenance::crossing_hole_center());
  REQUIRE(nearest);
  CHECK(nearest->point == provenance::crossing_contact());
  CHECK(nearest->distance == 10.0);
  const auto clearance = simulation::disc_clearance(terrain, provenance::crossing_contact());
  REQUIRE(clearance);
  CHECK(clearance->distance == 0.0);
}

TEST_CASE("terrain provenance keeps different curvature tangent branches and their supported cusp",
          "[unit][simulation][terrain][terrain_provenance]") {
  const auto terrain = provenance::different_curvature_tangent();
  for (const auto& probe : provenance::curvature_probes()) {
    INFO(probe.point.x() << "," << probe.point.y());
    CHECK(simulation::terrain_supports_point(terrain, probe.point) == probe.supported);
  }
  // Equal tangent rays do not make these radius-twenty and radius-ten arcs coincident. Each has
  // exposed spans, with opposite supported sides, and the curved region between them is ground.
  const auto& boundary = simulation::detail::TerrainQueryAccess::boundary(terrain);
  CHECK(std::any_of(boundary.spans.begin(), boundary.spans.end(), [](const auto& span) {
    const auto* arc = std::get_if<simulation::detail::TerrainArcSpan>(&span.geometry);
    return arc != nullptr && arc->radius == provenance::kContactRoadRadius &&
           span.supported_side == 1;
  }));
  CHECK(std::any_of(boundary.spans.begin(), boundary.spans.end(), [](const auto& span) {
    const auto* arc = std::get_if<simulation::detail::TerrainArcSpan>(&span.geometry);
    return arc != nullptr && arc->radius == provenance::kContactHoleRadius &&
           span.supported_side == -1;
  }));
  CHECK(simulation::terrain_supports_disc(terrain, provenance::curvature_supported_cusp(), 0.01));
  const auto nearest =
      simulation::nearest_supported_point(terrain, provenance::curvature_outside());
  REQUIRE(nearest);
  CHECK(nearest->point == provenance::curvature_contact());
  CHECK(nearest->distance == 1.0);
  const auto clearance = simulation::disc_clearance(terrain, provenance::curvature_contact());
  REQUIRE(clearance);
  CHECK(clearance->distance == 0.0);
  CHECK_FALSE(simulation::terrain_supports_disc(terrain, provenance::curvature_contact(),
                                                std::numeric_limits<double>::denorm_min()));
}

TEST_CASE("a subnormal terrain boundary that collapses fails visibly during compilation",
          "[unit][simulation][terrain]") {
  CHECK_THROWS_AS(simulation::TerrainDefinition::solid(simulation::ArenaBounds::create(
                      std::numeric_limits<double>::denorm_min(), 10.0)),
                  simulation::SimulationValidationError);
}
