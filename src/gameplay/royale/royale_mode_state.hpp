#ifndef BLOB_ROYALE_GAMEPLAY_ROYALE_ROYALE_MODE_STATE_HPP
#define BLOB_ROYALE_GAMEPLAY_ROYALE_ROYALE_MODE_STATE_HPP

#include "game_world.hpp"
#include "match_state.hpp"
#include "mode_match_state_registry.hpp"
#include "mode_states/royale_placements_mode_state.hpp"

#include <variant>

namespace blob_royale::gameplay {

// canonical: royale_mode_state_access -- the one read of royale's arm of `ModeMatchState`.
//
// `MatchState::mode_state` is a closed variant and every mode assigns its own arm the first tick it
// observes another (`mode_match_state_registry.hpp`). Two royale rules read it -- the spawn policy
// reads `previous_phase` at kernel phase 0, and `placement_recorder` reads and rewrites the whole
// block at `kLifecycle` -- so the "what if another arm is held" answer is written once, here.
//
// A world holding another arm reads as the default block: no placements, and a `previous_phase` of
// `lobby`, which is the phase every match begins in. That is total rather than a fallback: the
// value returned is exactly the state a match that has never run would hold, so the first tick of a
// royale simulation behaves identically whether the world was default-constructed or arrived
// carrying `NoModeState`.
//
// **Both readers are ordered so they cannot disagree.** The policy runs at phase 0 and the recorder
// updates `previous_phase` at `kLifecycle` of the same tick, so within one tick both see the phase
// the previous tick committed (`docs/architecture/0005-royale-mode.md` § "Spawning").
// related: mode_states/royale_placements_mode_state.hpp -- the value struct this reads.
// related: placement_recorder_system.hpp -- the one writer.
[[nodiscard]] inline simulation::RoyalePlacementsModeState
royale_mode_state_of(const simulation::GameWorld& world) {
  if (const auto* held =
          std::get_if<simulation::RoyalePlacementsModeState>(&world.match().mode_state);
      held != nullptr) {
    return *held;
  }
  return simulation::RoyalePlacementsModeState{};
}

} // namespace blob_royale::gameplay

#endif
