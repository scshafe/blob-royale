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
}

} // namespace

MatchLifecycleSystem::MatchLifecycleSystem(std::unique_ptr<const MatchObjective> objective)
    : objective_(std::move(objective)), durations_(objective_->durations()) {}

void MatchLifecycleSystem::apply(GameWorld& world, const TickContext& context) const {
  MatchState& match = world.mutable_match();
  const TickSequence now = context.tick_sequence();

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
    const MatchOutcome outcome = objective_->outcome(world);
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
