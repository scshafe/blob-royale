#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_INPUT_LOCK_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_INPUT_LOCK_HPP

#include "entity_id.hpp"
#include "tick_sequence.hpp"

namespace blob_royale::simulation {
class GameWorld;
}

namespace blob_royale::gameplay {

// canonical: input_lock -- active status admission, independent of phase and body presence.
// Returns true exactly when the entity's stun window contains tick. Never throws.
[[nodiscard]] bool input_is_locked(const simulation::GameWorld& world, simulation::EntityId entity,
                                   simulation::TickSequence tick) noexcept;

} // namespace blob_royale::gameplay

#endif
