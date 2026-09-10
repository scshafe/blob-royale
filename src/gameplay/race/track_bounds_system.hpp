#ifndef BLOB_ROYALE_GAMEPLAY_RACE_TRACK_BOUNDS_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_RACE_TRACK_BOUNDS_SYSTEM_HPP

#include "race/race_course.hpp"
#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: track_bounds -- names the alive racers whose centres left the closed corridor.
// @extension-point simulation_system
//
// Runs after checkpoint_progress at kPostKernel, only while running. A distance greater than
// track_half_width + kPositionTolerance emits one EliminationEvent, ascending EntityId. The
// shared respawn system removes those bodies later in the same tick; no geometry is repeated here.
// related: race_course.hpp -- the canonical centreline distance.
class TrackBoundsSystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "track_bounds";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(RaceCourse course);
  explicit TrackBoundsSystem(RaceCourse course) noexcept;

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

private:
  const RaceCourse course_;
};

} // namespace blob_royale::gameplay

#endif
