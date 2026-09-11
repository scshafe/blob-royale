#ifndef BLOB_ROYALE_TESTING_HAZARD_STREAM_FROZEN_REFERENCE_HPP
#define BLOB_ROYALE_TESTING_HAZARD_STREAM_FROZEN_REFERENCE_HPP

#include "../../simulation/fixtures/deterministic_random_frozen_reference.hpp"

#include <cmath>
#include <cstdint>

namespace blob_royale::testing::hazard_stream_reference {

inline constexpr std::uint64_t kSeed = 2026;
inline constexpr std::uint64_t kTickCount = 280;
inline constexpr std::uint64_t kSkippedReservationTick = 140;
inline constexpr double kWidth = 960.0;
inline constexpr double kHeight = 640.0;
inline constexpr double kRadius = 26.0;
inline constexpr double kMass = 40.0;
inline constexpr double kRestitution = 0.2;
inline constexpr double kSpeed = 200.0;
inline constexpr double kSecondsPerTick = 1.0 / 400.0;
inline constexpr double kFirstIntervalSeconds = 0.05;
inline constexpr double kSecondIntervalSeconds = 0.07;

enum class Birth { kNone, kLethal, kHarmless };

// Independent expected schedule, not production archetype accessors or observed births. With one
// reserved id, the first declaration wins the two common multiples, 140 and 280. A skipped kind
// is not deferred, and omitting the whole reservation at 140 skips both declarations without draws.
[[nodiscard]] constexpr Birth scheduled_birth(const std::uint64_t tick,
                                              const bool omit_reservation) noexcept {
  if (omit_reservation && tick == kSkippedReservationTick) {
    return Birth::kNone;
  }
  if (tick % 20 == 0) {
    return Birth::kLethal;
  }
  return tick % 28 == 0 ? Birth::kHarmless : Birth::kNone;
}

struct Point final {
  double x;
  double y;
};

// Frozen Vector2 materialization's signed-zero policy; this reference owns raw coordinates and
// never calls the production geometry value or query while calculating its expected outputs.
[[nodiscard]] inline Point point(const double x, const double y) {
  return {x == 0.0 ? 0.0 : x, y == 0.0 ? 0.0 : y};
}

[[nodiscard]] inline Point point_on_edge(const std::uint64_t edge, const double along) {
  switch (edge % 4) {
  case 0:
    return point(along * kWidth, 0.0);
  case 1:
    return point(kWidth, along * kHeight);
  case 2:
    return point(along * kWidth, kHeight);
  default:
    return point(0.0, along * kHeight);
  }
}

[[nodiscard]] inline Point outward_normal(const std::uint64_t edge) {
  switch (edge % 4) {
  case 0:
    return point(0.0, -1.0);
  case 1:
    return point(1.0, 0.0);
  case 2:
    return point(0.0, 1.0);
  default:
    return point(-1.0, 0.0);
  }
}

struct Crossing final {
  Point position;
  Point velocity;
  double travel_distance;
  std::uint64_t lifetime_ticks;
};

// Frozen from 42be52c:src/gameplay/shared/hazard_crossing.cpp, with the accepted fixture's
// bounds/radius/speed supplied by the literals above. Preserve the written edge/draw order,
// sqrt/product/normalization order, clearance, and lifetime clamps. The shared frozen RNG owns
// its own old constants/finalizer; neither it nor this oracle calls a production random helper.
[[nodiscard]] inline Crossing
draw_crossing(deterministic_random_reference::DeterministicRandom& random) {
  const std::uint64_t edge = random.next_below(4);
  const double entry_along = random.next_unit_interval();
  const double exit_along = random.next_unit_interval();
  const Point entry = point_on_edge(edge, entry_along);
  const Point exit = point_on_edge(edge + 2, exit_along);
  const double delta_x = exit.x - entry.x;
  const double delta_y = exit.y - entry.y;
  const double length = std::sqrt((delta_x * delta_x) + (delta_y * delta_y));
  const Point direction = point(delta_x / length, delta_y / length);
  const Point normal = outward_normal(edge);
  const double clearance = 2.0 * kRadius;
  const double travel_distance = length + (2.0 * (2.0 * kRadius));
  const double ticks = std::ceil(travel_distance / kSpeed / kSecondsPerTick);
  constexpr std::uint64_t kOldMaximumSafeInteger = 9'007'199'254'740'991ULL;
  const std::uint64_t lifetime =
      !(ticks >= 1.0) ? 1
      : ticks >= static_cast<double>(kOldMaximumSafeInteger) ? kOldMaximumSafeInteger
                                                            : static_cast<std::uint64_t>(ticks);
  return {point(entry.x + (normal.x * clearance), entry.y + (normal.y * clearance)),
          point(direction.x * kSpeed, direction.y * kSpeed), travel_distance, lifetime};
}

} // namespace blob_royale::testing::hazard_stream_reference

#endif
