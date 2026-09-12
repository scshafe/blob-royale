#include "royale/royale_mode.hpp"

#include "game_mode_configuration.hpp"

#include "gameplay_validation_error.hpp"
#include "royale/elimination_grace_publisher_system.hpp"
#include "royale/placement_recorder_system.hpp"
#include "royale/zone_elimination_system.hpp"
#include "royale/zone_shrink_system.hpp"
#include "shared/hazard_spawn_system.hpp"
#include "shared/lifetime_expiry_system.hpp"
#include "shared/match_reset_system.hpp"
#include "shared/status_system.hpp"
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
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kPreKernel,
                                                              ThrustSteeringSystem::create()});
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kPostKernel, ZoneShrinkSystem::create(configuration_)});
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kPostKernel, ZoneEliminationSystem::create(configuration_)});
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kPostKernel,
                                                              StatusSystem::create()});
  // The five `kLifecycle` systems are ordered remove, reset, then add, then publish, and the order
  // is load-bearing. `placement_recorder` destroys this tick's eliminated entities; `match_reset`
  // wipes every participant on the one lobby tick after a match ended; `lifetime_expiry` emits the
  // despawns for whatever ran out; `hazard_spawn` runs fourth so it draws from the entity id
  // reservation only after every system that also creates one has taken what it needs; and
  // `elimination_grace_publisher` runs last so it is the final writer of the mode-state block and
  // the grace a snapshot carries cannot depend on another system preserving a member it does not
  // know about.
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kLifecycle,
                                                              PlacementRecorderSystem::create()});
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kLifecycle,
                                                              MatchResetSystem::create()});
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kLifecycle,
                                                              LifetimeExpirySystem::create()});
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kLifecycle,
                                                              HazardSpawnSystem::create(hazards_)});
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kLifecycle,
      EliminationGracePublisherSystem::create(configuration_.elimination_grace_ticks())});
  return simulation::SystemPipeline::create(std::move(declared));
}

void RoyaleMode::validate_map(const simulation::MapDefinition& map) const {
  // A spawn marker per lobby seat used to be checked here too. It moved to the application's
  // `require_lobby_fits_map` with the seat count itself, because "a marker per seat" is true of
  // any mode with a lobby and the seat count is a `[match]` fact rather than royale's
  // (`docs/architecture/0006-lobbies-as-rooms.md` § "Rooms"). What only royale knows stays here.
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
