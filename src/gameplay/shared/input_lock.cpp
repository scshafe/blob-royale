#include "shared/input_lock.hpp"

#include "components/stun_component.hpp"
#include "game_world.hpp"

namespace blob_royale::gameplay {

bool input_is_locked(const simulation::GameWorld& world, const simulation::EntityId entity,
                     const simulation::TickSequence tick) noexcept {
  const auto* stun = world.store<simulation::Stun>().find(entity);
  return stun != nullptr && stun->window.contains(tick);
}

} // namespace blob_royale::gameplay
