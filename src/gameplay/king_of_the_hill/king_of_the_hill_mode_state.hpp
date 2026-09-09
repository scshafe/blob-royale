#ifndef BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_KING_OF_THE_HILL_MODE_STATE_HPP
#define BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_KING_OF_THE_HILL_MODE_STATE_HPP

#include "game_world.hpp"
#include "match_state.hpp"
#include "mode_match_state_registry.hpp"
#include "mode_states/king_of_the_hill_mode_state.hpp"

#include <variant>

namespace blob_royale::gameplay {

// canonical: king_of_the_hill_mode_state_access -- the one answer to "what if the world holds
// another arm", for the hill.
//
// `MatchState::mode_state` is a closed variant and every mode assigns its own arm the first tick it
// observes another (`mode_match_state_registry.hpp`). One hill rule touches it,
// `hill_rules_publisher`, which stamps the three declared constants every tick; a world holding
// another arm is **replaced** with the default block, which is exactly what royale's
// `royale_mode_state_in` does and for the same reason: a hill system running on a world holding
// another mode's state is a composition error either way, and the block it installs is the one the
// publisher is about to fill.
// related: ../../simulation/mode_states/king_of_the_hill_mode_state.hpp -- the value struct.
// related: hill_rules_publisher_system.hpp -- the only writer.
[[nodiscard]] inline simulation::KingOfTheHillModeState&
king_of_the_hill_mode_state_in(simulation::GameWorld& world) {
  simulation::ModeMatchState& mode_state = world.mutable_match().mode_state;
  if (auto* held = std::get_if<simulation::KingOfTheHillModeState>(&mode_state); held != nullptr) {
    return *held;
  }
  return mode_state.emplace<simulation::KingOfTheHillModeState>();
}

} // namespace blob_royale::gameplay

#endif
