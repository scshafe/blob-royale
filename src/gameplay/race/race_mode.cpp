#include "race/race_mode.hpp"

#include "gameplay_validation_error.hpp"
#include "race/checkpoint_progress_system.hpp"
#include "race/checkpoint_respawn_system.hpp"
#include "race/course_publisher_system.hpp"
#include "race/ordered_checkpoint_trigger.hpp"
#include "race/standings_recorder_system.hpp"
#include "shared/hazard_spawn_system.hpp"
#include "shared/lifetime_expiry_system.hpp"
#include "shared/match_reset_system.hpp"
#include "shared/respawn_system.hpp"
#include "shared/status_system.hpp"
#include "shared/support_loss_trigger.hpp"
#include "shared/thrust_steering_system.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::GameMode>
RaceMode::create(const GameModeConfiguration& configuration) {
  return create(configuration.race, configuration.hazards);
}

std::unique_ptr<const simulation::GameMode> RaceMode::create() {
  return create(RaceConfiguration::defaults());
}

std::unique_ptr<const simulation::GameMode> RaceMode::create(RaceConfiguration configuration) {
  return create(std::move(configuration), {});
}

std::unique_ptr<const simulation::GameMode> RaceMode::create(RaceConfiguration configuration,
                                                             std::vector<HazardArchetype> hazards) {
  return std::make_unique<const RaceMode>(std::move(configuration), std::move(hazards));
}

simulation::SystemPipeline RaceMode::systems() const {
  if (!course_.has_value()) {
    throw GameplayValidationError(
        GameplayValidationCode::kRaceCourseUnbound, "race_mode.systems",
        "validate_map must successfully bind the race course before systems are declared");
  }
  const RaceCourse& course = *course_;
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kPreKernel, CoursePublisherSystem::create(course, configuration_)});
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kPreKernel,
                                                              ThrustSteeringSystem::create()});
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kPostKernel, CheckpointProgressSystem::create(course)});
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kPostKernel,
                                                              StatusSystem::create()});
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kLifecycle, StandingsRecorderSystem::create(course)});
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kLifecycle, CheckpointRespawnSystem::create(course)});
  declared.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kLifecycle,
      RespawnSystem::create(configuration_.respawn_delay_ticks())});
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kLifecycle,
                                                              MatchResetSystem::create()});
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kLifecycle,
                                                              LifetimeExpirySystem::create()});
  declared.push_back(simulation::SystemPipeline::StagedSystem{simulation::SystemStage::kLifecycle,
                                                              HazardSpawnSystem::create(hazards_)});
  return simulation::SystemPipeline::create(std::move(declared));
}

simulation::MotionTriggerTable RaceMode::motion_triggers() const {
  if (!course_.has_value()) {
    throw GameplayValidationError(
        GameplayValidationCode::kRaceCourseUnbound, "race_mode.motion_triggers",
        "validate_map must bind the race course before motion triggers are declared");
  }
  std::vector<simulation::MotionTriggerTable::Declaration> declarations;
  // Authored gate count bounds the cursor, not the work of every trajectory. Existing motion
  // event/root/effect ceilings still fail the whole tick; they are never raised for dense gates.
  declarations.push_back({0, 0, SupportLossTrigger::create(SupportLossPhasePolicy::kRunningOnly)});
  declarations.push_back({1, static_cast<std::uint64_t>(course_->checkpoints().size()),
                          OrderedCheckpointTrigger::create(*course_)});
  return simulation::MotionTriggerTable::create(std::move(declarations));
}

void RaceMode::validate_map(const simulation::MapDefinition& map) const {
  course_.reset();
  course_.emplace(RaceCourse::create(map, configuration_));
}

} // namespace blob_royale::gameplay
