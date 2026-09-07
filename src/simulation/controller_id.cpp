#include "controller_id.hpp"

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <string>

namespace blob_royale::simulation {

ControllerId ControllerId::create(const Value value) {
  if (value < kMinimumControllerId || value > kMaximumControllerId) {
    throw SimulationValidationError(
        SimulationValidationCode::kControllerIdOutOfRange, "controller_id.value",
        "ControllerId " + std::to_string(value) + " is outside the inclusive safe-integer range");
  }
  return ControllerId(value);
}

} // namespace blob_royale::simulation
