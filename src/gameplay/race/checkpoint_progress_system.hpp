#ifndef BLOB_ROYALE_GAMEPLAY_RACE_CHECKPOINT_PROGRESS_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_RACE_CHECKPOINT_PROGRESS_SYSTEM_HPP

#include "race/race_course.hpp"
#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: checkpoint_progress -- the sole writer of each racer's ordered gate count.
// @extension-point simulation_system
//
// At kPostKernel, only while running, attach RaceProgress{0} to every alive entity that lacks
// it, then test only its next gate with shared/disc_geometry.hpp. At most one gate advances per
// tick. Finished racers keep their bodies and progress, so their contacts remain part of the race.
// Throws GAMEPLAY.RACE_PROGRESS_BEYOND_COURSE for an internally inconsistent gate count.
// related: docs/architecture/0007-king-of-the-hill-and-race-modes.md section "Progress and
// finishing".
class CheckpointProgressSystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "checkpoint_progress";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(RaceCourse course);
  explicit CheckpointProgressSystem(RaceCourse course) noexcept;

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

private:
  const RaceCourse course_;
};

} // namespace blob_royale::gameplay

#endif
