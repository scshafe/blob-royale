#include "race/race_objective.hpp"

#include "components/race_progress_component.hpp"
#include "game_world.hpp"
#include "match_outcome.hpp"
#include "mode_states/race_mode_state.hpp"
#include "race/race_mode_state.hpp"
#include "shared/lobby_start_rule.hpp"
#include "shared/roster.hpp"
#include "tick_context.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace blob_royale::gameplay {
namespace {

[[nodiscard]] std::uint64_t ticks_since(const simulation::TickSequence now,
                                        const simulation::TickSequence started) noexcept {
  return now.value() >= started.value() ? now.value() - started.value() : 0;
}

[[nodiscard]] std::uint64_t progress_of(const simulation::GameWorld& world,
                                        const simulation::EntityId entity) noexcept {
  const simulation::RaceProgress* progress = world.store<simulation::RaceProgress>().find(entity);
  return progress == nullptr ? 0 : progress->next_checkpoint;
}

[[nodiscard]] simulation::MatchOutcome
decided_by_leaders(const std::vector<simulation::EntityId>& leaders) {
  return leaders.size() == 1 ? simulation::MatchOutcome::won_by_entity(leaders.front())
                             : simulation::MatchOutcome::drawn();
}

} // namespace

bool RaceObjective::can_start(const simulation::GameWorld& world) const {
  return lobby_ready_to_start(world);
}

simulation::MatchOutcome RaceObjective::outcome(const simulation::GameWorld& world,
                                                const simulation::TickContext& context) const {
  const std::vector<simulation::EntityId> participants = participant_entities(world);
  if (participants.empty()) {
    return simulation::MatchOutcome::drawn();
  }

  const std::span<const simulation::RaceStanding> standings = race_standings_of(world);
  if (!standings.empty()) {
    const bool every_participant_finished = std::all_of(
        participants.begin(), participants.end(), [&](const simulation::EntityId entity) {
          return std::any_of(standings.begin(), standings.end(),
                             [entity](const simulation::RaceStanding& standing) {
                               return standing.entity == entity;
                             });
        });
    if (every_participant_finished ||
        ticks_since(context.tick_sequence(), standings.front().finished_tick) >=
            configuration_.finish_window_ticks()) {
      std::vector<simulation::EntityId> winners;
      for (const simulation::RaceStanding& standing : standings) {
        if (standing.placement == 1) {
          winners.push_back(standing.entity);
        }
      }
      return decided_by_leaders(winners);
    }
    return simulation::MatchOutcome::undecided();
  }

  if (ticks_since(context.tick_sequence(), world.match().running_started_tick) >=
      configuration_.time_limit_ticks()) {
    std::uint64_t best = 0;
    for (const simulation::EntityId entity : participants) {
      best = std::max(best, progress_of(world, entity));
    }
    std::vector<simulation::EntityId> leaders;
    for (const simulation::EntityId entity : participants) {
      if (progress_of(world, entity) == best) {
        leaders.push_back(entity);
      }
    }
    return decided_by_leaders(leaders);
  }
  return simulation::MatchOutcome::undecided();
}

} // namespace blob_royale::gameplay
