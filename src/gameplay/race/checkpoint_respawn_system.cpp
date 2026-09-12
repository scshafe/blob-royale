#include "race/checkpoint_respawn_system.hpp"

#include "components/controllable_component.hpp"
#include "components/race_progress_component.hpp"
#include "components/respawn_timer_component.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "physics_body.hpp"
#include "spawn_seating.hpp"
#include "tick_context.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::SimulationSystem>
CheckpointRespawnSystem::create(RaceCourse course) {
  return std::make_unique<const CheckpointRespawnSystem>(std::move(course));
}

void CheckpointRespawnSystem::apply(simulation::GameWorld& world,
                                    const simulation::TickContext& context) const {
  const double player_radius = context.simulation_config().player_radius();
  for (const auto& entry : world.store<simulation::Controllable>().entries()) {
    const simulation::RaceProgress* progress =
        world.store<simulation::RaceProgress>().find(entry.entity);
    if (progress == nullptr || progress->next_checkpoint == 0 ||
        world.store<simulation::PhysicsBody>().find(entry.entity) != nullptr ||
        world.store<simulation::RespawnTimer>().find(entry.entity) != nullptr) {
      continue;
    }
    if (progress->next_checkpoint > course_.checkpoints().size()) {
      throw GameplayValidationError(
          GameplayValidationCode::kRaceProgressBeyondCourse,
          "checkpoint_respawn.race_progress[entity_id=" + std::to_string(entry.entity.value()) +
              "]",
          "next_checkpoint " + std::to_string(progress->next_checkpoint) +
              " exceeds course checkpoint count " + std::to_string(course_.checkpoints().size()));
    }
    const simulation::Vector2& target =
        course_.checkpoints()[static_cast<std::size_t>(progress->next_checkpoint - 1)];
    if (simulation::seat_is_supported_and_unoccupied(
            target, world.store<simulation::PhysicsBody>().entries(), player_radius,
            context.map().terrain())) {
      simulation::seat_body_at_rest(world, entry.entity, target, player_radius);
    }
  }
}

} // namespace blob_royale::gameplay
