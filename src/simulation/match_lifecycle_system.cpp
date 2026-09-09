#include "match_lifecycle_system.hpp"

#include "game_world.hpp"
#include "match_outcome.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "tick_context.hpp"
#include "tick_sequence.hpp"

#include <cstdint>
#include <utility>

namespace blob_royale::simulation {
namespace {

// Ticks elapsed since a phase began. The phase start tick is always a sequence this simulation
// already committed or is committing, so the subtraction never underflows.
[[nodiscard]] std::uint64_t ticks_since(const TickSequence now,
                                        const TickSequence started) noexcept {
  return now.value() >= started.value() ? now.value() - started.value() : 0;
}

void commit_transition(MatchState& match, const MatchPhase phase, const TickSequence now) noexcept {
  match.phase = phase;
  match.phase_started_tick = now;
  // **A transition into `lobby` clears the pending start request, and that is the only direction
  // this fires in.** The rule the lobby needs is "a match never starts without someone pressing
  // Start for *this* lobby", and clearing on arrival is what delivers it: `ended -> lobby` after a
  // finished match, and `countdown -> lobby` after a seat emptied, both leave a lobby that will sit
  // still until the button is pressed again.
  //
  // Clearing on the way *out* of `lobby` instead -- the obvious reading of "one-shot" -- is a trap
  // worth naming, because it does not work. `can_start` is asked twice by this machine: once as
  // "may it leave `lobby`" and once as "may it stay in `countdown`". A request cleared by
  // `lobby -> countdown` would make the very next tick's countdown check see a request that is no
  // longer there, bounce straight back to `lobby`, and no match with a non-zero countdown -- or
  // with a zero one -- could ever reach `running` at all.
  //
  // This is the fifth thing a committed transition writes, and it is engine state written by the
  // engine's own system: the roster lives in `MatchState` beside the phase precisely because it is
  // the input to the first transition (`seat_roster.hpp`).
  if (phase == MatchPhase::kLobby) {
    match.seats.clear_start_request();
  }
}

} // namespace

MatchLifecycleSystem::MatchLifecycleSystem(std::unique_ptr<const MatchObjective> objective)
    : objective_(std::move(objective)), durations_(objective_->durations()) {}

void MatchLifecycleSystem::apply(GameWorld& world, const TickContext& context) const {
  MatchState& match = world.mutable_match();
  const TickSequence now = context.tick_sequence();

  // Recorded before the switch, on every tick, so a declared system on the next tick can tell a
  // phase that just began from one that has been in force: it reads `phase` as this tick's commit
  // and `previous_phase` as the last one's. Modes used to observe this for themselves at
  // `kLifecycle`; it is engine state now (`match_state.hpp`).
  match.previous_phase = match.phase;

  // Exactly one arm runs and each arm commits at most one transition, which is where the
  // "at most one transition per tick" bound comes from -- it is structural rather than checked.
  switch (match.phase) {
  case MatchPhase::kLobby:
    if (objective_->can_start(world)) {
      commit_transition(match, MatchPhase::kCountdown, now);
    }
    return;
  case MatchPhase::kCountdown:
    // Losing the ability to start takes precedence over the countdown elapsing, so a field that
    // empties during the countdown returns to the lobby rather than starting a match it cannot
    // hold.
    if (!objective_->can_start(world)) {
      commit_transition(match, MatchPhase::kLobby, now);
      return;
    }
    if (ticks_since(now, match.phase_started_tick) >= durations_.countdown_ticks) {
      commit_transition(match, MatchPhase::kRunning, now);
      match.running_started_tick = now;
    }
    return;
  case MatchPhase::kRunning: {
    // The objective is consulted only here, so a value it would consider nonsense in another
    // phase -- an empty field's `drawn` -- is never committed.
    const MatchOutcome outcome = objective_->outcome(world, context);
    if (outcome.is_decided()) {
      match.outcome = outcome;
      commit_transition(match, MatchPhase::kEnded, now);
    }
    return;
  }
  case MatchPhase::kEnded:
    if (ticks_since(now, match.phase_started_tick) >= durations_.restart_delay_ticks) {
      // The outcome is deliberately not cleared: it is the last committed result and a client
      // reads it for the whole restart delay. The next match overwrites it when it ends.
      commit_transition(match, MatchPhase::kLobby, now);
    }
    return;
  }
}

} // namespace blob_royale::simulation
