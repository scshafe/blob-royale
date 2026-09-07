#include "royale/royale_mode.hpp"

#include "game_mode_configuration.hpp"

#include "gameplay_validation_error.hpp"
#include "royale/placement_recorder_system.hpp"
#include "royale/zone_elimination_system.hpp"
#include "royale/zone_shrink_system.hpp"
#include "shared/hazard_spawn_system.hpp"
#include "shared/lifetime_expiry_system.hpp"
#include "shared/thrust_steering_system.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::GameMode>
RoyaleMode::create(const GameModeConfiguration& configuration) {
  return create(configuration.royale, configuration.hazards);
}

std::unique_ptr<const simulation::GameMode> RoyaleMode::create() {
  return create(RoyaleConfiguration::defaults());
}

std::unique_ptr<const simulation::GameMode> RoyaleMode::create(RoyaleConfiguration configuration) {
  return create(std::move(configuration), {});
}

std::unique_ptr<const simulation::GameMode>
RoyaleMode::create(RoyaleConfiguration configuration, std::vector<HazardArchetype> hazards) {
  return std::make_unique<const RoyaleMode>(std::move(configuration), std::move(hazards));
}

simulation::SystemPipeline RoyaleMode::systems() const {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kPreKernel,
      ThrustSteeringSystem::create(configuration_.thrust_maximum())});
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kPostKernel, ZoneShrinkSystem::create(configuration_)});
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kPostKernel, ZoneEliminationSystem::create(configuration_)});
  // The three `kLifecycle` systems are ordered remove-then-add, and the order is load-bearing.
  // `placement_recorder` destroys this tick's eliminated entities; `lifetime_expiry` emits the
  // despawns for whatever ran out; `hazard_spawn` runs last so it draws from the entity id
  // reservation only after every system that also creates one has taken what it needs.
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kLifecycle,
                                                              PlacementRecorderSystem::create()});
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kLifecycle,
                                                              LifetimeExpirySystem::create()});
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kLifecycle,
                                                              HazardSpawnSystem::create(hazards_)});
  return simulation::SystemPipeline::create(std::move(declared));
}

void RoyaleMode::validate_map(const simulation::MapDefinition& map) const {
  if (map.spawn_points().size() < configuration_.lobby_minimum_players()) {
    throw GameplayValidationError(GameplayValidationCode::kRoyaleMapWithoutEnoughSpawnPoints,
                                  "royale_mode.validate_map.spawn_points",
                                  "map " + std::string(map.name()) + " declares " +
                                      std::to_string(map.spawn_points().size()) +
                                      " spawn markers, fewer than the " +
                                      std::to_string(configuration_.lobby_minimum_players()) +
                                      " royale needs to ever satisfy can_start");
  }
  const double full_radius = zone_full_radius(map.bounds());
  if (full_radius <= configuration_.zone_minimum_radius()) {
    throw GameplayValidationError(
        GameplayValidationCode::kRoyaleMapArenaWithinZoneMinimum, "royale_mode.validate_map.bounds",
        "map " + std::string(map.name()) + " has a circumscribed radius of " +
            std::to_string(full_radius) + " wu, which must be strictly greater than the " +
            std::to_string(configuration_.zone_minimum_radius()) +
            " wu zone minimum or the zone would never contract");
  }
}

} // namespace blob_royale::gameplay
