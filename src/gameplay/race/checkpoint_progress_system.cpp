#include "race/checkpoint_progress_system.hpp"

#include "components/controllable_component.hpp"
#include "components/race_progress_component.hpp"
#include "events/race_checkpoint_event.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "shared/roster.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>

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
    world.mutable_store<simulation::RaceProgress>().insert_or_assign(entity, progress);
  }
  // Facts are already in certified chronology. Body presence is deliberately not a filter: an
  // earlier gate remains earned when a later support/contact consequence removes that body.
  for (const auto& event : world.events()) {
    const auto* checkpoint = std::get_if<simulation::RaceCheckpointEvent>(&event);
    if (checkpoint == nullptr) {
      continue;
    }
    auto* controllable =
        world.mutable_store<simulation::Controllable>().mutable_find(checkpoint->entity);
    const auto* held = world.store<simulation::RaceProgress>().find(checkpoint->entity);
    const std::uint64_t previous = held == nullptr ? 0 : held->next_checkpoint;
    if (controllable == nullptr || previous >= course_.checkpoints().size() ||
        checkpoint->next_checkpoint != previous + 1) {
      throw GameplayValidationError(GameplayValidationCode::kRaceCheckpointEventInvalid,
                                    "checkpoint_progress.event",
                                    "checkpoint fact must advance one participating racer gate");
    }
    world.mutable_store<simulation::RaceProgress>().insert_or_assign(
        checkpoint->entity, simulation::RaceProgress{checkpoint->next_checkpoint});
    if (checkpoint->next_checkpoint == course_.checkpoints().size()) {
      controllable->normalized_thrust_intent = simulation::Vector2::create(0.0, 0.0);
      controllable->commands_this_tick.clear();
    }
  }
}

} // namespace blob_royale::gameplay
