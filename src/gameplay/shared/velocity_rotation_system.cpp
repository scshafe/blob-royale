#include "shared/velocity_rotation_system.hpp"

#include "component_join.hpp"
#include "components/controllable_component.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "shared/input_lock.hpp"
#include "tick_context.hpp"

#include <memory>
#include <variant>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::SimulationSystem> VelocityRotationSystem::create() {
  return std::make_unique<const VelocityRotationSystem>();
}

void VelocityRotationSystem::apply(simulation::GameWorld& world,
                                   const simulation::TickContext& context) const {
  if (world.match().phase != simulation::MatchPhase::kRunning) {
    return;
  }
  simulation::for_each_entity_with_both(
      world.store<simulation::Controllable>(), world.store<simulation::PhysicsBody>(),
      [&world, &context](const simulation::EntityId entity,
                         const simulation::Controllable& controllable,
                         const simulation::PhysicsBody& body) {
        if (body.is_static() || input_is_locked(world, entity, context.tick_sequence())) {
          return;
        }
        for (const auto& command : controllable.commands_this_tick) {
          const auto* rotation = std::get_if<simulation::RotateVelocityCommand>(&command);
          if (rotation == nullptr || rotation->input_generation != controllable.input_generation) {
            continue;
          }
          const auto velocity = body.velocity();
          const auto rotated = rotation->clockwise
                                   ? simulation::Vector2::create(-velocity.y(), velocity.x())
                                   : simulation::Vector2::create(velocity.y(), -velocity.x());
          world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
              entity, body.with_velocity(rotated));
        }
      });
}

} // namespace blob_royale::gameplay
