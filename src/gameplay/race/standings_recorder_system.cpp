#include "race/standings_recorder_system.hpp"

#include "components/controllable_component.hpp"
#include "components/race_progress_component.hpp"
#include "events/race_checkpoint_event.hpp"
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
#include <variant>
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

  std::vector<simulation::RaceCheckpointEvent> finishers;
  for (const auto& event : world.events()) {
    const auto* checkpoint = std::get_if<simulation::RaceCheckpointEvent>(&event);
    if (checkpoint == nullptr || checkpoint->next_checkpoint != course_.checkpoints().size()) {
      continue;
    }
    const bool recorded = std::any_of(block.standings.begin(), block.standings.end(),
                                      [checkpoint](const simulation::RaceStanding& standing) {
                                        return standing.entity == checkpoint->entity;
                                      });
    const bool pending =
        std::any_of(finishers.begin(), finishers.end(),
                    [checkpoint](const auto& held) { return held.entity == checkpoint->entity; });
    if (!recorded && !pending) {
      finishers.push_back(*checkpoint);
    }
  }
  std::sort(finishers.begin(), finishers.end(), [](const auto& first, const auto& second) {
    return first.tick_offset == second.tick_offset ? first.entity < second.entity
                                                   : first.tick_offset < second.tick_offset;
  });
  if (block.standings.size() + finishers.size() > simulation::kMaximumPlayerCount) {
    throw GameplayValidationError(
        GameplayValidationCode::kRaceStandingLimitExceeded, "standings_recorder.standings",
        "one race's standings would hold " +
            std::to_string(block.standings.size() + finishers.size()) +
            " entries, past the accepted limit " + std::to_string(simulation::kMaximumPlayerCount));
  }
  for (const auto& finisher : finishers) {
    const auto* controllable = world.store<simulation::Controllable>().find(finisher.entity);
    if (controllable == nullptr) {
      throw GameplayValidationError(GameplayValidationCode::kRaceFinishEventInvalid,
                                    "standings_recorder.event",
                                    "finish fact must retain its controller identity");
    }
    const bool tied = !block.standings.empty() &&
                      block.standings.back().finished_tick == context.tick_sequence() &&
                      block.standings.back().finished_tick_offset == finisher.tick_offset;
    const std::uint64_t placement = tied ? block.standings.back().placement
                                         : static_cast<std::uint64_t>(block.standings.size()) + 1;
    block.standings.push_back(simulation::RaceStanding{finisher.entity, controllable->controller_id,
                                                       placement, context.tick_sequence(),
                                                       finisher.tick_offset});
  }
}

} // namespace blob_royale::gameplay
