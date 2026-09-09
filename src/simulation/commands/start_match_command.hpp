#ifndef BLOB_ROYALE_SIMULATION_COMMANDS_START_MATCH_COMMAND_HPP
#define BLOB_ROYALE_SIMULATION_COMMANDS_START_MATCH_COMMAND_HPP

#include "controller_id.hpp"

namespace blob_royale::simulation {

// canonical: start_match_command -- somebody pressed Start.
//
// The command names the deciding agent and carries no other field, because there is nothing else to
// say: the lobby it starts is the one the world holds, and "which match" is not a question a server
// running one simulation can be asked. **`controller` is the server's own stamp** rather than a
// client field, exactly as it is for every other lobby command (`set_seat_count_command.hpp`), and
// it is what gives this command an ordering key -- without it two simultaneous presses would be
// indistinguishable and `InputBatch` could not de-duplicate them.
//
// **It sets a request; it does not start a match.** The request is a one-shot flag on the seat
// roster, and `MatchLifecycleSystem` is still the only thing that commits a phase transition: the
// engine asks the mode's objective at `kLifecycle`, and royale's answer is "every seat is filled
// *and* a start has been requested" (`src/gameplay/royale/royale_objective.hpp`). So pressing Start
// into a lobby with an empty seat records the press and changes no phase, and the press survives
// until either the seats fill or the match ends -- which is deliberate, because the alternative is
// a button that has to be pressed at exactly the right instant by somebody watching for it.
//
// Pressing it twice in one tick is one request, because the flag is a boolean and `InputBatch`
// keeps one command of each kind per sender anyway. Pressing it while the match is already running
// is refused by the phase guard in `game_simulation.cpp`, so a stale press cannot arm the *next*
// match from inside this one.
// related: command_registry.hpp -- the closed list of command kinds.
// related: seat_roster.hpp -- the one-shot flag this sets, and the transition that clears it.
struct StartMatchCommand final {
  ControllerId controller;

  friend bool operator==(const StartMatchCommand&, const StartMatchCommand&) = default;
};

} // namespace blob_royale::simulation

#endif
