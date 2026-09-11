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

MovementTuning MovementTuning::create(const double acceleration, const double normal_top_speed) {
  require_tuning_scalar(acceleration, kMinimumMovementAcceleration, kMaximumMovementAcceleration,
                        "movement.acceleration_world_units_per_second_squared");
  require_tuning_scalar(normal_top_speed, kMinimumNormalTopSpeed, kMaximumNormalTopSpeed,
                        "movement.normal_top_speed_world_units_per_second");
  return MovementTuning(acceleration == 0.0 ? 0.0 : acceleration, normal_top_speed);
}

} // namespace blob_royale::simulation
