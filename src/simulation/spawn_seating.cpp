#include "spawn_seating.hpp"

#include "component_store.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "simulation_tolerance.hpp"
#include "vector2.hpp"

#include <cmath>
#include <span>

namespace blob_royale::simulation {

bool point_is_occupied(const Vector2& point,
                       const std::span<const ComponentStore<PhysicsBody>::Entry> bodies,
                       const double player_radius) {
  const double contact_distance = 2.0 * player_radius;
  for (const ComponentStore<PhysicsBody>::Entry& entry : bodies) {
    const double center_distance =
        std::hypot(entry.value.position().x() - point.x(), entry.value.position().y() - point.y());
    if (less_than_or_approximately_equal(center_distance, contact_distance, kPositionTolerance)) {
      return true;
    }
  }
  return false;
}

void seat_body_at_rest(GameWorld& world, const EntityId entity, const Vector2& position,
                       const double player_radius) {
  world.mutable_store<PhysicsBody>().insert_or_assign(
      entity, PhysicsBody::create(position, Vector2::create(0.0, 0.0), Vector2::create(0.0, 0.0),
                                  player_radius, PhysicsBody::kDefaultMass,
                                  PhysicsBody::kDefaultCollisionLayer,
                                  PhysicsBody::kDefaultCollisionMask, false));
}

} // namespace blob_royale::simulation
