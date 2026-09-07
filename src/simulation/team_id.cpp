#include "team_id.hpp"

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <string>

namespace blob_royale::simulation {

TeamId TeamId::create(const Value value) {
  if (value < kMinimumTeamId || value > kMaximumTeamId) {
    throw SimulationValidationError(SimulationValidationCode::kTeamIdOutOfRange, "team_id.value",
                                    "TeamId " + std::to_string(value) +
                                        " is outside the inclusive safe-integer range");
  }
  return TeamId(value);
}

} // namespace blob_royale::simulation
