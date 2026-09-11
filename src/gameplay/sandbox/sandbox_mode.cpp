#include "sandbox/sandbox_mode.hpp"

#include "game_mode_configuration.hpp"
#include "gameplay_validation_error.hpp"
#include "shared/thrust_steering_system.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::GameMode> SandboxMode::create(const GameModeConfiguration&) {
  return create();
}

std::unique_ptr<const simulation::GameMode> SandboxMode::create() {
  return std::make_unique<const SandboxMode>();
}

simulation::SystemPipeline SandboxMode::systems() const {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kPreKernel,
                                                              ThrustSteeringSystem::create()});
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
