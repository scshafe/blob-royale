#include "physics_body.hpp"

namespace blob_royale::simulation {

PhysicsBody PhysicsBody::create(Vector2 position, Vector2 velocity, Vector2 acceleration) {
  return PhysicsBody(position, velocity, acceleration);
}

PhysicsBody PhysicsBody::with_position(Vector2 position) const {
  return create(position, velocity_, acceleration_);
}

PhysicsBody PhysicsBody::with_velocity(Vector2 velocity) const {
  return create(position_, velocity, acceleration_);
}

PhysicsBody PhysicsBody::with_acceleration(Vector2 acceleration) const {
  return create(position_, velocity_, acceleration);
}

PhysicsBody::PhysicsBody(Vector2 position, Vector2 velocity, Vector2 acceleration) noexcept
    : position_(position), velocity_(velocity), acceleration_(acceleration) {}

} // namespace blob_royale::simulation
