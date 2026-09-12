#ifndef BLOB_ROYALE_GAMEPLAY_RACE_CHECKPOINT_RESPAWN_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_RACE_CHECKPOINT_RESPAWN_SYSTEM_HPP

#include "race/race_course.hpp"
#include "simulation_system.hpp"

#include <memory>
#include <string_view>
#include <utility>

namespace blob_royale::gameplay {

// canonical: checkpoint_respawn -- return a bodyless racer to the last gate it took.
// @extension-point simulation_system
//
// At kLifecycle before shared respawn, walk Controllable + RaceProgress in ascending EntityId.
// A timer or existing body defers the entity; progress zero belongs to the engine's grid policy.
// Otherwise the live body store and map terrain supply shared full-disc support/actual-radius
// occupancy admission, and a free supported gate receives
// the shared at-rest seating write. Reading the live store lets an earlier return block a later
// one at the same gate. Never reads the stage's spatial index, which predates these seatings.
//
// There is no phase predicate: return is roster bookkeeping, like shared respawn. Running-only
// rules own new progress and off-track eliminations; match_reset owns the next lobby's wipe.
// related: grid_spawn_policy.hpp -- progress-zero return through engine SpawnSystem.
// related: ../shared/respawn_system.hpp -- running before it aligns both routes to N + D + 1.
class CheckpointRespawnSystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "checkpoint_respawn";

  // Owns a validated course value; no references into a mode or map outlive construction.
  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(RaceCourse course);

  explicit CheckpointRespawnSystem(RaceCourse course) noexcept : course_(std::move(course)) {}

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  // Throws GAMEPLAY.RACE_PROGRESS_BEYOND_COURSE when a returning racer's progress exceeds the
  // validated gate count; silently choosing another gate would hide corrupt match state.
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

private:
  const RaceCourse course_;
};

} // namespace blob_royale::gameplay

#endif
