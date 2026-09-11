#include "race/standings_recorder_system.hpp"

#include "components/controllable_component.hpp"
#include "components/race_progress_component.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "match_phase.hpp"
#include "mode_states/race_mode_state.hpp"
#include "race/race_mode_state.hpp"
#include "shared/roster.hpp"
#include "simulation_limits.hpp"
#include "tick_context.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::SimulationSystem>
StandingsRecorderSystem::create(RaceCourse course) {
  return std::make_unique<const StandingsRecorderSystem>(std::move(course));
}

StandingsRecorderSystem::StandingsRecorderSystem(RaceCourse course) noexcept
    : course_(std::move(course)) {}

void StandingsRecorderSystem::apply(simulation::GameWorld& world,
                                    const simulation::TickContext& context) const {
  if (world.match().phase != simulation::MatchPhase::kRunning) {
    return;
  }
  simulation::RaceModeState& block = race_mode_state_in(world, course_);
  if (world.match().previous_phase == simulation::MatchPhase::kCountdown) {
    block.standings.clear();
  }

  std::vector<simulation::EntityId> finishers;
  for (const simulation::EntityId entity : alive_entities(world)) {
    const simulation::RaceProgress* progress = world.store<simulation::RaceProgress>().find(entity);
    if (progress == nullptr ||
        progress->next_checkpoint != static_cast<std::uint64_t>(course_.checkpoints().size())) {
      continue;
    }
    const bool recorded = std::any_of(
        block.standings.begin(), block.standings.end(),
        [entity](const simulation::RaceStanding& standing) { return standing.entity == entity; });
    if (!recorded) {
      finishers.push_back(entity);
    }
  }
  if (block.standings.size() + finishers.size() > simulation::kMaximumPlayerCount) {
    throw GameplayValidationError(
        GameplayValidationCode::kRaceStandingLimitExceeded, "standings_recorder.standings",
        "one race's standings would hold " +
            std::to_string(block.standings.size() + finishers.size()) +
            " entries, past the accepted limit " + std::to_string(simulation::kMaximumPlayerCount));
  }
  const std::uint64_t placement = static_cast<std::uint64_t>(block.standings.size()) + 1;
  for (const simulation::EntityId entity : finishers) {
    const simulation::Controllable& controllable =
        *world.store<simulation::Controllable>().find(entity);
    block.standings.push_back(simulation::RaceStanding{entity, controllable.controller_id,
                                                       placement, context.tick_sequence()});
  }
}

} // namespace blob_royale::gameplay
