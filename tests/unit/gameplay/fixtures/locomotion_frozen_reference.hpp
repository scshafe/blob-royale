#ifndef BLOB_ROYALE_TESTING_LOCOMOTION_FROZEN_REFERENCE_HPP
#define BLOB_ROYALE_TESTING_LOCOMOTION_FROZEN_REFERENCE_HPP

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>

namespace blob_royale::testing::locomotion_reference {

struct Point final {
  double x;
  double y;
};

[[nodiscard]] inline Point point(const double x, const double y) {
  return {x == 0.0 ? 0.0 : x, y == 0.0 ? 0.0 : y};
}

// Frozen from e4fad4b:src/gameplay/shared/thrust_steering_system.cpp. No production normalization,
// vector arithmetic, tuning limits, or cap helper participates in this expected-value owner.
[[nodiscard]] inline Point steered_acceleration(const Point direction, const double maximum) {
  const double x = direction.x;
  const double y = direction.y;
  const double magnitude = std::sqrt((x * x) + (y * y));
  const double scale = magnitude <= 1.0 ? 1.0 : 1.0 / magnitude;
  return point((x * scale) * maximum, (y * scale) * maximum);
}

inline constexpr double kPhysicalComponentLimit = 1'000'000'000'000.0;
inline constexpr double kDeltaSeconds = 1.0 / 400.0;
inline constexpr double kSequenceAcceleration = 4'000.0;
inline constexpr double kInactiveCeiling = 10'000.0;
inline constexpr Point kInitialPosition{480.0, 320.0};
inline constexpr Point kInitialVelocity{0.125, -0.25};
inline constexpr Point kAuthoredAcceleration{0.375, -0.625};

struct DirectionCase final {
  std::string_view name;
  Point direction;
};

[[nodiscard]] inline auto directions() {
  constexpr double tiny = std::numeric_limits<double>::denorm_min();
  return std::array{
      DirectionCase{"coast", {0.0, 0.0}},
      DirectionCase{"signed_zero", {-0.0, -0.0}},
      DirectionCase{"positive_axis", {1.0, 0.0}},
      DirectionCase{"negative_axis", {0.0, -1.0}},
      DirectionCase{"positive_diagonal", {1.0, 1.0}},
      DirectionCase{"negative_diagonal", {-1.0, -1.0}},
      DirectionCase{"mixed_diagonal", {-1.0, 1.0}},
      DirectionCase{"analog", {0.25, -0.75}},
      DirectionCase{"unit_circle", {0.6, 0.8}},
      DirectionCase{"inside_unit_circle", {0.6, std::nextafter(0.8, 0.0)}},
      DirectionCase{"outside_unit_circle", {0.6, std::nextafter(0.8, 1.0)}},
      DirectionCase{"inside_axis", {std::nextafter(1.0, 0.0), 0.0}},
      DirectionCase{"outside_axis", {std::nextafter(1.0, 2.0), 0.0}},
      DirectionCase{"subnormal_axis", {tiny, 0.0}},
      DirectionCase{"subnormal_diagonal", {-tiny, tiny}},
      DirectionCase{"mixed_scale", {tiny, 1.0}},
      DirectionCase{"legacy_large_axis", {kPhysicalComponentLimit, 0.0}},
      DirectionCase{"legacy_large_diagonal", {kPhysicalComponentLimit, -kPhysicalComponentLimit}}};
}

// Negative and very large scalars are standalone legacy arithmetic witnesses, not newly legal
// MovementTuning settings. Include final materialization failures instead of shrinking the proof.
inline constexpr std::array kScalars{0.0,
                                     -0.0,
                                     1.0,
                                     400.0,
                                     4'000.0,
                                     10'000.0,
                                     -400.0,
                                     kPhysicalComponentLimit,
                                     std::numeric_limits<double>::max()};

struct CommandStep final {
  std::string_view name;
  std::optional<Point> direction;
};

inline constexpr std::array kCommandSequence{
    CommandStep{"authored_acceleration_without_intent", std::nullopt},
    CommandStep{"diagonal_command", Point{1.0, 1.0}},
    CommandStep{"held_diagonal", std::nullopt},
    CommandStep{"analog_replacement", Point{0.25, -0.75}},
    CommandStep{"held_analog", std::nullopt},
    CommandStep{"explicit_coast", Point{0.0, -0.0}},
    CommandStep{"held_coast", std::nullopt},
    CommandStep{"negative_replacement", Point{-1.0, -1.0}},
    CommandStep{"held_negative", std::nullopt}};

struct Body final {
  Point position{kInitialPosition};
  Point velocity{kInitialVelocity};
  Point acceleration{kAuthoredAcceleration};
};

// Frozen literal old zero-drag phase1/position recurrence for this collision-free sequence.
// This is a test oracle, never an alternate runtime integration path.
inline void advance(Body& body, const CommandStep& step) {
  if (step.direction) {
    body.acceleration = steered_acceleration(*step.direction, kSequenceAcceleration);
  }
  body.velocity = point(body.velocity.x + (body.acceleration.x * kDeltaSeconds),
                        body.velocity.y + (body.acceleration.y * kDeltaSeconds));
  body.position = point(body.position.x + (body.velocity.x * kDeltaSeconds),
                        body.position.y + (body.velocity.y * kDeltaSeconds));
}

} // namespace blob_royale::testing::locomotion_reference

#endif
