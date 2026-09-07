#include "shared/thrust_steering_system.hpp"

#include "command_registry.hpp"
#include "component_join.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "physics_body.hpp"

#include <cmath>
#include <memory>
#include <string>
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
  const double x = direction.x();
  const double y = direction.y();
  const double magnitude = std::sqrt((x * x) + (y * y));
  const double scale = magnitude <= 1.0 ? 1.0 : 1.0 / magnitude;
  return simulation::Vector2::create((x * scale) * thrust_max, (y * scale) * thrust_max);
}

void require_valid_thrust_maximum(const double thrust_max_world_units_per_second_squared) {
  if (!std::isfinite(thrust_max_world_units_per_second_squared)) {
    throw GameplayValidationError(GameplayValidationCode::kThrustMaximumNotFinite,
                                  "thrust_steering.thrust_max_world_units_per_second_squared",
                                  "a thrust maximum must be a finite number of wu/s^2");
  }
  if (thrust_max_world_units_per_second_squared < 0.0) {
    throw GameplayValidationError(GameplayValidationCode::kThrustMaximumOutOfRange,
                                  "thrust_steering.thrust_max_world_units_per_second_squared",
                                  "thrust maximum " +
                                      std::to_string(thrust_max_world_units_per_second_squared) +
                                      " wu/s^2 must be greater than or equal to zero");
  }
}

std::unique_ptr<const simulation::SimulationSystem>
ThrustSteeringSystem::create(const double thrust_max_world_units_per_second_squared) {
  require_valid_thrust_maximum(thrust_max_world_units_per_second_squared);
  return std::make_unique<const ThrustSteeringSystem>(thrust_max_world_units_per_second_squared);
}

ThrustSteeringSystem::ThrustSteeringSystem(
    const double thrust_max_world_units_per_second_squared) noexcept
    : thrust_max_(thrust_max_world_units_per_second_squared) {}

void ThrustSteeringSystem::apply(simulation::GameWorld& world,
                                 const simulation::TickContext&) const {
  // The canonical two-store join: a steerable entity is one carrying both a controller link and a
  // body, which is the same question the protocol v1 player projection asks (`component_join.hpp`).
  // It visits in ascending EntityId order, which is the order every phase owes ADR 0003.
  simulation::for_each_entity_with_both(
      world.store<simulation::Controllable>(), world.store<simulation::PhysicsBody>(),
      [&world, this](const simulation::EntityId entity,
                     const simulation::Controllable& controllable,
                     const simulation::PhysicsBody& body) {
        const simulation::ThrustCommand* thrust = recorded_thrust_of(controllable);
        if (thrust == nullptr) {
          return;
        }
        // `insert_or_assign` on an id the store already holds assigns in place: it neither inserts
        // nor moves an entry, so the join's forward walk over the same store stays valid and the
        // ascending order is untouched.
        world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
            entity, body.with_acceleration(steered_acceleration(thrust->direction, thrust_max_)));
      });
}

} // namespace blob_royale::gameplay
