#ifndef BLOB_ROYALE_TESTING_LOCOMOTION_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_LOCOMOTION_FIXTURE_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace blob_royale::testing::locomotion_fixture {

struct Point final {
  double x;
  double y;
};

struct CapCase final {
  std::string_view name;
  Point velocity;
  Point acceleration;
  double ceiling;
};

inline constexpr std::array kUncappedCases{
    CapCase{"rest", {0.0, 0.0}, {400.0, 0.0}, 600.0},
    CapCase{"analog", {123.25, -21.5}, {100.0, -300.0}, 600.0},
    CapCase{"exact_endpoint_boundary", {0.0, 0.0}, {400.0, 0.0}, 1.0},
    CapCase{"reverse_braking", {600.0, 0.0}, {-400.0, 0.0}, 600.0},
    CapCase{"overspeed_braking", {1'200.0, 0.0}, {-400.0, 0.0}, 600.0},
    CapCase{"lowered_limit_coast", {900.0, -1'200.0}, {0.0, 0.0}, 1.0},
    CapCase{"overspeed_diagonal_braking", {3.0, 4.0}, {-400.0, -400.0}, 1.0}};

inline constexpr std::array kConstrainedCases{
    CapCase{"crossing_from_below", {0.5, 0.0}, {400.0, 0.0}, 1.0},
    CapCase{"crossing_from_rest", {0.0, 0.0}, {800.0, 800.0}, 1.0},
    CapCase{"perpendicular_at_ceiling", {600.0, 0.0}, {0.0, 400.0}, 600.0},
    CapCase{"perpendicular_after_lowering", {1'200.0, 0.0}, {0.0, 400.0}, 100.0},
    CapCase{"reverse_crosses_opposite_boundary", {1.0, 0.0}, {-10'000.0, 0.0}, 1.0},
    CapCase{"fractional_turn", {360.0, 480.0}, {-320.0, 240.0}, 600.0},
    CapCase{"negative_turn", {-360.0, -480.0}, {320.0, -240.0}, 600.0}};

// Independently derived binary64 witness: the first radial target is one ULP above each current
// component, but the next enumerated radial target is the current velocity itself. It is an
// ordinary admitted input rejected by the selected rounding policy, not invalid tuning.
inline constexpr Point kLaterIdentityVelocity{0x1.8000000000003p+0, 0x1.8000000000003p+0};
inline constexpr Point kLaterIdentityAcceleration{0x1.2c00000000003p+9, 0x1.2c00000000003p+9};
inline constexpr Point kLaterIdentityEndpoint{0x1.8000000000004p+1, 0x1.8000000000004p+1};
inline constexpr Point kLaterIdentityFirstTarget{0x1.8000000000004p+0, 0x1.8000000000004p+0};
inline constexpr double kLaterIdentityBoundSquared = 0x1.2000000000005p+2;
inline constexpr double kLaterIdentityEndpointSquared = 0x1.2000000000006p+4;
inline constexpr double kLaterIdentityBoundRoot = 0x1.0f876ccdf6cdcp+1;
inline constexpr double kLaterIdentityFirstTargetSquared = 0x1.2000000000006p+2;
inline constexpr std::uint64_t kLaterIdentityNormAttempts = 9;

inline constexpr double kTrajectoryAcceleration = 400.0;
inline constexpr double kTrajectoryCeiling = 600.0;
inline constexpr double kLoweredTrajectoryCeiling = 150.0;
inline constexpr std::uint64_t kHeldTrajectoryTicks = 2'400;
inline constexpr double kTrajectorySteadySpeedMargin = 1.0;
inline constexpr std::array kTrajectoryDragValues{0.0, 2.0};

struct HeldTrajectory final {
  std::string_view name;
  Point direction;
  Point initial_velocity{0.0, 0.0};
};

inline constexpr std::array kHeldTrajectories{
    HeldTrajectory{"positive_x", {1.0, 0.0}},
    HeldTrajectory{"negative_x", {-1.0, 0.0}},
    HeldTrajectory{"positive_y", {0.0, 1.0}},
    HeldTrajectory{"negative_y", {0.0, -1.0}},
    HeldTrajectory{"positive_diagonal", {1.0, 1.0}},
    HeldTrajectory{"negative_diagonal", {-1.0, -1.0}},
    HeldTrajectory{"mixed_diagonal", {1.0, -1.0}},
    HeldTrajectory{"other_mixed_diagonal", {-1.0, 1.0}},
    HeldTrajectory{"unit_oblique", {0.6, 0.8}},
    HeldTrajectory{"analog_oblique", {0.25, -0.75}},
    HeldTrajectory{"analog_half_axis", {0.5, 0.0}},
    HeldTrajectory{"initial_ceiling", {1.0, 0.0}, {600.0, 0.0}},
    HeldTrajectory{"external_overspeed", {1.0, 0.0}, {1'200.0, 0.0}}};

struct TrajectoryLeg final {
  std::string_view name;
  std::uint64_t ticks;
  std::optional<Point> new_direction;
  double ceiling;
};

// The last leg deliberately has no command: lowering the ceiling recomputes acceleration from
// the exact already-held intent. At zero drag it cannot delete existing overspeed momentum.
inline constexpr std::array kChangingTrajectory{
    TrajectoryLeg{"reach_and_hold", 1'200, Point{1.0, 0.0}, kTrajectoryCeiling},
    TrajectoryLeg{"perpendicular_turn", 1'200, Point{0.0, 1.0}, kTrajectoryCeiling},
    TrajectoryLeg{"reverse_and_hold", 1'600, Point{0.0, -1.0}, kTrajectoryCeiling},
    TrajectoryLeg{"lower_ceiling_while_held", 2'400, std::nullopt, kLoweredTrajectoryCeiling}};

} // namespace blob_royale::testing::locomotion_fixture

#endif
