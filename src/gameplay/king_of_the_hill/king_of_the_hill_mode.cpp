#include "king_of_the_hill/king_of_the_hill_mode.hpp"

#include "game_mode_configuration.hpp"
#include "gameplay_validation_error.hpp"
#include "king_of_the_hill/hill_geometry.hpp"
#include "king_of_the_hill/hill_movement_system.hpp"
#include "king_of_the_hill/hill_rules_publisher_system.hpp"
#include "king_of_the_hill/hill_scoring_system.hpp"
#include "shared/hazard_spawn_system.hpp"
#include "shared/lifetime_expiry_system.hpp"
#include "shared/match_reset_system.hpp"
#include "shared/respawn_system.hpp"
#include "shared/status_system.hpp"
#include "shared/thrust_steering_system.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::GameMode>
KingOfTheHillMode::create(const GameModeConfiguration& configuration) {
  return create(configuration.king_of_the_hill, configuration.hazards);
}

std::unique_ptr<const simulation::GameMode> KingOfTheHillMode::create() {
  return create(KingOfTheHillConfiguration::defaults());
}

std::unique_ptr<const simulation::GameMode>
KingOfTheHillMode::create(KingOfTheHillConfiguration configuration) {
  return create(std::move(configuration), {});
}

std::unique_ptr<const simulation::GameMode>
KingOfTheHillMode::create(KingOfTheHillConfiguration configuration,
                          std::vector<HazardArchetype> hazards) {
  return std::make_unique<const KingOfTheHillMode>(std::move(configuration), std::move(hazards));
}

simulation::SystemPipeline KingOfTheHillMode::systems() const {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kPreKernel,
                                                              ThrustSteeringSystem::create()});
  // The order of the two `kPostKernel` systems is load-bearing: scoring reads the circle this
  // tick's `hill_movement` wrote.
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kPostKernel, HillMovementSystem::create(configuration_)});
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kPostKernel, HillScoringSystem::create(configuration_)});
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kPostKernel,
                                                              StatusSystem::create()});
  // Remove, reset, expire, add, publish.
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kLifecycle,
      RespawnSystem::create(configuration_.respawn_delay_ticks())});
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kLifecycle,
                                                              MatchResetSystem::create()});
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kLifecycle,
                                                              LifetimeExpirySystem::create()});
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kLifecycle,
                                                              HazardSpawnSystem::create(hazards_)});
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kLifecycle, HillRulesPublisherSystem::create(configuration_)});
  return simulation::SystemPipeline::create(std::move(declared));
}

void KingOfTheHillMode::validate_map(const simulation::MapDefinition& map) const {
  if (hill_markers(map).empty()) {
    throw GameplayValidationError(GameplayValidationCode::kKingOfTheHillMapWithoutHill,
                                  "king_of_the_hill_mode.validate_map.markers",
                                  "map " + std::string(map.name()) +
                                      " declares no hill marker, and a hill match needs a hill");
  }
  if (map.spawn_points().empty()) {
    throw GameplayValidationError(GameplayValidationCode::kKingOfTheHillMapWithoutSpawnPoint,
                                  "king_of_the_hill_mode.validate_map.spawn_points",
                                  "map " + std::string(map.name()) +
                                      " declares no spawn marker, so nobody could be seated");
  }
}

} // namespace blob_royale::gameplay
