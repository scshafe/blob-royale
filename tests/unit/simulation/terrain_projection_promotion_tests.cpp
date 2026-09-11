#include "terrain_queries.hpp"

#include "simulation_validation_error.hpp"
#include "terrain_projection_detail.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

struct FrozenProjection final {
  double x;
  double y;
  double distance;
};

// Frozen f837061 racer projection arithmetic, retaining raw cx/cy so the distance reader's wider
// standalone domain can be proved too. The controller proof separately freezes the full original
// point materialization and decision path. Never implement this reference using the new query.
[[nodiscard]] FrozenProjection frozen_projection(const std::span<const simulation::Vector2> track,
                                                 const simulation::Vector2& position) {
  FrozenProjection nearest{track.front().x(), track.front().y(),
                           std::numeric_limits<double>::infinity()};
  for (std::size_t index = 1; index < track.size(); ++index) {
    const simulation::Vector2& a = track[index - 1];
    const simulation::Vector2& b = track[index];
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double wx = position.x() - a.x();
    const double wy = position.y() - a.y();
    double t = (wx * dx + wy * dy) / (dx * dx + dy * dy);
    if (t < 0.0) {
      t = 0.0;
    } else if (t > 1.0) {
      t = 1.0;
    }
    const double cx = a.x() + (dx * t);
    const double cy = a.y() + (dy * t);
    const double ex = position.x() - cx;
    const double ey = position.y() - cy;
    const double distance = std::sqrt(ex * ex + ey * ey);
    if (distance < nearest.distance) {
      nearest = {cx, cy, distance};
    }
  }
  return nearest;
}

[[nodiscard]] simulation::Vector2 point(const double x, const double y) {
  return simulation::Vector2::create(x, y);
}

void check_bits(const double actual, const double expected) {
  CHECK(std::bit_cast<std::uint64_t>(actual) == std::bit_cast<std::uint64_t>(expected));
}

void check_projection(const simulation::TerrainCorridor& corridor,
                      const simulation::Vector2& position) {
  const auto expected = frozen_projection(corridor.points(), position);
  const auto raw = simulation::detail::project_corridor_raw(corridor, position);
  check_bits(raw.x, expected.x);
  check_bits(raw.y, expected.y);
  check_bits(raw.distance, expected.distance);
  const auto actual = simulation::corridor_project_to_centreline(corridor, position);
  check_bits(actual.point.x(), expected.x);
  check_bits(actual.point.y(), expected.y);
  check_bits(actual.distance, expected.distance);
  check_bits(simulation::corridor_distance_to_centreline(corridor, position), expected.distance);
}

} // namespace

TEST_CASE("terrain projection promotion preserves frozen point and distance bits after delegation",
          "[unit][simulation][terrain][promotion]") {
  struct Shape final {
    std::string_view name;
    std::vector<simulation::Vector2> points;
  };
  const std::vector<Shape> shapes{
      {"straight", {point(100.0, 320.0), point(800.0, 320.0)}},
      {"vertical", {point(320.0, 100.0), point(320.0, 560.0)}},
      {"bent", {point(100.0, 320.0), point(500.0, 320.0), point(500.0, 560.0)}},
      {"reversed_bend", {point(500.0, 560.0), point(500.0, 320.0), point(100.0, 320.0)}},
      {"sloped", {point(100.0, 100.0), point(500.0, 400.0)}},
      {"fractional", {point(100.125, 100.375), point(700.625, 500.875)}},
      {"signed", {point(-700.625, -500.875), point(-100.125, 100.375)}}};
  const std::vector<simulation::Vector2> positions{
      point(200.0, 320.0),
      point(200.0, 380.0),
      point(60.0, 370.0),
      point(840.0, 370.0),
      point(450.0, 370.0),
      point(450.0, std::nextafter(370.0, 0.0)),
      point(450.0, std::nextafter(370.0, std::numeric_limits<double>::infinity())),
      point(340.1875, 370.3125),
      point(0.0, 0.0),
      point(-900.25, -600.5),
      point(1e9, 1e9),
      point(-1e9, -1e9)};
  for (const auto& shape : shapes) {
    INFO(shape.name);
    const auto corridor = simulation::TerrainCorridor::create("proof", 80.0, shape.points);
    const auto before = corridor;
    for (const auto& position : positions) {
      INFO(position.x() << "," << position.y());
      check_projection(corridor, position);
    }
    for (const auto& node : corridor.points()) {
      check_projection(corridor, node);
    }
    // Ordered grid exercises non-dyadic parameters without any randomized or tolerance oracle.
    for (double x = -20.25; x <= 980.0; x += 37.25) {
      for (double y = -20.5; y <= 660.0; y += 29.5) {
        check_projection(corridor, point(x, y));
      }
    }
    CHECK(corridor == before);
  }
}

TEST_CASE("terrain projection exact and adjacent ties retain authored segment priority",
          "[unit][simulation][terrain][promotion]") {
  const auto bent = simulation::TerrainCorridor::create(
      "bend", 80.0, {point(100.0, 320.0), point(500.0, 320.0), point(500.0, 560.0)});
  const auto exact = simulation::corridor_project_to_centreline(bent, point(450.0, 370.0));
  check_bits(exact.point.x(), 450.0);
  check_bits(exact.point.y(), 320.0);
  check_bits(exact.distance, 50.0);
  const auto below =
      simulation::corridor_project_to_centreline(bent, point(450.0, std::nextafter(370.0, 0.0)));
  CHECK(below.point.y() == 320.0);
  const auto above = simulation::corridor_project_to_centreline(
      bent, point(450.0, std::nextafter(370.0, std::numeric_limits<double>::infinity())));
  CHECK(above.point.x() == 500.0);
  const auto reversed = simulation::TerrainCorridor::create(
      "reversed", 80.0, {point(500.0, 560.0), point(500.0, 320.0), point(100.0, 320.0)});
  const auto reversed_tie =
      simulation::corridor_project_to_centreline(reversed, point(450.0, 370.0));
  check_bits(reversed_tie.point.x(), 500.0);
  check_bits(reversed_tie.point.y(), 370.0);
  check_bits(reversed_tie.distance, 50.0);
}

TEST_CASE("terrain projection endpoint clamps and zero distance keep literal point bits",
          "[unit][simulation][terrain][promotion]") {
  const auto corridor = simulation::TerrainCorridor::create(
      "straight", 80.0, {point(100.0, 320.0), point(800.0, 320.0)});
  for (const auto& [query, expected] :
       std::vector<std::pair<simulation::Vector2, simulation::Vector2>>{
           {point(0.0, 320.0), point(100.0, 320.0)},
           {point(900.0, 320.0), point(800.0, 320.0)},
           {point(300.0, 320.0), point(300.0, 320.0)}}) {
    const auto projection = simulation::corridor_project_to_centreline(corridor, query);
    check_bits(projection.point.x(), expected.x());
    check_bits(projection.point.y(), expected.y());
    check_bits(projection.distance, query == expected ? 0.0 : 100.0);
  }
}

TEST_CASE("terrain projection owns its point after a temporary corridor is destroyed",
          "[unit][simulation][terrain][promotion]") {
  const auto projection = simulation::corridor_project_to_centreline(
      simulation::TerrainCorridor::create("temporary", 80.0,
                                          {point(100.0, 320.0), point(800.0, 320.0)}),
      point(450.0, 370.0));
  check_bits(projection.point.x(), 450.0);
  check_bits(projection.point.y(), 320.0);
  check_bits(projection.distance, 50.0);
}

TEST_CASE("terrain distance promotion preserves the standalone signed extreme noexcept domain",
          "[unit][simulation][terrain][promotion]") {
  const auto endpoint = point(1e12, 0.0);
  const auto corridor = simulation::TerrainCorridor::create(
      "signed_extreme", 1.0, {point(std::nextafter(-1e12, 0.0), 0.0), endpoint});
  static_assert(noexcept(simulation::corridor_distance_to_centreline(corridor, endpoint)));
  const auto expected = frozen_projection(corridor.points(), endpoint);
  const auto raw = simulation::detail::project_corridor_raw(corridor, endpoint);
  check_bits(raw.x, expected.x);
  check_bits(raw.y, expected.y);
  check_bits(raw.distance, expected.distance);
  CHECK(expected.x == std::nextafter(1e12, std::numeric_limits<double>::infinity()));
  check_bits(expected.distance, 0x1p-13);
  check_bits(simulation::corridor_distance_to_centreline(corridor, endpoint), expected.distance);
  // The new point-valued capability may refuse a coordinate the old distance reader never built.
  // Later distance delegation must read the raw core, not this throwing point materialization.
  try {
    static_cast<void>(simulation::corridor_project_to_centreline(corridor, endpoint));
    FAIL("an out-of-range projected Vector2 must fail visibly");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kPhysicalScalarOutOfRange);
    CHECK(error.context() == "vector2.create.x");
  }
}
