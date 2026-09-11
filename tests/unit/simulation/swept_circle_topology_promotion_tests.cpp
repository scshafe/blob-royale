#include "swept_geometry.hpp"

#include "simulation_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

using Relation = simulation::CircleInitialRelation;
using Topology = simulation::CircleLineTopology;
using Radial = simulation::CircleRadialMotion;

[[nodiscard]] simulation::Vector2 point(const double x, const double y) {
  return simulation::Vector2::create(x, y);
}

constexpr std::uint64_t kZero = 0x0000000000000000ULL;
constexpr std::uint64_t kQuarter = 0x3fd0000000000000ULL;
constexpr std::uint64_t kHalf = 0x3fe0000000000000ULL;
constexpr std::uint64_t kThreeQuarters = 0x3fe8000000000000ULL;
constexpr std::uint64_t kFourFifths = 0x3fe999999999999aULL;
constexpr std::uint64_t kOne = 0x3ff0000000000000ULL;

struct SweepCase final {
  std::string_view name;
  simulation::Vector2 start;
  simulation::Vector2 displacement;
  double radius;
  Relation initial_relation;
  Topology topology;
  std::vector<std::uint64_t> root_bits;
};

// Frozen input/output table for the root implementation at 638da1b. Most times are rational
// geometric roots; the long radial sweep explicitly pins the written c/q rounding (its entry is
// one ULP above the rounded mathematical root). Phase A checks these constants against the still
// untouched original API and the richer query on both lanes before delegation. After delegation,
// the constants remain an independent oracle, not wrapper-vs-delegate self-comparison.
[[nodiscard]] std::vector<SweepCase> frozen_cases() {
  return {
      {"two roots",
       point(-2.0, 0.0),
       point(4.0, 0.0),
       1.0,
       Relation::kOutside,
       Topology::kSecant,
       {kQuarter, kThreeQuarters}},
      {"clipped entry only",
       point(-2.0, 0.0),
       point(2.0, 0.0),
       1.0,
       Relation::kOutside,
       Topology::kSecant,
       {kHalf}},
      {"initial interior exit",
       point(0.0, 0.0),
       point(2.0, 0.0),
       1.0,
       Relation::kInside,
       Topology::kSecant,
       {kHalf}},
      {"interior with no clipped roots",
       point(0.0, 0.0),
       point(0.5, 0.0),
       1.0,
       Relation::kInside,
       Topology::kSecant,
       {}},
      {"outside with no clipped roots",
       point(2.0, 0.0),
       point(2.0, 0.0),
       1.0,
       Relation::kOutside,
       Topology::kSecant,
       {}},
      {"stationary inside",
       point(0.0, 0.0),
       point(0.0, 0.0),
       1.0,
       Relation::kInside,
       Topology::kStationary,
       {}},
      {"stationary outside",
       point(2.0, 0.0),
       point(0.0, 0.0),
       1.0,
       Relation::kOutside,
       Topology::kStationary,
       {}},
      {"stationary boundary",
       point(1.0, 0.0),
       point(0.0, 0.0),
       1.0,
       Relation::kOnBoundary,
       Topology::kStationary,
       {kZero, kOne}},
      {"zero scale boundary",
       point(0.0, 0.0),
       point(0.0, 0.0),
       0.0,
       Relation::kOnBoundary,
       Topology::kStationary,
       {kZero, kOne}},
      {"boundary inward",
       point(1.0, 0.0),
       point(-4.0, 0.0),
       1.0,
       Relation::kOnBoundary,
       Topology::kSecant,
       {kZero, kHalf}},
      {"boundary outward",
       point(1.0, 0.0),
       point(4.0, 0.0),
       1.0,
       Relation::kOnBoundary,
       Topology::kSecant,
       {kZero}},
      {"tangent at zero",
       point(1.0, 0.0),
       point(0.0, 4.0),
       1.0,
       Relation::kOnBoundary,
       Topology::kTangent,
       {kZero}},
      {"tangent at one",
       point(-4.0, 1.0),
       point(4.0, 0.0),
       1.0,
       Relation::kOutside,
       Topology::kTangent,
       {kOne}},
      {"entry at one with other root outside",
       point(-2.0, 0.0),
       point(1.0, 0.0),
       1.0,
       Relation::kOutside,
       Topology::kSecant,
       {kOne}},
      {"exit at one",
       point(0.0, 0.0),
       point(1.0, 0.0),
       1.0,
       Relation::kInside,
       Topology::kSecant,
       {kOne}},
      {"both endpoint roots",
       point(-1.0, 0.0),
       point(2.0, 0.0),
       1.0,
       Relation::kOnBoundary,
       Topology::kSecant,
       {kZero, kOne}},
      {"interior tangent",
       point(-2.0, 1.0),
       point(4.0, 0.0),
       1.0,
       Relation::kOutside,
       Topology::kTangent,
       {kHalf}},
      {"tangent beyond clipped interval",
       point(-2.0, 1.0),
       point(1.0, 0.0),
       1.0,
       Relation::kOutside,
       Topology::kTangent,
       {}},
      {"supporting line miss",
       point(-2.0, 2.0),
       point(4.0, 0.0),
       1.0,
       Relation::kOutside,
       Topology::kMiss,
       {}},
      {"rational rotated tangent",
       point(-1.0, 7.0),
       point(5.0, -3.75),
       5.0,
       Relation::kOutside,
       Topology::kTangent,
       {kFourFifths}},
      {"rotated endpoint tangent",
       point(-1.0, 7.0),
       point(4.0, -3.0),
       5.0,
       Relation::kOutside,
       Topology::kTangent,
       {kOne}},
      {"high speed rotated tangent",
       point(13e6, 22.25e6),
       point(-24e6, 7e6),
       25e6,
       Relation::kOutside,
       Topology::kTangent,
       {kQuarter}},
      {"scaled exact stationary boundary",
       point(300000003.0, 400000004.0),
       point(0.0, 0.0),
       500000005.0,
       Relation::kOnBoundary,
       Topology::kStationary,
       {kZero, kOne}},
      {"point radius tangent",
       point(-1.0, 0.0),
       point(2.0, 0.0),
       0.0,
       Relation::kOutside,
       Topology::kTangent,
       {kHalf}},
      {"long radial small circle",
       point(-1e8, 0.0),
       point(2e8, 0.0),
       1.0,
       Relation::kOutside,
       Topology::kSecant,
       {0x3fdffffffaa19c48ULL, 0x3fe0000002af31dcULL}},
  };
}

void check_root_bits(const simulation::SweptBoundaryRoots& actual,
                     const std::vector<std::uint64_t>& expected) {
  REQUIRE(actual.size() == expected.size());
  for (std::size_t index = 0; index < expected.size(); ++index) {
    CHECK(std::bit_cast<std::uint64_t>(actual.times()[index].value()) == expected[index]);
  }
}

struct Rejection final {
  simulation::SimulationValidationCode code;
  std::string context;
  std::string detail;

  friend bool operator==(const Rejection&, const Rejection&) = default;
};

template <typename Operation> [[nodiscard]] Rejection rejection_from(Operation&& operation) {
  std::optional<Rejection> rejection;
  try {
    static_cast<void>(std::forward<Operation>(operation)());
  } catch (const simulation::SimulationValidationError& error) {
    rejection.emplace(Rejection{error.validation_code(), error.context(), error.detail()});
  }
  REQUIRE(rejection.has_value());
  return *rejection;
}

} // namespace

TEST_CASE("circle topology promotion preserves original root bits and frozen goldens",
          "[unit][simulation][swept_geometry][promotion]") {
  for (const SweepCase& test : frozen_cases()) {
    INFO(test.name);
    const auto original = simulation::swept_circle_boundary_roots(test.start, test.displacement,
                                                                  point(0.0, 0.0), test.radius);
    const auto detailed = simulation::swept_circle_boundary_query(test.start, test.displacement,
                                                                  point(0.0, 0.0), test.radius);
    check_root_bits(original, test.root_bits);
    check_root_bits(detailed.roots, test.root_bits);
    CHECK(detailed.roots == original);
    CHECK(detailed.initial_relation == test.initial_relation);
    CHECK(detailed.topology == test.topology);
  }
}

TEST_CASE("circle topology promotion distinguishes exact tangent from adjacent hit and miss",
          "[unit][simulation][swept_geometry][promotion]") {
  const auto start = point(13e6, 22.25e6);
  const auto displacement = point(-24e6, 7e6);
  for (const auto& [radius, expected] :
       {std::pair{std::nextafter(25e6, 0.0), Topology::kMiss}, std::pair{25e6, Topology::kTangent},
        std::pair{std::nextafter(25e6, std::numeric_limits<double>::infinity()),
                  Topology::kSecant}}) {
    const auto original =
        simulation::swept_circle_boundary_roots(start, displacement, point(0.0, 0.0), radius);
    const auto detailed =
        simulation::swept_circle_boundary_query(start, displacement, point(0.0, 0.0), radius);
    CHECK(detailed.initial_relation == Relation::kOutside);
    CHECK(detailed.topology == expected);
    REQUIRE(detailed.roots.size() == original.size());
    for (std::size_t index = 0; index < original.size(); ++index) {
      CHECK(std::bit_cast<std::uint64_t>(detailed.roots.times()[index].value()) ==
            std::bit_cast<std::uint64_t>(original.times()[index].value()));
    }
    if (expected == Topology::kMiss) {
      CHECK(detailed.roots.empty());
    } else if (expected == Topology::kTangent) {
      check_root_bits(detailed.roots, {kQuarter});
    } else {
      REQUIRE(detailed.roots.size() == 2);
      CHECK(detailed.roots.times()[0].value() < 0.25);
      CHECK(detailed.roots.times()[1].value() > 0.25);
    }
  }
}

TEST_CASE("circle topology promotion exposes exact initial radial direction and coincidence",
          "[unit][simulation][swept_geometry][promotion]") {
  struct InitialMotionCase final {
    std::string_view name;
    simulation::Vector2 start;
    simulation::Vector2 displacement;
    simulation::Vector2 center;
    double radius;
    Radial radial_motion;
    bool coincident;
  };
  // These signs are independent dot-product facts, not classifications reconstructed from root
  // count, contact-normal rounding, or a positional epsilon. The tiny offsets/displacements are
  // nonzero even though each is far below the legacy contact tolerances.
  for (const InitialMotionCase& test :
       {InitialMotionCase{"outside approaching secant", point(-2.0, 0.0), point(4.0, 0.0),
                          point(0.0, 0.0), 1.0, Radial::kApproaching, false},
        InitialMotionCase{"outside receding secant", point(2.0, 0.0), point(4.0, 0.0),
                          point(0.0, 0.0), 1.0, Radial::kReceding, false},
        InitialMotionCase{"approaching supporting-line miss", point(-2.0, 2.0), point(4.0, 0.0),
                          point(0.0, 0.0), 1.0, Radial::kApproaching, false},
        InitialMotionCase{"receding supporting-line miss", point(2.0, 2.0), point(4.0, 0.0),
                          point(0.0, 0.0), 1.0, Radial::kReceding, false},
        InitialMotionCase{"endpoint tangent approach", point(-4.0, 1.0), point(4.0, 0.0),
                          point(0.0, 0.0), 1.0, Radial::kApproaching, false},
        InitialMotionCase{"endpoint secant approach", point(-2.0, 0.0), point(1.0, 0.0),
                          point(0.0, 0.0), 1.0, Radial::kApproaching, false},
        InitialMotionCase{"tiny boundary approach", point(1.0, 0.0), point(-1e-50, 0.0),
                          point(0.0, 0.0), 1.0, Radial::kApproaching, false},
        InitialMotionCase{"tiny boundary recession", point(1.0, 0.0), point(1e-50, 0.0),
                          point(0.0, 0.0), 1.0, Radial::kReceding, false},
        InitialMotionCase{"tiny boundary tangent", point(1.0, 0.0), point(0.0, 1e-50),
                          point(0.0, 0.0), 1.0, Radial::kOrthogonal, false},
        InitialMotionCase{"tiny noncoincident approach", point(1e-50, 0.0), point(-1e-50, 0.0),
                          point(0.0, 0.0), 1.0, Radial::kApproaching, false},
        InitialMotionCase{"tiny noncoincident recession", point(1e-50, 0.0), point(1e-50, 0.0),
                          point(0.0, 0.0), 1.0, Radial::kReceding, false},
        InitialMotionCase{"tiny noncoincident orthogonal motion", point(1e-50, 0.0),
                          point(0.0, 1e-50), point(0.0, 0.0), 1.0, Radial::kOrthogonal, false},
        InitialMotionCase{"noncoincident stationary inside", point(0.5, 0.0), point(0.0, 0.0),
                          point(0.0, 0.0), 1.0, Radial::kOrthogonal, false},
        InitialMotionCase{"noncoincident stationary boundary", point(1.0, 0.0), point(0.0, 0.0),
                          point(0.0, 0.0), 1.0, Radial::kOrthogonal, false},
        InitialMotionCase{"noncoincident stationary outside", point(2.0, 0.0), point(0.0, 0.0),
                          point(0.0, 0.0), 1.0, Radial::kOrthogonal, false},
        InitialMotionCase{"coincident moving at translated center", point(5.0, 7.0),
                          point(3.0, 4.0), point(5.0, 7.0), 10.0, Radial::kOrthogonal, true},
        InitialMotionCase{"coincident stationary at translated center", point(5.0, 7.0),
                          point(0.0, 0.0), point(5.0, 7.0), 10.0, Radial::kOrthogonal, true},
        InitialMotionCase{"zero-scale coincident boundary", point(0.0, 0.0), point(0.0, 0.0),
                          point(0.0, 0.0), 0.0, Radial::kOrthogonal, true},
        InitialMotionCase{"high-speed future tangent approach", point(13e6, 22.25e6),
                          point(-24e6, 7e6), point(0.0, 0.0), 25e6, Radial::kApproaching, false},
        InitialMotionCase{"high-speed revised exact tangent", point(7e6, 24e6), point(-24e6, 7e6),
                          point(0.0, 0.0), 25e6, Radial::kOrthogonal, false},
        InitialMotionCase{
            "high-speed revised approaching neighbor", point(7e6, 24e6),
            point(std::nextafter(-24e6, -std::numeric_limits<double>::infinity()), 7e6),
            point(0.0, 0.0), 25e6, Radial::kApproaching, false},
        InitialMotionCase{"high-speed revised receding neighbor", point(7e6, 24e6),
                          point(std::nextafter(-24e6, 0.0), 7e6), point(0.0, 0.0), 25e6,
                          Radial::kReceding, false}}) {
    INFO(test.name);
    const auto result = simulation::swept_circle_boundary_query(test.start, test.displacement,
                                                                test.center, test.radius);
    CHECK(result.initial_radial_motion == test.radial_motion);
    CHECK(result.initial_centers_coincident == test.coincident);
  }
}

TEST_CASE("circle topology promotion preserves translated geometry and initial signs",
          "[unit][simulation][swept_geometry][promotion]") {
  const auto crossing = simulation::swept_circle_boundary_query(point(98.0, 100.0), point(4.0, 0.0),
                                                                point(100.0, 100.0), 1.0);
  check_root_bits(crossing.roots, {kQuarter, kThreeQuarters});
  CHECK(crossing.initial_relation == Relation::kOutside);
  CHECK(crossing.topology == Topology::kSecant);
  for (const auto& [x, expected] : {std::pair{std::nextafter(1.0, 0.0), Relation::kInside},
                                    std::pair{1.0, Relation::kOnBoundary},
                                    std::pair{std::nextafter(1.0, 2.0), Relation::kOutside}}) {
    const auto result = simulation::swept_circle_boundary_query(point(x, 0.0), point(0.0, 0.0),
                                                                point(0.0, 0.0), 1.0);
    CHECK(result.initial_relation == expected);
    CHECK(result.topology == Topology::kStationary);
    if (expected == Relation::kOnBoundary) {
      check_root_bits(result.roots, {kZero, kOne});
    } else {
      CHECK(result.roots.empty());
    }
  }
}

TEST_CASE("circle topology promotion preserves visible precision and radius rejections",
          "[unit][simulation][swept_geometry][promotion][validation]") {
  struct InvalidCase final {
    simulation::Vector2 start;
    simulation::Vector2 displacement;
    double radius;
    std::string_view context;
  };
  for (const InvalidCase& test :
       {InvalidCase{point(0.0, 0.0), point(1.0, 0.0), -1.0, "swept_geometry.boundary_radius"},
        InvalidCase{point(0.0, 0.0), point(1e12, 0.0), 1e-100, "swept_geometry.radius"},
        InvalidCase{point(1.0 - 0x1p-52, -4.0), point(0x1p-52, 4.0), 1.0,
                    "swept_geometry.root_separation"}}) {
    const auto original = rejection_from([&] {
      return simulation::swept_circle_boundary_roots(test.start, test.displacement, point(0.0, 0.0),
                                                     test.radius);
    });
    const auto detailed = rejection_from([&] {
      return simulation::swept_circle_boundary_query(test.start, test.displacement, point(0.0, 0.0),
                                                     test.radius);
    });
    CHECK(original.code == simulation::SimulationValidationCode::kPhysicalScalarOutOfRange);
    CHECK(original.context == test.context);
    CHECK(detailed == original);
  }
}
