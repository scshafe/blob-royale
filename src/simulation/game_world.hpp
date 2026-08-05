#ifndef BLOB_ROYALE_SIMULATION_GAME_WORLD_HPP
#define BLOB_ROYALE_SIMULATION_GAME_WORLD_HPP

#include "entity_id.hpp"
#include "player.hpp"

#include <span>
#include <vector>

namespace blob_royale::simulation {

class GameWorld final {
public:
  // Canonicalizes caller order into strict ascending EntityId order.
  [[nodiscard]] static GameWorld create(std::vector<Player> players);

  GameWorld(const GameWorld&) = default;
  GameWorld(GameWorld&&) noexcept = default;
  GameWorld& operator=(const GameWorld&) = default;
  GameWorld& operator=(GameWorld&&) noexcept = default;
  ~GameWorld() = default;

  [[nodiscard]] std::span<const Player> players() const& noexcept { return players_; }
  [[nodiscard]] std::span<const Player> players() const&& = delete;

  [[nodiscard]] const Player* find(EntityId id) const& noexcept;
  [[nodiscard]] const Player* find(EntityId id) const&& = delete;

  friend bool operator==(const GameWorld&, const GameWorld&) = default;

private:
  explicit GameWorld(std::vector<Player> players) noexcept;

  std::vector<Player> players_;
};

} // namespace blob_royale::simulation

#endif
