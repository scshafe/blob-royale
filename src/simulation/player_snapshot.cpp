#include "player_snapshot.hpp"

#include "physics_body.hpp"

namespace blob_royale::simulation {

PlayerSnapshot PlayerSnapshot::from_body(const EntityId entity_id,
                                         const PhysicsBody& body) noexcept {
  return PlayerSnapshot(entity_id, body.position(), body.velocity(), body.acceleration());
}

PlayerSnapshot::PlayerSnapshot(EntityId entity_id, Vector2 position, Vector2 velocity,
                               Vector2 acceleration) noexcept
    : entity_id_(entity_id), position_(position), velocity_(velocity), acceleration_(acceleration) {
}

} // namespace blob_royale::simulation
