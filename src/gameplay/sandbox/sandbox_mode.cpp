#include "sandbox/sandbox_mode.hpp"

#include "gameplay_validation_error.hpp"
#include "shared/thrust_steering_system.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::GameMode> SandboxMode::create() {
  return create(kDefaultThrustMaximumWorldUnitsPerSecondSquared);
}

std::unique_ptr<const simulation::GameMode>
SandboxMode::create(const double thrust_maximum_world_units_per_second_squared) {
  // Validated through the steering system's own named rule rather than a second copy of it here, so
  // a mode cannot declare a thrust maximum the system it builds would refuse.
  require_valid_thrust_maximum(thrust_maximum_world_units_per_second_squared);
  return std::make_unique<const SandboxMode>(thrust_maximum_world_units_per_second_squared);
}

SandboxMode::SandboxMode(const double thrust_maximum_world_units_per_second_squared) noexcept
    : thrust_maximum_(thrust_maximum_world_units_per_second_squared) {}

simulation::SystemPipeline SandboxMode::systems() const {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kPreKernel, ThrustSteeringSystem::create(thrust_maximum_)});
  return simulation::SystemPipeline::create(std::move(declared));
}

void SandboxMode::validate_map(const simulation::MapDefinition& map) const {
  if (!map.spawn_points().empty()) {
    return;
  }
  throw GameplayValidationError(GameplayValidationCode::kSandboxMapWithoutSpawnPoint,
                                "sandbox_mode.validate_map.spawn_points",
                                "map " + std::string(map.name()) +
                                    " declares no spawn marker, so sandbox could seat nobody");
}

} // namespace blob_royale::gameplay
