#include "movement_tuning.hpp"

#include "simulation_validation_error.hpp"

#include <cmath>
#include <string>
#include <string_view>

namespace blob_royale::simulation {
namespace {

void require_tuning_scalar(const double value, const double minimum, const double maximum,
                           const std::string_view context) {
  if (!std::isfinite(value)) {
    throw SimulationValidationError(SimulationValidationCode::kMovementTuningNotFinite,
                                    std::string(context), "movement tuning must be finite");
  }
  if (value < minimum || value > maximum) {
    throw SimulationValidationError(SimulationValidationCode::kMovementTuningOutOfRange,
                                    std::string(context),
                                    "movement tuning must lie in the inclusive range " +
                                        std::to_string(minimum) + " to " + std::to_string(maximum));
  }
}

} // namespace

MovementTuning MovementTuning::create(const double acceleration, const double normal_top_speed,
                                      const double charge_speed_fraction,
                                      const double lethal_spawn_rate_per_second,
                                      const double nonlethal_spawn_rate_per_second) {
  require_tuning_scalar(acceleration, kMinimumMovementAcceleration, kMaximumMovementAcceleration,
                        "movement.acceleration_world_units_per_second_squared");
  require_tuning_scalar(normal_top_speed, kMinimumNormalTopSpeed, kMaximumNormalTopSpeed,
                        "movement.normal_top_speed_world_units_per_second");
  require_tuning_scalar(charge_speed_fraction, 0.0, kMaximumChargeSpeedFraction,
                        "movement.charge_speed_fraction");
  require_tuning_scalar(lethal_spawn_rate_per_second, 0.0, kMaximumCrossingSpawnRatePerSecond,
                        "movement.lethal_spawn_rate_per_second");
  require_tuning_scalar(nonlethal_spawn_rate_per_second, 0.0, kMaximumCrossingSpawnRatePerSecond,
                        "movement.nonlethal_spawn_rate_per_second");
  return MovementTuning(acceleration == 0.0 ? 0.0 : acceleration, normal_top_speed,
                        charge_speed_fraction == 0.0 ? 0.0 : charge_speed_fraction,
                        lethal_spawn_rate_per_second == 0.0 ? 0.0 : lethal_spawn_rate_per_second,
                        nonlethal_spawn_rate_per_second == 0.0 ? 0.0
                                                               : nonlethal_spawn_rate_per_second);
}

} // namespace blob_royale::simulation
