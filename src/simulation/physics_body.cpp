#include "physics_body.hpp"

namespace blob_royale::simulation {

PhysicsBody PhysicsBody::create(Vector2 position, Vector2 velocity, Vector2 acceleration) {
  return PhysicsBody(position, velocity, acceleration, kDefaultRadius, kDefaultMass,
                     kDefaultCollisionLayer, kDefaultCollisionMask, false);
}

PhysicsBody PhysicsBody::create(Vector2 position, Vector2 velocity, Vector2 acceleration,
                                const double radius, const double mass,
                                const CollisionLayer collision_layer,
                                const CollisionLayer collision_mask, const bool is_static) {
  return PhysicsBody(position, velocity, acceleration, radius, mass, collision_layer,
                     collision_mask, is_static);
}

PhysicsBody PhysicsBody::with_position(Vector2 position) const {
  return create(position, velocity_, acceleration_, radius_, mass_, collision_layer_,
                collision_mask_, is_static_);
}

PhysicsBody PhysicsBody::with_velocity(Vector2 velocity) const {
  return create(position_, velocity, acceleration_, radius_, mass_, collision_layer_,
                collision_mask_, is_static_);
}

PhysicsBody PhysicsBody::with_acceleration(Vector2 acceleration) const {
  return create(position_, velocity_, acceleration, radius_, mass_, collision_layer_,
                collision_mask_, is_static_);
}

PhysicsBody::PhysicsBody(Vector2 position, Vector2 velocity, Vector2 acceleration,
                         const double radius, const double mass,
                         const CollisionLayer collision_layer, const CollisionLayer collision_mask,
                         const bool is_static) noexcept
    : position_(position), velocity_(velocity), acceleration_(acceleration), radius_(radius),
      mass_(mass), collision_layer_(collision_layer), collision_mask_(collision_mask),
      is_static_(is_static) {}

} // namespace blob_royale::simulation
