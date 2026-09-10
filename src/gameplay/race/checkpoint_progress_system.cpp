#include "race/checkpoint_progress_system.hpp"

#include "components/race_progress_component.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "shared/disc_geometry.hpp"
#include "shared/roster.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::SimulationSystem>
CheckpointProgressSystem::create(RaceCourse course) {
  return std::make_unique<const CheckpointProgressSystem>(std::move(course));
}

CheckpointProgressSystem::CheckpointProgressSystem(RaceCourse course) noexcept
    : course_(std::move(course)) {}

void CheckpointProgressSystem::apply(simulation::GameWorld& world,
                                     const simulation::TickContext&) const {
  if (world.match().phase != simulation::MatchPhase::kRunning) {
    return;
  }
  for (const simulation::EntityId entity : alive_entities(world)) {
    const simulation::RaceProgress* held = world.store<simulation::RaceProgress>().find(entity);
    simulation::RaceProgress progress = held == nullptr ? simulation::RaceProgress{} : *held;
    if (progress.next_checkpoint > static_cast<std::uint64_t>(course_.checkpoints().size())) {
      throw GameplayValidationError(
          GameplayValidationCode::kRaceProgressBeyondCourse, "checkpoint_progress.race_progress",
          "entity " + std::to_string(entity.value()) + " names gate " +
              std::to_string(progress.next_checkpoint) + " beyond the course's " +
              std::to_string(course_.checkpoints().size()) + " gates");
    }
    if (progress.next_checkpoint < static_cast<std::uint64_t>(course_.checkpoints().size())) {
      const simulation::PhysicsBody& body = *world.store<simulation::PhysicsBody>().find(entity);
      const simulation::Vector2& gate =
          course_.checkpoints()[static_cast<std::size_t>(progress.next_checkpoint)];
      if (!center_is_outside(body.position(), gate, course_.checkpoint_radius())) {
        ++progress.next_checkpoint;
      }
    }
    world.mutable_store<simulation::RaceProgress>().insert_or_assign(entity, progress);
  }
}

} // namespace blob_royale::gameplay
