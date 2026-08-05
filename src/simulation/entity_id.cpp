#include "entity_id.hpp"

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <string>

namespace blob_royale::simulation {

EntityId EntityId::create(const Value value) {
  if (value < kMinimumEntityId || value > kMaximumEntityId) {
    throw SimulationValidationError(
        SimulationValidationCode::kEntityIdOutOfRange, "entity_id.value",
        "EntityId " + std::to_string(value) + " is outside the inclusive safe-integer range");
  }
  return EntityId(value);
}

} // namespace blob_royale::simulation
