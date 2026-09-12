#include "sandbox/sandbox_mode.hpp"

#include "game_mode_configuration.hpp"
#include "gameplay_validation_error.hpp"
#include "shared/ability_system.hpp"
#include "shared/respawn_system.hpp"
#include "shared/status_system.hpp"
#include "shared/support_loss_trigger.hpp"
#include "shared/thrust_steering_system.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::GameMode>
SandboxMode::create(const GameModeConfiguration& configuration) {
  return std::make_unique<const SandboxMode>(configuration.sandbox, configuration.abilities);
}

std::unique_ptr<const simulation::GameMode> SandboxMode::create() {
  return std::make_unique<const SandboxMode>();
}

simulation::SystemPipeline SandboxMode::systems() const {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kPreKernel,
                                                              ThrustSteeringSystem::create()});
  // `ability` is last at kPreKernel in every mode that declares it, mirroring "`status` runs last
  // at kPostKernel": pulse admission reads the canonical input lock, so every kPreKernel system
  // that can change what that lock answers has already run.
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kPreKernel,
                                                              AbilitySystem::create(abilities_)});
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kPostKernel,
                                                              StatusSystem::create()});
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kLifecycle,
      RespawnSystem::create(configuration_.respawn_delay_ticks())});
  return simulation::SystemPipeline::create(std::move(declared));
}

simulation::MotionTriggerTable SandboxMode::motion_triggers() const {
  std::vector<simulation::MotionTriggerTable::Declaration> declared;
  declared.push_back({0, 0, SupportLossTrigger::create(SupportLossPhasePolicy::kAlways)});
  return simulation::MotionTriggerTable::create(std::move(declared));
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
