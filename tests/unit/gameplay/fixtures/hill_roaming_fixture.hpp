#ifndef BLOB_ROYALE_TESTING_HILL_ROAMING_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_HILL_ROAMING_FIXTURE_HPP

#include "king_of_the_hill/king_of_the_hill_configuration.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace blob_royale::testing::hill_roaming_fixture {

struct Point final {
  double x;
  double y;
};

struct BoundaryCase final {
  std::string_view name;
  Point center;
  Point velocity;
  Point bounds;
  Point expected_center;
  Point expected_velocity;
};

// Velocities 400 and 200 yield exact unit and half-unit displacements at the canonical tick.
// Overshoot fixtures distinguish cancelling the WHOLE axis from advancing partway to the wall.
inline constexpr std::array kBoundaryCases{
    BoundaryCase{"right_overshoot_slides_up",
                 {9.5, 4.0},
                 {400.0, 200.0},
                 {10.0, 10.0},
                 {9.5, 4.5},
                 {0.0, 200.0}},
    BoundaryCase{"left_overshoot_slides_down",
                 {0.5, 4.0},
                 {-400.0, -200.0},
                 {10.0, 10.0},
                 {0.5, 3.5},
                 {0.0, -200.0}},
    BoundaryCase{"upper_overshoot_slides_right",
                 {4.0, 9.5},
                 {200.0, 400.0},
                 {10.0, 10.0},
                 {4.5, 9.5},
                 {200.0, 0.0}},
    BoundaryCase{"lower_overshoot_slides_left",
                 {4.0, 0.5},
                 {-200.0, -400.0},
                 {10.0, 10.0},
                 {3.5, 0.5},
                 {-200.0, 0.0}},
    BoundaryCase{
        "exact_right_arrival", {9.0, 4.0}, {400.0, 200.0}, {10.0, 10.0}, {10.0, 4.5}, {0.0, 200.0}},
    BoundaryCase{
        "exact_left_arrival", {1.0, 4.0}, {-400.0, 200.0}, {10.0, 10.0}, {0.0, 4.5}, {0.0, 200.0}},
    BoundaryCase{
        "exact_upper_arrival", {4.0, 9.0}, {200.0, 400.0}, {10.0, 10.0}, {4.5, 10.0}, {200.0, 0.0}},
    BoundaryCase{
        "exact_lower_arrival", {4.0, 1.0}, {200.0, -400.0}, {10.0, 10.0}, {4.5, 0.0}, {200.0, 0.0}},
    BoundaryCase{
        "left_edge_inward", {0.0, 4.0}, {400.0, 0.0}, {10.0, 10.0}, {1.0, 4.0}, {400.0, 0.0}},
    BoundaryCase{
        "right_edge_inward", {10.0, 4.0}, {-400.0, 0.0}, {10.0, 10.0}, {9.0, 4.0}, {-400.0, 0.0}},
    BoundaryCase{
        "left_edge_outward", {0.0, 4.0}, {-400.0, 200.0}, {10.0, 10.0}, {0.0, 4.5}, {0.0, 200.0}},
    BoundaryCase{
        "upper_edge_outward", {4.0, 10.0}, {200.0, 400.0}, {10.0, 10.0}, {4.5, 10.0}, {200.0, 0.0}},
    BoundaryCase{
        "right_edge_tangent", {10.0, 4.0}, {0.0, 400.0}, {10.0, 10.0}, {10.0, 5.0}, {0.0, 400.0}},
    BoundaryCase{"corner_stops_both_axes",
                 {10.0, 10.0},
                 {400.0, 400.0},
                 {10.0, 10.0},
                 {10.0, 10.0},
                 {0.0, 0.0}},
    BoundaryCase{"corner_preserves_inward_axis",
                 {10.0, 10.0},
                 {400.0, -400.0},
                 {10.0, 10.0},
                 {10.0, 9.0},
                 {0.0, -400.0}},
    BoundaryCase{"tiny_rectangle_cancels_without_radius_requirement",
                 {0x1p-31, 0x1p-31},
                 {1.0, 1.0},
                 {0x1p-30, 0x1p-30},
                 {0x1p-31, 0x1p-31},
                 {0.0, 0.0}},
    BoundaryCase{"maximum_velocity_proposal_is_finite_before_cancellation",
                 {999'999'999.0, 0.0},
                 {1e12, -1e12},
                 {1e9, 1e9},
                 {999'999'999.0, 0.0},
                 {0.0, 0.0}},
    BoundaryCase{"rounded_outward_edge_identity_still_cancels_velocity",
                 {10.0, 4.0},
                 {0x1p-100, 200.0},
                 {10.0, 10.0},
                 {10.0, 4.5},
                 {0.0, 200.0}},
    BoundaryCase{"rounded_inward_edge_identity_retains_inward_velocity",
                 {10.0, 4.0},
                 {-0x1p-100, 200.0},
                 {10.0, 10.0},
                 {10.0, 4.5},
                 {-0x1p-100, 200.0}}};

struct InteriorCase final {
  std::string_view name;
  Point center;
  Point velocity;
};

inline constexpr Point kLargeBounds{1e9, 1e9};
inline constexpr Point kNearMaximumInteriorCenter{999'999'999.0, 999'999'999.0};
inline constexpr std::array kInteriorCases{
    InteriorCase{"fractional_positive_negative", {100'000'000.25, 23'456.125}, {13.37, -29.875}},
    InteriorCase{"fractional_negative_positive", {123.75, 234.125}, {-70.125, 53.375}},
    InteriorCase{"canonical_zero", {123.75, 234.125}, {-0.0, 0.0}}};

inline constexpr Point kValidationBounds{10.0, 10.0};
inline constexpr std::array kOutsideCenters{Point{-1.0, 5.0}, Point{11.0, 5.0}, Point{5.0, -1.0},
                                            Point{5.0, 11.0}};
inline constexpr Point kZero{0.0, 0.0};

struct SamplingCase final {
  std::string_view name;
  double speed_minimum;
  double speed_maximum;
  double retarget_minimum_seconds;
  double retarget_maximum_seconds;
};

inline constexpr SamplingCase kDefaultSampling{"default", 20.0, 70.0, 0.35, 1.2};
inline constexpr SamplingCase kMinimumSingleton{"minimum_singleton", 0x1p-10, 0x1p-10, 0.0025,
                                                0.0025};
inline constexpr SamplingCase kMaximumSingleton{"maximum_singleton", 1e6, 1e6, 3600.0, 3600.0};
inline constexpr SamplingCase kFullSamplingRange{"full_range", 0x1p-10, 1e6, 0.0025, 3600.0};
inline constexpr SamplingCase kAdjacentSpeeds{"adjacent_speeds", 1.0, 0x1.0000000000001p+0, 0.0025,
                                              0.02};
inline constexpr std::array kSamplingCases{kDefaultSampling, kMinimumSingleton, kMaximumSingleton,
                                           kFullSamplingRange, kAdjacentSpeeds};
inline constexpr std::array<std::uint64_t, 6> kSeeds{
    0, 1, 7, 42, 1'234'567, std::numeric_limits<std::uint64_t>::max()};
inline constexpr std::size_t kSamplesPerSeed = 64;
inline constexpr std::uint64_t kMinimumCallsPerSelection = 4;
// A test-only relative allowance for the final normalized vector's computed norm. Production
// bounds constrain the selected scalar and use no epsilon or retry to repair the realization.
inline constexpr double kRelativeSpeedRoundoff = 16.0 * std::numeric_limits<double>::epsilon();

// The first and third raw draws are frozen in deterministic_random_tests.cpp. The second raw
// draw ends in 0xF4, so its quadrant is zero. These pin sampling arithmetic independently of the
// sampler's access to the generator without introducing another random implementation.
inline constexpr std::uint64_t kSeedZeroFirstDraw = 0xE220'A839'7B1D'CDAFULL;
inline constexpr std::uint64_t kSeedZeroThirdDraw = 0x06C4'5D18'8009'454FULL;
// These are -gamma and -4*gamma modulo 2^64. SplitMix's finalizer maps zero to zero:
// the first makes t exactly zero; the second makes the fourth (interval) raw draw zero.
inline constexpr std::uint64_t kCardinalHeadingSeed = 0x61C8'8646'80B5'83EBULL;
inline constexpr std::uint64_t kIntervalRejectionSeed = 0x8722'191A'02D6'0FACULL;
inline constexpr std::uint64_t kIntervalRejectionDrawCount = 5;

[[nodiscard]] inline gameplay::KingOfTheHillConfiguration configuration(const SamplingCase& input) {
  auto section = gameplay::KingOfTheHillConfiguration::default_section();
  section.hill_motion = gameplay::KingOfTheHillConfiguration::HillMotionPolicy::kRandomRoam;
  section.hill_speed_minimum = input.speed_minimum;
  section.hill_speed_maximum = input.speed_maximum;
  section.hill_retarget_minimum_seconds = input.retarget_minimum_seconds;
  section.hill_retarget_maximum_seconds = input.retarget_maximum_seconds;
  return gameplay::KingOfTheHillConfiguration::create(section);
}

} // namespace blob_royale::testing::hill_roaming_fixture

#endif
