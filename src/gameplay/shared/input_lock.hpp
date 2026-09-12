#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_INPUT_LOCK_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_INPUT_LOCK_HPP

#include "entity_id.hpp"
#include "tick_sequence.hpp"

namespace blob_royale::simulation {
class GameWorld;
}

namespace blob_royale::gameplay {

// canonical: input_lock -- active status or completed-race activation admission.
// Returns true during stun or while retained RaceProgress completes the published race course.
// Neither condition depends on phase or body presence; match reset clears progress. Never throws.
[[nodiscard]] bool input_is_locked(const simulation::GameWorld& world, simulation::EntityId entity,
                                   simulation::TickSequence tick) noexcept;

} // namespace blob_royale::gameplay

#endif
