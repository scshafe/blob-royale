#ifndef BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_HILL_OBJECTIVE_HPP
#define BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_HILL_OBJECTIVE_HPP

#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "match_lifecycle_durations.hpp"
#include "match_objective.hpp"
#include "match_outcome.hpp"
#include "tick_context.hpp"

#include <utility>

namespace blob_royale::gameplay {

// canonical: hill_objective -- ending by points or by the clock, as three total predicates.
// @extension-point game_mode
//
//   can_start   every seat filled and a start requested (`shared/lobby_start_rule.hpp`)
//   outcome     evaluated after this tick's scoring, in this order:
//                   participants = participant_entities(world)
//                   if participants is empty:                         drawn
//                   if participants has one entity:                   won_by_entity(it)
//                   best = the maximum Score among participants, absent reading as zero
//                   leaders = participants holding best, ascending
//                   if best >= points_to_win:                         one leader ? won : drawn
//                   if elapsed_running_ticks >= time_limit_ticks:     one leader ? won : drawn
//                   undecided
//   durations   `countdown_ticks` and `restart_delay_ticks` from `[king_of_the_hill]`
//
// The order matters twice. A field that collapses to one is decided before the scoreboard is
// read, which is royale's rule and what makes an abandoned room resolve the same way in every
// mode. And two players reaching the threshold on the same tick is a draw rather than a tie-break
// by id, for the reason royale draws a simultaneous elimination: the tick is the finest grain the
// simulation resolves, and pretending otherwise would be a rule nobody could see
// (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "Lifecycle and objective").
//
// The field is counted in **participants**, every entity carrying a `Controllable`, and not in
// alive bodies: a hill match in which everyone but one is respawning has not collapsed to one.
// `outcome` reads the committing tick from its context, which is the whole reason the objective
// interface carries one.
// related: king_of_the_hill_mode.hpp -- the one declaration that returns this.
// related: ../shared/roster.hpp -- the definition of "participant" the outcome reads.
class HillObjective final : public simulation::MatchObjective {
public:
  explicit HillObjective(KingOfTheHillConfiguration configuration) noexcept
      : configuration_(std::move(configuration)) {}

  [[nodiscard]] bool can_start(const simulation::GameWorld& world) const override;

  [[nodiscard]] simulation::MatchOutcome
  outcome(const simulation::GameWorld& world,
          const simulation::TickContext& context) const override;

  [[nodiscard]] simulation::MatchLifecycleDurations durations() const noexcept override {
    return simulation::MatchLifecycleDurations{configuration_.countdown_ticks(),
                                               configuration_.restart_delay_ticks()};
  }

private:
  KingOfTheHillConfiguration configuration_;
};

} // namespace blob_royale::gameplay

#endif
