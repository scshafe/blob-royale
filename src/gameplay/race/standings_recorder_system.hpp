#ifndef BLOB_ROYALE_GAMEPLAY_RACE_STANDINGS_RECORDER_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_RACE_STANDINGS_RECORDER_SYSTEM_HPP

#include "race/race_course.hpp"
#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: standings_recorder -- records certified finishes once, sharing exact finish times.
// @extension-point simulation_system
//
// First at kLifecycle, only while running. Clears standings when previous_phase is countdown;
// then reads final RaceCheckpointEvent facts, ordered by certified tick offset then EntityId.
// Only equal tick/offset values share placement. Controller identity survives later destruction.
// Throws GAMEPLAY.RACE_STANDING_LIMIT_EXCEEDED if the bounded match ranking would overflow.
// related: docs/architecture/0007-king-of-the-hill-and-race-modes.md section "Progress and
// finishing".
class StandingsRecorderSystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "standings_recorder";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(RaceCourse course);
  explicit StandingsRecorderSystem(RaceCourse course) noexcept;

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

private:
  const RaceCourse course_;
};

} // namespace blob_royale::gameplay

#endif
