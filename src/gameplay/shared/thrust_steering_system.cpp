#include "shared/thrust_steering_system.hpp"

#include "command_registry.hpp"
#include "component_join.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "shared/input_lock.hpp"
#include "shared/locomotion.hpp"
#include "tick_context.hpp"

#include <memory>
#include <variant>

namespace blob_royale::gameplay {
namespace {

namespace simulation = blob_royale::simulation;

// The thrust this tick recorded for one entity, or nullptr when it recorded none. A recorded list
// holds at most one command of each kind (`components/controllable_component.hpp`), so the first
// match is the only one.
[[nodiscard]] const simulation::ThrustCommand*
recorded_thrust_of(const simulation::Controllable& controllable) noexcept {
  for (const simulation::Command& command : controllable.commands_this_tick) {
    if (const auto* thrust = std::get_if<simulation::ThrustCommand>(&command); thrust != nullptr) {
      return thrust;
    }
  }
  return nullptr;
}

} // namespace

simulation::Vector2 steered_acceleration(const simulation::Vector2& direction,
                                         const double thrust_max) {
  return thrust_acceleration_from_intent(normalized_thrust_intent(direction), thrust_max);
}

std::unique_ptr<const simulation::SimulationSystem> ThrustSteeringSystem::create() {
  return std::make_unique<const ThrustSteeringSystem>();
}

void ThrustSteeringSystem::apply(simulation::GameWorld& world,
                                 const simulation::TickContext& context) const {
  // The canonical two-store join: a steerable entity is one carrying both a controller link and a
  // body, which is the same question the protocol v1 player projection asks (`component_join.hpp`).
  // It visits in ascending EntityId order, which is the order every phase owes ADR 0003.
  simulation::for_each_entity_with_both(
      world.store<simulation::Controllable>(), world.store<simulation::PhysicsBody>(),
      [&world, &context](const simulation::EntityId entity,
                         const simulation::Controllable& controllable,
                         const simulation::PhysicsBody& body) {
        if (input_is_locked(world, entity, context.tick_sequence())) {
          const auto zero = simulation::Vector2::create(0.0, 0.0);
          world.mutable_store<simulation::Controllable>()
              .mutable_find(entity)
              ->normalized_thrust_intent = zero;
          world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
              entity, body.with_acceleration(zero));
          return;
        }
        const simulation::ThrustCommand* thrust = recorded_thrust_of(controllable);
        auto intent = controllable.normalized_thrust_intent;
        if (thrust != nullptr && thrust->input_generation == controllable.input_generation) {
          intent = normalized_thrust_intent(thrust->direction);
        }
        if (!intent.has_value()) {
          // An authored body may accelerate before its first command. Absence is not coasting.
          return;
        }
        const auto& tuning = world.match().movement.current;
        const auto requested = thrust_acceleration_from_intent(*intent, tuning.acceleration());
        const auto acceleration = limit_normal_propulsion(
            body.velocity(), requested, tuning.normal_top_speed(), context.fixed_delta());
        // `insert_or_assign` on an id the store already holds assigns in place: it neither inserts
        // nor moves an entry, so the join's forward walk over the same store stays valid and the
        // ascending order is untouched.
        world.mutable_store<simulation::Controllable>()
            .mutable_find(entity)
            ->normalized_thrust_intent = intent;
        world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
            entity, body.with_acceleration(acceleration));
      });
}

} // namespace blob_royale::gameplay
