#include "world_snapshot.hpp"

#include "game_world.hpp"

#include <utility>
#include <vector>

namespace blob_royale::simulation {

WorldSnapshot WorldSnapshot::from_world(const TickSequence tick_sequence, const GameWorld& world) {
  std::vector<PlayerSnapshot> players;
  players.reserve(world.players().size());
  for (const Player& player : world.players()) {
    players.push_back(PlayerSnapshot::from_player(player));
  }
  return WorldSnapshot(tick_sequence, std::move(players));
}

WorldSnapshot::WorldSnapshot(const TickSequence tick_sequence,
                             std::vector<PlayerSnapshot> players) noexcept
    : tick_sequence_(tick_sequence), players_(std::move(players)) {}

} // namespace blob_royale::simulation
