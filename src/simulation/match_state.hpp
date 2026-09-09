#ifndef BLOB_ROYALE_SIMULATION_MATCH_STATE_HPP
#define BLOB_ROYALE_SIMULATION_MATCH_STATE_HPP

#include "match_outcome.hpp"
#include "match_phase.hpp"
#include "mode_match_state_registry.hpp"
#include "seat_roster.hpp"
#include "tick_sequence.hpp"

#include <cstdint>

namespace blob_royale::simulation {

// canonical: match_state -- the match-wide state of one simulated match.
//
// `MatchState` is world state, so **any committed snapshot determines the whole future of the
// lifecycle machine**: the phase, when it began, when this match's `running` began, and the last
// committed outcome are all a reader needs to know what the next tick may do
// (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle").
//
// The four engine fields are the ADR's four. Three more are here and each earns its place:
//
//   * `seats` is **the lobby the first transition reads**. The four-phase machine is engine-owned
//     and generic, and a mode supplies predicates rather than lifecycle machinery
//     (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle");
//     the roster is the input to `lobby -> countdown`, so it belongs beside the phase it gates
//     rather than in one mode's `mode_state` arm, where the second competitive mode would have to
//     duplicate it along with its wire schema and the whole client lobby. The argument and its
//     honest costs are written out in `seat_roster.hpp`.
//
//   * `spawn_rotation_counter` is the world-owned counter the engine's SpawnSystem offers to the
//     mode's SpawnPolicy and advances after a seating. It is match-wide, durable across ticks, and
//     not entity-shaped, which is exactly what MatchState is for; keeping it in the world rather
//     than in the policy is what makes `SpawnPolicy::choose_spawn_point` a pure function and
//     seating a deterministic function of the committed world
//     (`docs/architecture/0005-royale-mode.md` § "Spawning").
//   * `mode_state` is **the seam for a mode's own match-wide state**. The engine never reads it,
//     never writes it, and never branches on which arm it holds; a mode's system assigns its own
//     arm and reads it back. Royale's placement list and `previous_phase` land here in Step 21 as
//     one more arm of ModeMatchState plus one registration line, with no kernel file edited
//     (`docs/architecture/0005-royale-mode.md` § "Where zone and elimination state live").
//
// A default-constructed value is the state every match begins in: `lobby`, started at tick zero,
// undecided, with no seats declared, seating from the first spawn point, and no mode-state block
// declared.
// related: match_lifecycle_system.hpp -- the only writer of the four engine fields.
// related: spawn_system.hpp -- the only writer of the rotation counter.
struct MatchState final {
  MatchPhase phase{MatchPhase::kLobby};
  // The tick sequence at which `phase` began. A transition committed on tick N records N.
  TickSequence phase_started_tick{TickSequence::zero()};
  // The tick sequence at which the current match's `running` began. Meaningful from the first
  // `running` tick onward and left at its previous value in `lobby` and `countdown`, so a mode
  // that shrinks a zone by elapsed running ticks reads one field and never a clock.
  TickSequence running_started_tick{TickSequence::zero()};
  MatchOutcome outcome{MatchOutcome::undecided()};
  // The lobby: an ordered, bounded list of seats and the one-shot start request that goes with it.
  // A default-constructed roster has no seats, which is the honest value for a world nobody
  // declared a lobby for; the caller that builds the initial world sizes it from
  // `[royale] lobby_seat_count`.
  SeatRoster seats{};
  std::uint64_t spawn_rotation_counter{};
  ModeMatchState mode_state{NoModeState{}};

  friend bool operator==(const MatchState&, const MatchState&) = default;
};

} // namespace blob_royale::simulation

#endif
