#ifndef BLOB_ROYALE_SIMULATION_MATCH_OBJECTIVE_HPP
#define BLOB_ROYALE_SIMULATION_MATCH_OBJECTIVE_HPP

#include "match_lifecycle_durations.hpp"
#include "match_outcome.hpp"

namespace blob_royale::simulation {

class GameWorld;

// canonical: match_objective -- the mode's complete contract with the generic match lifecycle.
// @extension-point game_mode
//
// Three members are the mode's whole answer to "when may a match start, when is it decided, and
// how long are the two engine-timed phases"
// (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle").
//
// **All three must be total in every phase.** The engine calls them without a guard, and it
// consults `outcome` only for the `running -> ended` transition, so a value a mode would consider
// nonsense in `lobby` -- royale's empty field returning `drawn` -- is never committed. A mode that
// needs a guard has misplaced a rule that belongs in one of its own systems.
//
// Both predicates are pure functions of the committed world. They may read any component store,
// `MatchState`, and this tick's world events through the world reference; they may not mutate, and
// the `const GameWorld&` is what enforces that.
//
// Two implementations of this seam: `IdleMatchObjective`, which the engine declares when no mode
// does, and royale's alive-count objective in Step 21.
// related: game_mode.hpp -- the declaration that returns one of these.
// related: match_lifecycle_system.hpp -- the engine system that calls all three.
class MatchObjective {
public:
  virtual ~MatchObjective() = default;
  MatchObjective(const MatchObjective&) = delete;
  MatchObjective& operator=(const MatchObjective&) = delete;

  // Whether the match may leave `lobby`, and whether it may stay in `countdown`.
  [[nodiscard]] virtual bool can_start(const GameWorld& world) const = 0;

  // The outcome of the tick's final world. Consulted only while `running`.
  [[nodiscard]] virtual MatchOutcome outcome(const GameWorld& world) const = 0;

  // The two engine-timed phase lengths, in ticks.
  [[nodiscard]] virtual MatchLifecycleDurations durations() const noexcept = 0;

protected:
  MatchObjective() = default;
};

} // namespace blob_royale::simulation

#endif
