#include "king_of_the_hill/hill_objective.hpp"

#include "components/score_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "match_outcome.hpp"
#include "match_state.hpp"
#include "shared/lobby_start_rule.hpp"
#include "shared/roster.hpp"
#include "tick_context.hpp"
#include "tick_sequence.hpp"

#include <cstdint>
#include <vector>

namespace blob_royale::gameplay {
namespace {

namespace simulation = blob_royale::simulation;

[[nodiscard]] std::uint64_t ticks_since(const simulation::TickSequence now,
                                        const simulation::TickSequence started) noexcept {
  return now.value() >= started.value() ? now.value() - started.value() : 0;
}

[[nodiscard]] std::int64_t score_of(const simulation::GameWorld& world,
                                    const simulation::EntityId entity) noexcept {
  const simulation::Score* score = world.store<simulation::Score>().find(entity);
  return score == nullptr ? 0 : score->points;
}

// The unique holder of the best score, or a draw when several share it.
[[nodiscard]] simulation::MatchOutcome
decided_by_score(const simulation::GameWorld& world,
                 const std::vector<simulation::EntityId>& participants,
                 const std::vector<simulation::EntityId>& leaders) {
  static_cast<void>(world);
  static_cast<void>(participants);
  if (leaders.size() == 1) {
    return simulation::MatchOutcome::won_by_entity(leaders.front());
  }
  return simulation::MatchOutcome::drawn();
}

} // namespace

bool HillObjective::can_start(const simulation::GameWorld& world) const {
  return lobby_ready_to_start(world);
}

simulation::MatchOutcome HillObjective::outcome(const simulation::GameWorld& world,
                                                const simulation::TickContext& context) const {
  const std::vector<simulation::EntityId> participants = participant_entities(world);
  if (participants.empty()) {
    return simulation::MatchOutcome::drawn();
  }
  if (participants.size() == 1) {
    return simulation::MatchOutcome::won_by_entity(participants.front());
  }

  std::int64_t best = score_of(world, participants.front());
  for (const simulation::EntityId entity : participants) {
    const std::int64_t points = score_of(world, entity);
    if (points > best) {
      best = points;
    }
  }
  std::vector<simulation::EntityId> leaders;
  for (const simulation::EntityId entity : participants) {
    if (score_of(world, entity) == best) {
      leaders.push_back(entity);
    }
  }

  if (best >= 0 && static_cast<std::uint64_t>(best) >= configuration_.points_to_win()) {
    return decided_by_score(world, participants, leaders);
  }
  if (ticks_since(context.tick_sequence(), world.match().running_started_tick) >=
      configuration_.time_limit_ticks()) {
    return decided_by_score(world, participants, leaders);
  }
  return simulation::MatchOutcome::undecided();
}

} // namespace blob_royale::gameplay
