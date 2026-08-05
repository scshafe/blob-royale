#include "player.hpp"

namespace blob_royale::simulation {

Player Player::create(EntityId id, PhysicsBody body) { return Player(id, body); }

Player Player::with_body(PhysicsBody body) const { return create(id_, body); }

Player::Player(EntityId id, PhysicsBody body) noexcept : id_(id), body_(body) {}

} // namespace blob_royale::simulation
