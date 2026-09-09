#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_LOBBY_START_RULE_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_LOBBY_START_RULE_HPP

#include "game_world.hpp"
#include "seat_roster.hpp"

namespace blob_royale::gameplay {

// canonical: lobby_start_rule -- when a lobby mode may leave `lobby` and stay in `countdown`.
//
// Every seat in the lobby holds a controller -- a person's, or the bot created for a declared NPC
// seat -- **and** a start has been requested. It is total in every phase, as `MatchObjective`
// requires, and phase-free: the engine asks it both as "may it leave `lobby`" and as "may it stay
// in `countdown`", and the same conjunction answers both. It stays true through the countdown
// because the flag is cleared on arrival *in* `lobby` and never on departure from it
// (`match_lifecycle_system.cpp`), and it goes false the moment a seat empties or loses its bot,
// which is what returns a match whose field broke up to the lobby. A seat declared for a bot the
// runtime has not built yet is not filled (`seat_roster.hpp`, seat_is_filled), so a lobby of
// declarations cannot start a match with nobody in it.
//
// The alive count is deliberately not consulted. Who is *seated* is the lobby's business and who
// is *alive* is the outcome's, and conflating them is what once made a joiner's arrival start
// somebody else's match (`docs/architecture/0005-royale-mode.md` § "Match lifecycle").
//
// This was `RoyaleObjective::can_start`'s body while royale was the only lobby mode; every mode
// with a lobby answers the same way, so it lives here and each objective calls it
// (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "Shared rules promoted, because
// they now have a second customer").
// related: ../royale/royale_objective.hpp -- the first caller.
// related: seat_roster.hpp -- the lobby this asks, and where the flag is cleared.
[[nodiscard]] inline bool lobby_ready_to_start(const simulation::GameWorld& world) {
  const simulation::SeatRoster& seats = world.match().seats;
  return seats.is_full() && seats.start_requested();
}

} // namespace blob_royale::gameplay

#endif
