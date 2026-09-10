#ifndef BLOB_ROYALE_GAMEPLAY_RACE_RACE_OBJECTIVE_HPP
#define BLOB_ROYALE_GAMEPLAY_RACE_RACE_OBJECTIVE_HPP

#include "match_lifecycle_durations.hpp"
#include "match_objective.hpp"
#include "race/race_configuration.hpp"

#include <utility>

namespace blob_royale::gameplay {

// canonical: race_objective -- finish-window decisions, with gate-count ranking at the clock.
// @extension-point game_mode
//
// The field is participant_entities, including racers awaiting a body. An empty field draws.
// Once a finish is recorded, decide when every participant has finished or the finish window
// expires, using placement 1 (a shared first is a draw). Until then the total clock cannot end the
// race. With no finish, the time limit ranks next_checkpoint, absent reading as zero. A solo
// participant remains a time trial until its finish or the clock.
// related: docs/architecture/0007-king-of-the-hill-and-race-modes.md section "Lifecycle and
// objective".
class RaceObjective final : public simulation::MatchObjective {
public:
  explicit RaceObjective(RaceConfiguration configuration) noexcept
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
  const RaceConfiguration configuration_;
};

} // namespace blob_royale::gameplay

#endif
