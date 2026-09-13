#ifndef BLOB_ROYALE_SIMULATION_MOVEMENT_TUNING_HPP
#define BLOB_ROYALE_SIMULATION_MOVEMENT_TUNING_HPP

#include "simulation_limits.hpp"

namespace blob_royale::simulation {

// canonical: movement_tuning -- the one validated room-tuning value. The historical name is
// retained by protocol v3; it now includes charge strength and crossing-object birth rates.
// This value owns intrinsic scalar admission; match state owns narrower room capabilities.
class MovementTuning final {
public:
  // Throws SIMULATION.MOVEMENT_TUNING_NOT_FINITE or MOVEMENT_TUNING_OUT_OF_RANGE with the
  // offending movement field as context. Invalid values are never clipped to a legal setting.
  [[nodiscard]] static MovementTuning
  create(double acceleration, double normal_top_speed,
         double charge_speed_fraction = kDefaultChargeSpeedFraction,
         double lethal_spawn_rate_per_second = 0.0, double nonlethal_spawn_rate_per_second = 0.0);

  [[nodiscard]] static constexpr MovementTuning defaults() noexcept {
    return MovementTuning(kDefaultMovementAcceleration, kDefaultNormalTopSpeed,
                          kDefaultChargeSpeedFraction, 0.0, 0.0);
  }

  MovementTuning(const MovementTuning&) = default;
  MovementTuning(MovementTuning&&) noexcept = default;
  MovementTuning& operator=(const MovementTuning&) = default;
  MovementTuning& operator=(MovementTuning&&) noexcept = default;
  ~MovementTuning() = default;

  [[nodiscard]] constexpr double acceleration() const noexcept { return acceleration_; }
  [[nodiscard]] constexpr double normal_top_speed() const noexcept { return normal_top_speed_; }
  [[nodiscard]] constexpr double charge_speed_fraction() const noexcept {
    return charge_speed_fraction_;
  }
  [[nodiscard]] constexpr double lethal_spawn_rate_per_second() const noexcept {
    return lethal_spawn_rate_per_second_;
  }
  [[nodiscard]] constexpr double nonlethal_spawn_rate_per_second() const noexcept {
    return nonlethal_spawn_rate_per_second_;
  }

  friend bool operator==(const MovementTuning&, const MovementTuning&) = default;

private:
  constexpr MovementTuning(const double acceleration, const double normal_top_speed,
                           const double charge_speed_fraction,
                           const double lethal_spawn_rate_per_second,
                           const double nonlethal_spawn_rate_per_second) noexcept
      : acceleration_(acceleration), normal_top_speed_(normal_top_speed),
        charge_speed_fraction_(charge_speed_fraction),
        lethal_spawn_rate_per_second_(lethal_spawn_rate_per_second),
        nonlethal_spawn_rate_per_second_(nonlethal_spawn_rate_per_second) {}

  double acceleration_;
  double normal_top_speed_;
  double charge_speed_fraction_;
  double lethal_spawn_rate_per_second_;
  double nonlethal_spawn_rate_per_second_;
};

static_assert(kDefaultMovementAcceleration >= kMinimumMovementAcceleration &&
              kDefaultMovementAcceleration <= kMaximumMovementAcceleration);
static_assert(kDefaultNormalTopSpeed >= kMinimumNormalTopSpeed &&
              kDefaultNormalTopSpeed <= kMaximumNormalTopSpeed);

} // namespace blob_royale::simulation

#endif
