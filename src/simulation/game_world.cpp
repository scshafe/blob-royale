#include "game_world.hpp"

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace blob_royale::simulation {

GameWorld GameWorld::create(std::vector<Player> players) {
  if (players.size() > kMaximumPlayerCount) {
    throw SimulationValidationError(
        SimulationValidationCode::kGameWorldPlayerLimitExceeded, "game_world.players",
        "player count " + std::to_string(players.size()) + " exceeds the accepted limit");
  }

  std::sort(players.begin(), players.end(),
            [](const Player& left, const Player& right) { return left.id() < right.id(); });

  const auto duplicate = std::adjacent_find(
      players.cbegin(), players.cend(),
      [](const Player& left, const Player& right) { return left.id() == right.id(); });
  if (duplicate != players.cend()) {
    throw SimulationValidationError(
        SimulationValidationCode::kGameWorldDuplicateEntityId, "game_world.players.entity_id",
        "duplicate EntityId " + std::to_string(duplicate->id().value()));
  }

  return GameWorld(std::move(players));
}

const Player* GameWorld::find(const EntityId id) const& noexcept {
  const auto match = std::lower_bound(
      players_.cbegin(), players_.cend(), id,
      [](const Player& player, const EntityId searched_id) { return player.id() < searched_id; });
  if (match == players_.cend() || match->id() != id) {
    return nullptr;
  }
  return &*match;
}

GameWorld::GameWorld(std::vector<Player> players) noexcept : players_(std::move(players)) {}

} // namespace blob_royale::simulation
