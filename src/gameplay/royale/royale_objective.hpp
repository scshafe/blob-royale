#ifndef BLOB_ROYALE_GAMEPLAY_ROYALE_ROYALE_OBJECTIVE_HPP
#define BLOB_ROYALE_GAMEPLAY_ROYALE_ROYALE_OBJECTIVE_HPP

#include "game_world.hpp"
#include "match_lifecycle_durations.hpp"
#include "match_objective.hpp"
#include "match_outcome.hpp"
#include "royale/royale_configuration.hpp"
#include "royale/royale_roster.hpp"

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
//   can_start   the alive count is at or above `lobby_minimum_players`
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
// `lobby_minimum_players = 1` is a legal, degenerate configuration: one player enters `countdown`,
// `running` immediately observes an alive count of 1, and the match ends and restarts on a period
// of the two configured durations plus a handful of transition ticks. The engine's
// one-transition-per-tick rule keeps that cycle terminating and observable rather than a hang.
// related: royale_mode.hpp -- the one declaration that returns this.
// related: royale/royale_roster.hpp -- the definition of "alive" all three read.
class RoyaleObjective final : public simulation::MatchObjective {
public:
  // The configuration is held **by value**, so nothing this objective reads points back at the mode
  // the engine destroys at construction.
  explicit RoyaleObjective(RoyaleConfiguration configuration) noexcept
      : configuration_(std::move(configuration)) {}

  [[nodiscard]] bool can_start(const simulation::GameWorld& world) const override {
    return static_cast<std::uint64_t>(alive_count(world)) >= configuration_.lobby_minimum_players();
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
