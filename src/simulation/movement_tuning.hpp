#ifndef BLOB_ROYALE_SIMULATION_MOVEMENT_TUNING_HPP
#define BLOB_ROYALE_SIMULATION_MOVEMENT_TUNING_HPP

#include "simulation_limits.hpp"

namespace blob_royale::simulation {

// canonical: movement_tuning -- the validated room-wide pair consumed by normal propulsion.
// Acceleration is wu/s^2, normal top speed is wu/s. This value owns intrinsic scalar admission;
// it owns neither a revision/clock nor external momentum. Authoring, commands, and match state
// share this value rather than imposing different legal ranges at their respective boundaries.
class MovementTuning final {
public:
  // Throws SIMULATION.MOVEMENT_TUNING_NOT_FINITE or MOVEMENT_TUNING_OUT_OF_RANGE with the
  // offending movement field as context. Invalid values are never clipped to a legal setting.
  [[nodiscard]] static MovementTuning create(double acceleration, double normal_top_speed);

  [[nodiscard]] static constexpr MovementTuning defaults() noexcept {
    return MovementTuning(kDefaultMovementAcceleration, kDefaultNormalTopSpeed);
  }

  MovementTuning(const MovementTuning&) = default;
  MovementTuning(MovementTuning&&) noexcept = default;
  MovementTuning& operator=(const MovementTuning&) = default;
  MovementTuning& operator=(MovementTuning&&) noexcept = default;
  ~MovementTuning() = default;

  [[nodiscard]] constexpr double acceleration() const noexcept { return acceleration_; }
  [[nodiscard]] constexpr double normal_top_speed() const noexcept { return normal_top_speed_; }

  friend bool operator==(const MovementTuning&, const MovementTuning&) = default;

private:
  constexpr MovementTuning(const double acceleration, const double normal_top_speed) noexcept
      : acceleration_(acceleration), normal_top_speed_(normal_top_speed) {}

  double acceleration_;
  double normal_top_speed_;
};

static_assert(kDefaultMovementAcceleration >= kMinimumMovementAcceleration &&
              kDefaultMovementAcceleration <= kMaximumMovementAcceleration);
static_assert(kDefaultNormalTopSpeed >= kMinimumNormalTopSpeed &&
              kDefaultNormalTopSpeed <= kMaximumNormalTopSpeed);

} // namespace blob_royale::simulation

#endif
