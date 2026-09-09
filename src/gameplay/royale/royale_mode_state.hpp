#ifndef BLOB_ROYALE_GAMEPLAY_ROYALE_ROYALE_MODE_STATE_HPP
#define BLOB_ROYALE_GAMEPLAY_ROYALE_ROYALE_MODE_STATE_HPP

#include "game_world.hpp"
#include "match_state.hpp"
#include "mode_match_state_registry.hpp"
#include "mode_states/royale_placements_mode_state.hpp"

#include <variant>

namespace blob_royale::gameplay {

// canonical: royale_mode_state_access -- the one answer to "what if the world holds another arm".
//
// `MatchState::mode_state` is a closed variant and every mode assigns its own arm the first tick it
// observes another (`mode_match_state_registry.hpp`). Two royale rules touch it --
// `placement_recorder` reads and rewrites the whole block at `kLifecycle`, and
// `elimination_grace_publisher` stamps one member at `kLifecycle` after it -- so the "what if
// another arm is held" answer is written once, here, for readers and writers alike. The spawn
// policy used to read `previous_phase` here; it reads the engine's `MatchState::previous_phase`
// now, and the block's member is a published mirror of that field (`match_state.hpp`).
//
// A world holding another arm reads as the default block: no placements, and a `previous_phase` of
// `lobby`, which is the phase every match begins in. That is total rather than a fallback: the
// value returned is exactly the state a match that has never run would hold, so the first tick of a
// royale simulation behaves identically whether the world was default-constructed or arrived
// carrying `NoModeState`.
//
// The mirror's `previous_phase` of `lobby` in that default block is also the engine field's
// default, so the two never disagree about a match that has never run
// (`docs/architecture/0005-royale-mode.md` § "Spawning").
// related: mode_states/royale_placements_mode_state.hpp -- the value struct this reads.
// related: placement_recorder_system.hpp -- the writer of the observed members.
// related: elimination_grace_publisher_system.hpp -- the writer of the declared member.
[[nodiscard]] inline simulation::RoyalePlacementsModeState
royale_mode_state_of(const simulation::GameWorld& world) {
  if (const auto* held =
          std::get_if<simulation::RoyalePlacementsModeState>(&world.match().mode_state);
      held != nullptr) {
    return *held;
  }
  return simulation::RoyalePlacementsModeState{};
}

// The held royale block, made present if the world held another arm, for a rule that writes **one
// member** and must leave every other member exactly as it found it.
//
// It exists so that a single-member write is not spelled as a whole-block read-modify-write.
// `placement_recorder` copies the block out, edits three things and assigns it back, which is right
// for a rule that rewrites the block; spelling a one-member stamp the same way would copy the
// placement list twice per tick and would make a future editor's "assign a fresh block" mistake
// silently lose the other members. This returns a reference into the world instead, so the write
// touches exactly what it names.
//
// A world holding another arm is **replaced**, not left alone, which is the same thing
// `placement_recorder` does when it assigns royale's arm over whatever was there. A royale system
// running on a world holding capture-the-flag state is a composition error either way; the block
// this installs is the default one, which is exactly the state `royale_mode_state_of` reports for
// that world, so the two helpers agree about what that world means.
[[nodiscard]] inline simulation::RoyalePlacementsModeState&
royale_mode_state_in(simulation::GameWorld& world) {
  simulation::ModeMatchState& mode_state = world.mutable_match().mode_state;
  if (auto* held = std::get_if<simulation::RoyalePlacementsModeState>(&mode_state);
      held != nullptr) {
    return *held;
  }
  return mode_state.emplace<simulation::RoyalePlacementsModeState>();
}

} // namespace blob_royale::gameplay

#endif
