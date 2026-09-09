#include "shared/match_reset_system.hpp"

#include "entity_id.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "shared/roster.hpp"
#include "tick_context.hpp"

#include <memory>
#include <vector>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::SimulationSystem> MatchResetSystem::create() {
  return std::make_unique<const MatchResetSystem>();
}

void MatchResetSystem::apply(simulation::GameWorld& world, const simulation::TickContext&) const {
  const simulation::MatchState& match = world.match();
  if (match.phase != simulation::MatchPhase::kLobby ||
      match.previous_phase != simulation::MatchPhase::kEnded) {
    return;
  }
  // Collected before destroying, because `destroy_entity` erases from the store the roster walks.
  for (const simulation::EntityId entity : participant_entities(world)) {
    world.destroy_entity(entity);
  }
}

} // namespace blob_royale::gameplay
