#ifndef BLOB_ROYALE_SIMULATION_IDLE_MATCH_OBJECTIVE_HPP
#define BLOB_ROYALE_SIMULATION_IDLE_MATCH_OBJECTIVE_HPP

#include "match_lifecycle_durations.hpp"
#include "match_objective.hpp"
#include "match_outcome.hpp"

namespace blob_royale::simulation {

// canonical: idle_match_objective -- the objective the engine declares when no mode does.
//
// A match that can never start never leaves `lobby`, so the lifecycle system runs and commits no
// transition, and a simulation constructed without a mode behaves exactly as it did before the
// machine existed. This is what keeps the accepted seven-phase baseline a special case of the
// staged kernel rather than an approximation of it
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick").
//
// It is deliberately not a GameMode: a mode is a rule set and rule sets live in `blob_gameplay`.
// These are the engine's own fallback declarations, which are mechanism.
// related: idle_spawn_policy.hpp -- the matching declaration for the other policy socket.
class IdleMatchObjective final : public MatchObjective {
public:
  IdleMatchObjective() = default;

  [[nodiscard]] bool can_start(const GameWorld&) const override { return false; }

  [[nodiscard]] MatchOutcome outcome(const GameWorld&) const override {
    return MatchOutcome::undecided();
  }

  [[nodiscard]] MatchLifecycleDurations durations() const noexcept override { return {}; }
};

} // namespace blob_royale::simulation

#endif
