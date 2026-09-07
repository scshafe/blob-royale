#ifndef BLOB_ROYALE_GAMEPLAY_SANDBOX_FREE_PLAY_OBJECTIVE_HPP
#define BLOB_ROYALE_GAMEPLAY_SANDBOX_FREE_PLAY_OBJECTIVE_HPP

#include "game_world.hpp"
#include "match_lifecycle_durations.hpp"
#include "match_objective.hpp"
#include "match_outcome.hpp"

namespace blob_royale::gameplay {

// canonical: free_play_objective -- the objective of a game that never ends.
//
// A match that may always start and is never decided reaches `running` on its second tick -- zero
// durations make `countdown` occupy exactly one tick, because the engine's machine commits at most
// one transition per tick -- and then stays there for as long as the simulation runs. That is what
// "free play" means expressed in the three declarations the generic lifecycle asks for
// (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle").
//
// All three are total in every phase, as the interface requires: none of them reads the world, so
// none of them has a phase it would rather not answer in.
//
// It is not `IdleMatchObjective`, which is the engine's fallback for *no mode at all* and never
// leaves `lobby`. The two are opposites -- never start, versus always started -- and one mode's
// rule set does not belong in the kernel.
// related: sandbox_mode.hpp -- the one declaration that returns this.
// related: idle_match_objective.hpp -- the engine fallback this is not.
class FreePlayObjective final : public simulation::MatchObjective {
public:
  FreePlayObjective() = default;

  [[nodiscard]] bool can_start(const simulation::GameWorld&) const override { return true; }

  [[nodiscard]] simulation::MatchOutcome outcome(const simulation::GameWorld&) const override {
    return simulation::MatchOutcome::undecided();
  }

  [[nodiscard]] simulation::MatchLifecycleDurations durations() const noexcept override {
    return {};
  }
};

} // namespace blob_royale::gameplay

#endif
