#include "shared/input_lock.hpp"

#include "components/race_progress_component.hpp"
#include "components/stun_component.hpp"
#include "game_world.hpp"
#include "mode_states/race_mode_state.hpp"

#include <variant>

namespace blob_royale::gameplay {

bool input_is_locked(const simulation::GameWorld& world, const simulation::EntityId entity,
                     const simulation::TickSequence tick) noexcept {
  const auto* stun = world.store<simulation::Stun>().find(entity);
  if (stun != nullptr && stun->window.contains(tick)) {
    return true;
  }
  const auto* race = std::get_if<simulation::RaceModeState>(&world.match().mode_state);
  const auto* progress = world.store<simulation::RaceProgress>().find(entity);
  return race != nullptr && !race->checkpoints.empty() && progress != nullptr &&
         progress->next_checkpoint == race->checkpoints.size();
}

} // namespace blob_royale::gameplay
