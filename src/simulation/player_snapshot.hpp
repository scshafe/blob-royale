#ifndef BLOB_ROYALE_SIMULATION_PLAYER_SNAPSHOT_HPP
#define BLOB_ROYALE_SIMULATION_PLAYER_SNAPSHOT_HPP

#include "entity_id.hpp"
#include "vector2.hpp"

namespace blob_royale::simulation {

class Player;

// canonical: player_snapshot -- immutable copied presentation state for one player.
class PlayerSnapshot final {
public:
  // Copies all observable simulation state from one validated player value.
  [[nodiscard]] static PlayerSnapshot from_player(const Player& player) noexcept;

  PlayerSnapshot(const PlayerSnapshot&) = default;
  PlayerSnapshot(PlayerSnapshot&&) noexcept = default;
  PlayerSnapshot& operator=(const PlayerSnapshot&) = default;
  PlayerSnapshot& operator=(PlayerSnapshot&&) noexcept = default;
  ~PlayerSnapshot() = default;

  [[nodiscard]] EntityId entity_id() const noexcept { return entity_id_; }
  [[nodiscard]] const Vector2& position() const& noexcept { return position_; }
  [[nodiscard]] const Vector2& position() const&& = delete;
  [[nodiscard]] const Vector2& velocity() const& noexcept { return velocity_; }
  [[nodiscard]] const Vector2& velocity() const&& = delete;
  [[nodiscard]] const Vector2& acceleration() const& noexcept { return acceleration_; }
  [[nodiscard]] const Vector2& acceleration() const&& = delete;

  friend bool operator==(const PlayerSnapshot&, const PlayerSnapshot&) = default;

private:
  PlayerSnapshot(EntityId entity_id, Vector2 position, Vector2 velocity,
                 Vector2 acceleration) noexcept;

  EntityId entity_id_;
  Vector2 position_;
  Vector2 velocity_;
  Vector2 acceleration_;
};

} // namespace blob_royale::simulation

#endif
