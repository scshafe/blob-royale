#ifndef BLOB_ROYALE_SIMULATION_MATCH_STATE_HPP
#define BLOB_ROYALE_SIMULATION_MATCH_STATE_HPP

#include "match_outcome.hpp"
#include "match_phase.hpp"
#include "mode_match_state_registry.hpp"
#include "movement_tuning_state.hpp"
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
// The four engine fields are the ADR's four. Four more are here and each earns its place:
//
//   * `previous_phase` is **the phase the tick before last committed**, recorded by the engine's
//     lifecycle system at the top of its `apply`, before it evaluates this tick's transition. That
//     system runs last at `kLifecycle`, so on the next tick every declared system reads `phase` as
//     the last commit and this as the one before it, and `phase != previous_phase` is exactly "a
//     transition committed on the previous tick" -- which pair it was says which one. Royale
//     observed this for itself in its mode-state block, and the second and third competitive modes
//     would each have observed it again; "which phase did the previous tick commit" is an engine
//     fact, so it is engine state (`docs/architecture/0007-king-of-the-hill-and-race-modes.md`
//     § "Where the framework has to move"). Royale's block keeps a copy as a published mirror so
//     the wire is unchanged.
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
// with `lobby` as the phase before it -- a match that has never run has never left it -- undecided,
// with no seats declared, seating from the first spawn point, and no mode-state block declared.
// related: match_lifecycle_system.hpp -- the only writer of the four engine fields.
// related: spawn_system.hpp -- the only writer of the rotation counter.
struct MatchState final {
  MatchPhase phase{MatchPhase::kLobby};
  // The phase the tick before last committed; see the note above. Written by
  // `MatchLifecycleSystem` on every tick, transition or not.
  MatchPhase previous_phase{MatchPhase::kLobby};
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
  // `[match] lobby_seat_count`.
  SeatRoster seats{};
  std::uint64_t spawn_rotation_counter{};
  ModeMatchState mode_state{NoModeState{}};
  // Shared active tuning survives phase/round changes. The composition root seeds authored
  // defaults once; phase0 is the only command writer and increments a revision once per winner.
  MovementTuningState movement{};

  friend bool operator==(const MatchState&, const MatchState&) = default;
};

} // namespace blob_royale::simulation

#endif
