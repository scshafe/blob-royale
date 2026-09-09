#ifndef BLOB_ROYALE_GAMEPLAY_ROYALE_ROYALE_OBJECTIVE_HPP
#define BLOB_ROYALE_GAMEPLAY_ROYALE_ROYALE_OBJECTIVE_HPP

#include "game_world.hpp"
#include "match_lifecycle_durations.hpp"
#include "match_objective.hpp"
#include "match_outcome.hpp"
#include "royale/royale_configuration.hpp"
#include "royale/royale_roster.hpp"
#include "seat_roster.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {

// canonical: royale_objective -- ending by attrition, as three total predicates.
// @extension-point game_mode
//
// The four-phase machine is engine-owned and generic; royale supplies three predicates and two
// durations here and declares no transition logic of its own
// (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle").
//
//   can_start   every seat in the lobby is filled **and** a start has been requested
//   outcome     `won_by_entity` naming the single alive entity at an alive count of 1, `drawn` at
//               0, `undecided` otherwise
//   durations   `countdown_ticks` and `restart_delay_ticks` from `[royale]`
//
// **All three are total in every phase**, which is what lets the engine call them without a guard.
// The engine consults `outcome` only for the `running -> ended` transition, so the `drawn` that an
// empty lobby would return is never committed.
//
// A despawn reduces the alive count exactly as an elimination does, so a disconnect can end a
// match, and neither this objective nor any royale system has to know which happened.
//
// **Starting is a decision, not a threshold.** It used to be "the alive count is at or above
// `lobby_minimum_players`", which meant a match began the instant enough blobs happened to exist --
// nobody chose the moment, and a player who joined a second late arrived mid-match. Now the lobby
// says who is playing and somebody presses Start, so the two questions "is the field complete" and
// "does anyone want to begin" are answered separately and both by people. `lobby_minimum_players`
// was deleted rather than kept as a floor on the seat count, because a floor and a count are two
// sources of truth for one idea.
//
// A one-seat lobby is still a legal, degenerate configuration: one player fills the only seat,
// presses Start, enters `countdown`, and `running` immediately observes an alive count of 1, so the
// match ends and returns to `lobby` -- where the start request has been cleared and it waits. That
// is the difference the one-shot flag makes: the old threshold cycled forever on the same input,
// this one stops.
// related: royale_mode.hpp -- the one declaration that returns this.
// related: royale/royale_roster.hpp -- the definition of "alive" the outcome reads.
// related: seat_roster.hpp -- the lobby `can_start` asks, and where the flag is cleared.
class RoyaleObjective final : public simulation::MatchObjective {
public:
  // The configuration is held **by value**, so nothing this objective reads points back at the mode
  // the engine destroys at construction.
  explicit RoyaleObjective(RoyaleConfiguration configuration) noexcept
      : configuration_(std::move(configuration)) {}

  [[nodiscard]] bool can_start(const simulation::GameWorld& world) const override {
    // Total in every phase, as the interface requires, and phase-free: the engine asks this both as
    // "may it leave `lobby`" and as "may it stay in `countdown`", and the same conjunction answers
    // both. It stays true through the countdown because the flag is cleared on arrival *in* `lobby`
    // and never on departure from it (`match_lifecycle_system.cpp`), and it goes false the moment a
    // seat empties, which is what returns a match whose field broke up to the lobby.
    //
    // The alive count is deliberately not consulted. Who is *seated* is the lobby's business and
    // who is *alive* is the outcome's, and conflating them is what made a joiner's arrival start
    // somebody else's match.
    const simulation::SeatRoster& seats = world.match().seats;
    return seats.is_full() && seats.start_requested();
  }

  [[nodiscard]] simulation::MatchOutcome
  outcome(const simulation::GameWorld& world) const override {
    const std::vector<simulation::EntityId> alive = alive_entities(world);
    if (alive.size() == 1) {
      return simulation::MatchOutcome::won_by_entity(alive.front());
    }
    if (alive.empty()) {
      return simulation::MatchOutcome::drawn();
    }
    return simulation::MatchOutcome::undecided();
  }

  [[nodiscard]] simulation::MatchLifecycleDurations durations() const noexcept override {
    return simulation::MatchLifecycleDurations{configuration_.countdown_ticks(),
                                               configuration_.restart_delay_ticks()};
  }

private:
  RoyaleConfiguration configuration_;
};

} // namespace blob_royale::gameplay

#endif
