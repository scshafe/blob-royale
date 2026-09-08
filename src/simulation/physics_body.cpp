#include "physics_body.hpp"

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <cmath>

namespace blob_royale::simulation {

PhysicsBody PhysicsBody::create(Vector2 position, Vector2 velocity, Vector2 acceleration) {
  return validated(position, velocity, acceleration, kUndeclaredRadius, kDefaultMass,
                   kDefaultRestitution, kDefaultDragScale, kDefaultCollisionLayer,
                   kDefaultCollisionMask, false, kDefaultBoundsBehavior);
}

PhysicsBody PhysicsBody::create(Vector2 position, Vector2 velocity, Vector2 acceleration,
                                const double radius, const double mass,
                                const CollisionLayer collision_layer,
                                const CollisionLayer collision_mask, const bool is_static) {
  return validated(position, velocity, acceleration, radius, mass, kDefaultRestitution,
                   kDefaultDragScale, collision_layer, collision_mask, is_static,
                   kDefaultBoundsBehavior);
}

PhysicsBody PhysicsBody::create_static(Vector2 position) {
  return create_static(position, kDefaultCollisionLayer, kDefaultCollisionMask);
}

PhysicsBody PhysicsBody::create_static(Vector2 position, const CollisionLayer collision_layer,
                                       const CollisionLayer collision_mask) {
  return validated(position, Vector2::create(0.0, 0.0), Vector2::create(0.0, 0.0),
                   kUndeclaredRadius, kDefaultMass, kDefaultRestitution, kDefaultDragScale,
                   collision_layer, collision_mask, true, kDefaultBoundsBehavior);
}

PhysicsBody PhysicsBody::with_position(Vector2 position) const {
  return validated(position, velocity_, acceleration_, radius_, mass_, restitution_, drag_scale_,
                   collision_layer_, collision_mask_, is_static_, bounds_behavior_);
}

PhysicsBody PhysicsBody::with_velocity(Vector2 velocity) const {
  return validated(position_, velocity, acceleration_, radius_, mass_, restitution_, drag_scale_,
                   collision_layer_, collision_mask_, is_static_, bounds_behavior_);
}

PhysicsBody PhysicsBody::with_acceleration(Vector2 acceleration) const {
  return validated(position_, velocity_, acceleration, radius_, mass_, restitution_, drag_scale_,
                   collision_layer_, collision_mask_, is_static_, bounds_behavior_);
}

PhysicsBody PhysicsBody::with_radius(const double radius) const {
  return validated(position_, velocity_, acceleration_, radius, mass_, restitution_, drag_scale_,
                   collision_layer_, collision_mask_, is_static_, bounds_behavior_);
}

PhysicsBody PhysicsBody::with_mass(const double mass) const {
  return validated(position_, velocity_, acceleration_, radius_, mass, restitution_, drag_scale_,
                   collision_layer_, collision_mask_, is_static_, bounds_behavior_);
}

PhysicsBody PhysicsBody::with_restitution(const double restitution) const {
  return validated(position_, velocity_, acceleration_, radius_, mass_, restitution, drag_scale_,
                   collision_layer_, collision_mask_, is_static_, bounds_behavior_);
}

PhysicsBody PhysicsBody::with_drag_scale(const double drag_scale) const {
  return validated(position_, velocity_, acceleration_, radius_, mass_, restitution_, drag_scale,
                   collision_layer_, collision_mask_, is_static_, bounds_behavior_);
}

PhysicsBody PhysicsBody::with_bounds_behavior(const BoundsBehavior bounds_behavior) const {
  return validated(position_, velocity_, acceleration_, radius_, mass_, restitution_, drag_scale_,
                   collision_layer_, collision_mask_, is_static_, bounds_behavior);
}

PhysicsBody PhysicsBody::validated(Vector2 position, Vector2 velocity, Vector2 acceleration,
                                   const double radius, const double mass, const double restitution,
                                   const double drag_scale, const CollisionLayer collision_layer,
                                   const CollisionLayer collision_mask, const bool is_static,
                                   const BoundsBehavior bounds_behavior) {
  // A **dynamic** body's mass is strictly positive because the general impulse equation divides by
  // it, and bounded by the same physical component limit every other scalar in this domain obeys.
  // Zero is not "a body that cannot be pushed" -- that is `is_static` -- it is a division by zero
  // one phase later.
  //
  // A **static** body's mass may be zero, and that is not a loophole. Nothing divides by it:
  // neither `elastic_disc` nor `reflect_static` reads a mass at all, and the general impulse
  // requires a dynamic body on both sides, so a wall is never a divisor. Zero there means what a
  // wall's zero velocity means -- structurally absent rather than incidentally small -- and it is
  // what the accepted protocol v2 golden example publishes for its wall, against a schema that
  // types `mass` as `nonnegative_world_scalar`
  // (`docs/protocol/schema/v2/physics-body-component.schema.json`). Requiring otherwise would
  // regenerate an accepted artifact to state an invariant nothing needs.
  if (!std::isfinite(mass)) {
    throw SimulationValidationError(SimulationValidationCode::kPhysicsBodyMassOutOfRange,
                                    "physics_body.mass", "mass must be finite");
  }
  if (mass < 0.0 || (mass == 0.0 && !is_static) || mass > kMaximumPhysicalComponentMagnitude) {
    throw SimulationValidationError(
        SimulationValidationCode::kPhysicsBodyMassOutOfRange, "physics_body.mass",
        "mass must be within the accepted physical component limit, and greater than zero unless "
        "the body is static");
  }
  // Restitution is a dimensionless fraction of the normal closing speed a contact returns. Above
  // one it would add energy to the pair on every bounce, which no accepted phase bounds; below zero
  // it would reverse the sign of the impulse and pull the pair together.
  if (!std::isfinite(restitution)) {
    throw SimulationValidationError(SimulationValidationCode::kPhysicsBodyRestitutionOutOfRange,
                                    "physics_body.restitution", "restitution must be finite");
  }
  if (restitution < kMinimumRestitution || restitution > kMaximumRestitution) {
    throw SimulationValidationError(SimulationValidationCode::kPhysicsBodyRestitutionOutOfRange,
                                    "physics_body.restitution",
                                    "restitution must lie in the closed interval from zero to one");
  }
  // The drag scale is a dimensionless multiplier on the configured `[simulation] drag_per_second`,
  // and it is validated by exactly the rule that parameter is validated by
  // (`simulation_config.cpp` § `require_drag_per_second`): finite, non-negative, and **not bounded
  // above**.
  //
  // Below zero the phase 1 factor `max(0, 1 - drag_per_second * drag_scale * dt)` exceeds one, so
  // the body gains speed geometrically on every tick with nothing to stop it. That is not drag at
  // any magnitude, so it is a rejection rather than a clamp.
  //
  // Above, there is nothing to reject. A large scale only drives the factor's subtrahend past one,
  // and the clamp at zero already makes that total: the body stops on the tick it is applied rather
  // than reversing. Saturating at "stops immediately" is a meaningful body -- one held still by
  // drag alone -- and a ceiling here would state an invariant nothing needs while forbidding it,
  // which is the same argument that leaves `drag_per_second` itself unbounded above. The one
  // remaining edge is the product `drag_per_second * drag_scale` overflowing to infinity, which
  // needs a configured drag around 1e300 to reach and which phase 1 fails closed on, because
  // `apply_velocity_drag` rejects a drag that is not finite.
  if (!std::isfinite(drag_scale)) {
    throw SimulationValidationError(SimulationValidationCode::kPhysicsBodyDragScaleOutOfRange,
                                    "physics_body.drag_scale", "drag scale must be finite");
  }
  if (drag_scale < kMinimumDragScale) {
    throw SimulationValidationError(SimulationValidationCode::kPhysicsBodyDragScaleOutOfRange,
                                    "physics_body.drag_scale",
                                    "drag scale must be greater than or equal to zero");
  }
  return PhysicsBody(position, velocity, acceleration, radius, mass, restitution, drag_scale,
                     collision_layer, collision_mask, is_static, bounds_behavior);
}

PhysicsBody::PhysicsBody(Vector2 position, Vector2 velocity, Vector2 acceleration,
                         const double radius, const double mass, const double restitution,
                         const double drag_scale, const CollisionLayer collision_layer,
                         const CollisionLayer collision_mask, const bool is_static,
                         const BoundsBehavior bounds_behavior) noexcept
    : position_(position), velocity_(velocity), acceleration_(acceleration), radius_(radius),
      mass_(mass), restitution_(restitution), drag_scale_(drag_scale),
      collision_layer_(collision_layer), collision_mask_(collision_mask), is_static_(is_static),
      bounds_behavior_(bounds_behavior) {}

} // namespace blob_royale::simulation
