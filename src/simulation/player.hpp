#ifndef BLOB_ROYALE_SIMULATION_PLAYER_HPP
#define BLOB_ROYALE_SIMULATION_PLAYER_HPP

#include "entity_id.hpp"
#include "physics_body.hpp"

namespace blob_royale::simulation {

class Player final {
public:
  [[nodiscard]] static Player create(EntityId id, PhysicsBody body);

  Player(const Player&) = default;
  Player(Player&&) noexcept = default;
  Player& operator=(const Player&) = default;
  Player& operator=(Player&&) noexcept = default;
  ~Player() = default;

  [[nodiscard]] EntityId id() const noexcept { return id_; }
  [[nodiscard]] const PhysicsBody& body() const& noexcept { return body_; }
  [[nodiscard]] const PhysicsBody& body() const&& = delete;

  [[nodiscard]] Player with_body(PhysicsBody body) const;

  friend bool operator==(const Player&, const Player&) = default;

private:
  Player(EntityId id, PhysicsBody body) noexcept;

  EntityId id_;
  PhysicsBody body_;
};

} // namespace blob_royale::simulation

#endif
