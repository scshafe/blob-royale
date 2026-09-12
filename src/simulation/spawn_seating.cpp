#include "spawn_seating.hpp"

#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "simulation_tolerance.hpp"
#include "terrain_queries.hpp"
#include "vector2.hpp"

#include <cmath>
#include <span>

namespace blob_royale::simulation {

bool seat_is_supported_and_unoccupied(
    const Vector2& point, const std::span<const ComponentStore<PhysicsBody>::Entry> bodies,
    const double player_radius, const TerrainDefinition& terrain) {
  if (!terrain_supports_disc(terrain, point, player_radius)) {
    return false;
  }
  for (const ComponentStore<PhysicsBody>::Entry& entry : bodies) {
    const double dx = entry.value.position().x() - point.x();
    const double dy = entry.value.position().y() - point.y();
    const double center_distance = std::sqrt(dx * dx + dy * dy);
    const double contact_distance = player_radius + effective_radius(entry.value, player_radius);
    if (less_than_or_approximately_equal(center_distance, contact_distance, kPositionTolerance)) {
      return false;
    }
  }
  return true;
}

void seat_body_at_rest(GameWorld& world, const EntityId entity, const Vector2& position,
                       const double player_radius) {
  world.mutable_store<PhysicsBody>().insert_or_assign(
      entity, PhysicsBody::create(position, Vector2::create(0.0, 0.0), Vector2::create(0.0, 0.0),
                                  player_radius, PhysicsBody::kDefaultMass,
                                  PhysicsBody::kDefaultCollisionLayer,
                                  PhysicsBody::kDefaultCollisionMask, false)
                  .with_ground_attachment(GroundAttachment::kGroundBound));
  if (auto* controllable = world.mutable_store<Controllable>().mutable_find(entity);
      controllable != nullptr) {
    // This is a new body even for a zero-delay return. Keep a freshly recorded command so the
    // pre-kernel steering system can apply it, but never inherit the previous body's held intent.
    controllable->normalized_thrust_intent.reset();
  }
}

} // namespace blob_royale::simulation
