#ifndef BLOB_ROYALE_TESTS_UNIT_PROTOCOL_FIXTURES_HILL_MOTION_ENCODING_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_PROTOCOL_FIXTURES_HILL_MOTION_ENCODING_FIXTURE_HPP

#include "../protocol_v3_test_fixture.hpp"
#include "simulation_limits.hpp"

#include <array>
#include <string_view>

namespace blob_royale::protocol::hill_motion_fixture {

struct VelocityCase final {
  std::string_view name;
  double x;
  double y;
};

inline constexpr double kRadius = 90.0;
inline constexpr std::uint64_t kPointsToWin = 30;
inline constexpr VelocityCase kGoldenVelocity{"golden signed velocity", 30.0, -40.0};
inline constexpr std::array kVelocityCases{
    kGoldenVelocity, VelocityCase{"zero at boundary", 0.0, 0.0},
    VelocityCase{"below sampled minimum", 0.000001, 0.0},
    VelocityCase{"inclusive vector bounds", simulation::kMaximumPhysicalComponentMagnitude,
                 -simulation::kMaximumPhysicalComponentMagnitude}};

[[nodiscard]] inline simulation::HillMotion private_motion(const VelocityCase& input) {
  return simulation::HillMotion{
      simulation::Vector2::create(input.x, input.y),
      simulation::HillMotionSchedule{simulation::TickSequence::create(140),
                                     simulation::RandomStreamKind::kHill}};
}

[[nodiscard]] inline simulation::WorldSnapshot snapshot(const VelocityCase& input) {
  return v3_test_fixture::hill_mode_snapshot(kRadius, kPointsToWin, private_motion(input));
}

} // namespace blob_royale::protocol::hill_motion_fixture

#endif
