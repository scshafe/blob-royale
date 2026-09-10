#include "swept_geometry.hpp"

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::Vector2 point(const double x, const double y) {
  return simulation::Vector2::create(x, y);
}

void check_roots(const simulation::SweptBoundaryRoots& roots, const double first,
                 const double second) {
  REQUIRE(roots.size() == 2);
  REQUIRE(roots.first().has_value());
  CHECK(roots.first()->value() == first);
  CHECK(roots.times()[0].value() == first);
  CHECK(roots.times()[1].value() == second);
}

} // namespace

TEST_CASE("swept circle keeps both entry and exit in time order",
          "[unit][simulation][swept_geometry]") {
  const auto roots = simulation::swept_circle_boundary_roots(point(-2.0, 0.0), point(4.0, 0.0),
                                                             point(0.0, 0.0), 1.0);
  check_roots(roots, 0.25, 0.75);
  const auto reversed = simulation::swept_circle_boundary_roots(point(2.0, 0.0), point(-4.0, 0.0),
                                                                point(0.0, 0.0), 1.0);
  CHECK(roots == reversed);
}

TEST_CASE("swept circle exact tangency is one root and a near miss stays a miss",
          "[unit][simulation][swept_geometry]") {
  const auto tangent = simulation::swept_circle_boundary_roots(point(-2.0, 1.0), point(4.0, 0.0),
                                                               point(0.0, 0.0), 1.0);
  REQUIRE(tangent.size() == 1);
  CHECK(tangent.first()->value() == 0.5);
  const auto miss = simulation::swept_circle_boundary_roots(point(-2.0, std::nextafter(1.0, 2.0)),
                                                            point(4.0, 0.0), point(0.0, 0.0), 1.0);
  CHECK(miss.empty());
  CHECK_FALSE(miss.first().has_value());
}

TEST_CASE("swept circle initial penetration reports exit rather than zero contact",
          "[unit][simulation][swept_geometry]") {
  const auto roots = simulation::swept_circle_boundary_roots(point(0.0, 0.0), point(2.0, 0.0),
                                                             point(0.0, 0.0), 1.0);
  REQUIRE(roots.size() == 1);
  CHECK(roots.first()->value() == 0.5);
  CHECK(simulation::swept_circle_boundary_roots(point(0.0, 0.0), point(0.5, 0.0), point(0.0, 0.0),
                                                1.0)
            .empty());
}

TEST_CASE("swept circle initial boundary retains zero for incoming and separating motion",
          "[unit][simulation][swept_geometry]") {
  const auto inward = simulation::swept_circle_boundary_roots(point(1.0, 0.0), point(-4.0, 0.0),
                                                              point(0.0, 0.0), 1.0);
  check_roots(inward, 0.0, 0.5);
  const auto outward = simulation::swept_circle_boundary_roots(point(1.0, 0.0), point(4.0, 0.0),
                                                               point(0.0, 0.0), 1.0);
  REQUIRE(outward.size() == 1);
  CHECK(outward.first()->value() == 0.0);
  CHECK_FALSE(std::signbit(outward.first()->value()));
  const auto tangent = simulation::swept_circle_boundary_roots(point(1.0, 0.0), point(0.0, 4.0),
                                                               point(0.0, 0.0), 1.0);
  REQUIRE(tangent.size() == 1);
  CHECK(tangent.first()->value() == 0.0);
  CHECK(simulation::swept_circle_boundary_roots(point(2.0, 0.0), point(1.0, 0.0), point(0.0, 0.0),
                                                1.0)
            .empty());
}

TEST_CASE("stationary circle sweeps distinguish interior exterior and boundary intervals",
          "[unit][simulation][swept_geometry]") {
  const auto boundary = simulation::swept_circle_boundary_roots(point(1.0, 0.0), point(0.0, 0.0),
                                                                point(0.0, 0.0), 1.0);
  check_roots(boundary, 0.0, 1.0);
  CHECK(simulation::swept_circle_boundary_roots(point(0.0, 0.0), point(0.0, 0.0), point(0.0, 0.0),
                                                1.0)
            .empty());
  CHECK(simulation::swept_circle_boundary_roots(point(2.0, 0.0), point(0.0, 0.0), point(0.0, 0.0),
                                                1.0)
            .empty());
}

TEST_CASE("swept zero-radius circle is a point boundary", "[unit][simulation][swept_geometry]") {
  const auto crossing = simulation::swept_circle_boundary_roots(point(-1.0, 0.0), point(2.0, 0.0),
                                                                point(0.0, 0.0), 0.0);
  REQUIRE(crossing.size() == 1);
  CHECK(crossing.first()->value() == 0.5);
  const auto stationary = simulation::swept_circle_boundary_roots(point(0.0, 0.0), point(0.0, 0.0),
                                                                  point(0.0, 0.0), 0.0);
  check_roots(stationary, 0.0, 1.0);
}

TEST_CASE("swept circle root arithmetic retains a small circle on a long radial sweep",
          "[unit][simulation][swept_geometry]") {
  const auto roots = simulation::swept_circle_boundary_roots(
      point(-100'000'000.0, 0.0), point(200'000'000.0, 0.0), point(0.0, 0.0), 1.0);
  REQUIRE(roots.size() == 2);
  CHECK(roots.times()[0].value() == Catch::Approx(0.499999995).epsilon(1e-15));
  CHECK(roots.times()[1].value() == Catch::Approx(0.500000005).epsilon(1e-15));
  CHECK(roots.times()[0] < simulation::MotionTime::create(0.5));
  CHECK(roots.times()[1] > simulation::MotionTime::create(0.5));
}

TEST_CASE("swept circle scaling preserves tiny and large exactly representable geometry",
          "[unit][simulation][swept_geometry]") {
  for (const double scale : {0x1p-500, 0x1p30}) {
    const auto roots = simulation::swept_circle_boundary_roots(
        point(-2.0 * scale, 0.0), point(4.0 * scale, 0.0), point(0.0, 0.0), scale);
    check_roots(roots, 0.25, 0.75);
  }
  CHECK_THROWS_AS(
      simulation::swept_circle_boundary_roots(point(-1.0, 0.0), point(2.0, 0.0), point(0.0, 0.0),
                                              std::numeric_limits<double>::denorm_min()),
      simulation::SimulationValidationError);
}

TEST_CASE("swept circle preserves scaled rational tangency and distinguishes adjacent radii",
          "[unit][simulation][swept_geometry]") {
  for (const double scale : {1.0, 1'000'000.0, 123'456'789.0}) {
    const auto start = point(-scale, 7.0 * scale);
    const auto displacement = point(5.0 * scale, -3.75 * scale);
    const double radius = 5.0 * scale;
    const auto tangent =
        simulation::swept_circle_boundary_roots(start, displacement, point(0.0, 0.0), radius);
    REQUIRE(tangent.size() == 1);
    CHECK(tangent.first()->value() == Catch::Approx(0.8).epsilon(1e-15));
    CHECK(simulation::swept_circle_boundary_roots(start, displacement, point(0.0, 0.0),
                                                  std::nextafter(radius, 0.0))
              .empty());
    const auto crossing = simulation::swept_circle_boundary_roots(
        start, displacement, point(0.0, 0.0), std::nextafter(radius, 2.0 * radius));
    REQUIRE(crossing.size() == 2);
    CHECK(crossing.times()[0] < crossing.times()[1]);
  }
}

TEST_CASE("swept circle exact polynomial endpoint stays one even when division would round out",
          "[unit][simulation][swept_geometry]") {
  const auto endpoint = simulation::swept_circle_boundary_roots(point(12.0, 0.0), point(-11.0, 0.0),
                                                                point(0.0, 0.0), 1.0);
  REQUIRE(endpoint.size() == 1);
  CHECK(endpoint.first()->value() == 1.0);
  const auto tangent = simulation::swept_circle_boundary_roots(point(-1.0, 7.0), point(4.0, -3.0),
                                                               point(0.0, 0.0), 5.0);
  REQUIRE(tangent.size() == 1);
  CHECK(tangent.first()->value() == 1.0);
}

TEST_CASE("swept stationary rational boundary uses the exact squared-distance sign",
          "[unit][simulation][swept_geometry]") {
  const double scale = 100'000'001.0;
  const double radius = 5.0 * scale;
  const auto start = point(3.0 * scale, 4.0 * scale);
  const auto roots =
      simulation::swept_circle_boundary_roots(start, point(0.0, 0.0), point(0.0, 0.0), radius);
  check_roots(roots, 0.0, 1.0);
  for (const double adjacent_radius :
       {std::nextafter(radius, 0.0), std::nextafter(radius, 2.0 * radius)}) {
    CHECK(simulation::swept_circle_boundary_roots(start, point(0.0, 0.0), point(0.0, 0.0),
                                                  adjacent_radius)
              .empty());
  }
}

TEST_CASE("swept circle refuses distinct crossings whose root times cannot be represented",
          "[unit][simulation][swept_geometry]") {
  CHECK_THROWS_AS(simulation::swept_circle_boundary_roots(point(-1.0, 0.0), point(2.0, 0.0),
                                                          point(0.0, 0.0), 0x1p-100),
                  simulation::SimulationValidationError);
}

TEST_CASE("swept exact endpoint refuses only an additional interior root that rounds onto it",
          "[unit][simulation][swept_geometry]") {
  constexpr double epsilon = 0x1p-52;
  CHECK_THROWS_AS(simulation::swept_circle_boundary_roots(
                      point(1.0 - epsilon, -4.0), point(epsilon, 4.0), point(0.0, 0.0), 1.0),
                  simulation::SimulationValidationError);
  const auto outside = simulation::swept_circle_boundary_roots(
      point(1.0 + epsilon, -4.0), point(-epsilon, 4.0), point(0.0, 0.0), 1.0);
  REQUIRE(outside.size() == 1);
  CHECK(outside.first()->value() == 1.0);
}

TEST_CASE("swept line owns wall times including endpoints and stationary boundary intervals",
          "[unit][simulation][swept_geometry]") {
  const auto crossing = simulation::swept_line_boundary_roots(5.0, -8.0, 1.0);
  REQUIRE(crossing.size() == 1);
  CHECK(crossing.first()->value() == 0.5);
  const auto endpoint = simulation::swept_line_boundary_roots(0.0, 1.0, 1.0);
  REQUIRE(endpoint.size() == 1);
  CHECK(endpoint.first()->value() == 1.0);
  const auto stationary = simulation::swept_line_boundary_roots(1.0, 0.0, 1.0);
  check_roots(stationary, 0.0, 1.0);
  CHECK(simulation::swept_line_boundary_roots(0.0, 0.0, 1.0).empty());
  CHECK(simulation::swept_line_boundary_roots(0.0, 1.0, std::nextafter(1.0, 2.0)).empty());
  CHECK(simulation::swept_line_boundary_roots(0.0, 1.0, std::nextafter(0.0, -1.0)).empty());
  CHECK(simulation::swept_line_boundary_roots(0.0, std::numeric_limits<double>::denorm_min(), 1.0)
            .empty());
}

TEST_CASE("swept capsule crosses side strips without admitting internal cap boundaries",
          "[unit][simulation][swept_geometry]") {
  const auto side = simulation::swept_capsule_boundary_roots(point(2.0, -2.0), point(0.0, 4.0),
                                                             point(0.0, 0.0), point(4.0, 0.0), 1.0);
  check_roots(side, 0.25, 0.75);
  const auto caps = simulation::swept_capsule_boundary_roots(point(-3.0, 0.0), point(8.0, 0.0),
                                                             point(0.0, 0.0), point(4.0, 0.0), 1.0);
  check_roots(caps, 0.25, 1.0);
  // Both endpoint circles are crossed inside the capsule, but the path never leaves its interior.
  CHECK(simulation::swept_capsule_boundary_roots(point(0.5, 0.0), point(3.0, 0.0), point(0.0, 0.0),
                                                 point(4.0, 0.0), 1.0)
            .empty());
}

TEST_CASE("swept capsule initially penetrated side reports its eventual exit",
          "[unit][simulation][swept_geometry]") {
  const auto roots = simulation::swept_capsule_boundary_roots(
      point(2.0, 0.0), point(0.0, 2.0), point(0.0, 0.0), point(4.0, 0.0), 1.0);
  REQUIRE(roots.size() == 1);
  CHECK(roots.first()->value() == 0.5);
}

TEST_CASE("swept capsule outer endcap tangent is one root", "[unit][simulation][swept_geometry]") {
  const auto roots = simulation::swept_capsule_boundary_roots(
      point(-1.0, -2.0), point(0.0, 4.0), point(0.0, 0.0), point(4.0, 0.0), 1.0);
  REQUIRE(roots.size() == 1);
  CHECK(roots.first()->value() == 0.5);
}

TEST_CASE("swept capsule boundary-parallel motion returns boundary interval endpoints",
          "[unit][simulation][swept_geometry]") {
  const auto whole_side = simulation::swept_capsule_boundary_roots(
      point(-2.0, 1.0), point(8.0, 0.0), point(0.0, 0.0), point(4.0, 0.0), 1.0);
  check_roots(whole_side, 0.25, 0.75);
  const auto starts_on_side = simulation::swept_capsule_boundary_roots(
      point(1.0, 1.0), point(6.0, 0.0), point(0.0, 0.0), point(4.0, 0.0), 1.0);
  check_roots(starts_on_side, 0.0, 0.5);
  const auto stationary = simulation::swept_capsule_boundary_roots(
      point(2.0, 1.0), point(0.0, 0.0), point(0.0, 0.0), point(4.0, 0.0), 1.0);
  check_roots(stationary, 0.0, 1.0);
}

TEST_CASE("swept capsule preserves exact rational side membership at every boundary endpoint",
          "[unit][simulation][swept_geometry]") {
  for (const double scale : {1.0, 1'000'000.0, 123'456'789.0}) {
    const auto end = point(5.0 * scale, 12.0 * scale);
    const auto on_side = point(14.5 * scale, scale);
    const auto stationary = simulation::swept_capsule_boundary_roots(
        on_side, point(0.0, 0.0), point(0.0, 0.0), end, 13.0 * scale);
    check_roots(stationary, 0.0, 1.0);
    const auto along_side = simulation::swept_capsule_boundary_roots(
        on_side, point(1.25 * scale, 3.0 * scale), point(0.0, 0.0), end, 13.0 * scale);
    check_roots(along_side, 0.0, 1.0);
    const auto arriving = simulation::swept_capsule_boundary_roots(
        point(2.5 * scale, 6.0 * scale), point(12.0 * scale, -5.0 * scale), point(0.0, 0.0), end,
        13.0 * scale);
    REQUIRE(arriving.size() == 1);
    CHECK(arriving.first()->value() == 1.0);
  }
}

TEST_CASE("swept capsule degenerate segment delegates exactly to the circle roots",
          "[unit][simulation][swept_geometry]") {
  const auto capsule = simulation::swept_capsule_boundary_roots(
      point(-2.0, 0.0), point(4.0, 0.0), point(0.0, 0.0), point(0.0, 0.0), 1.0);
  const auto circle = simulation::swept_circle_boundary_roots(point(-2.0, 0.0), point(4.0, 0.0),
                                                              point(0.0, 0.0), 1.0);
  CHECK(capsule == circle);
  const auto line = simulation::swept_capsule_boundary_roots(point(-2.0, 0.0), point(8.0, 0.0),
                                                             point(0.0, 0.0), point(4.0, 0.0), 0.0);
  check_roots(line, 0.25, 0.75);
}

TEST_CASE("swept capsule reversal and quarter-turn preserve the same boundary times",
          "[unit][simulation][swept_geometry]") {
  const auto original = simulation::swept_capsule_boundary_roots(
      point(-3.0, 0.0), point(8.0, 0.0), point(0.0, 0.0), point(4.0, 0.0), 1.0);
  const auto reversed = simulation::swept_capsule_boundary_roots(
      point(-3.0, 0.0), point(8.0, 0.0), point(4.0, 0.0), point(0.0, 0.0), 1.0);
  const auto rotated = simulation::swept_capsule_boundary_roots(
      point(0.0, -3.0), point(0.0, 8.0), point(0.0, 0.0), point(0.0, 4.0), 1.0);
  CHECK(original == reversed);
  CHECK(original == rotated);
}

TEST_CASE("swept disc boundaries use the same point roots on inflated geometry",
          "[unit][simulation][swept_geometry]") {
  const auto circle = simulation::swept_disc_circle_boundary_roots(
      point(-4.0, 0.0), point(8.0, 0.0), 1.0, point(0.0, 0.0), 1.0);
  const auto inflated = simulation::swept_circle_boundary_roots(point(-4.0, 0.0), point(8.0, 0.0),
                                                                point(0.0, 0.0), 2.0);
  CHECK(circle == inflated);
  check_roots(circle, 0.25, 0.75);
  const auto capsule = simulation::swept_disc_capsule_boundary_roots(
      point(2.0, -4.0), point(0.0, 8.0), 1.0, point(0.0, 0.0), point(4.0, 0.0), 1.0);
  check_roots(capsule, 0.25, 0.75);
}

TEST_CASE("swept geometry rejects invalid scalar inputs visibly",
          "[unit][simulation][swept_geometry]") {
  for (const double invalid :
       {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(),
        3.0 * simulation::kMaximumPhysicalComponentMagnitude}) {
    CHECK_THROWS_AS(simulation::swept_circle_boundary_roots(point(0.0, 0.0), point(1.0, 0.0),
                                                            point(0.0, 0.0), invalid),
                    simulation::SimulationValidationError);
    CHECK_THROWS_AS(simulation::swept_capsule_boundary_roots(point(0.0, 0.0), point(1.0, 0.0),
                                                             point(0.0, 0.0), point(1.0, 0.0),
                                                             invalid),
                    simulation::SimulationValidationError);
  }
  CHECK_THROWS_AS(
      simulation::swept_line_boundary_roots(0.0, std::numeric_limits<double>::quiet_NaN(), 1.0),
      simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::swept_disc_circle_boundary_roots(point(0.0, 0.0), point(1.0, 0.0),
                                                               -1.0, point(0.0, 0.0), 1.0),
                  simulation::SimulationValidationError);
}
