#ifndef BLOB_ROYALE_SIMULATION_WORLD_SNAPSHOT_HPP
#define BLOB_ROYALE_SIMULATION_WORLD_SNAPSHOT_HPP

#include "player_snapshot.hpp"
#include "tick_sequence.hpp"

#include <span>
#include <vector>

namespace blob_royale::simulation {

class GameWorld;
class GameSimulation;

// canonical: world_snapshot -- complete immutable copy of one committed simulation state.
class WorldSnapshot final {
public:
  WorldSnapshot(const WorldSnapshot&) = default;
  WorldSnapshot(WorldSnapshot&&) noexcept = default;
  WorldSnapshot& operator=(const WorldSnapshot&) = default;
  WorldSnapshot& operator=(WorldSnapshot&&) noexcept = default;
  ~WorldSnapshot() = default;

  [[nodiscard]] TickSequence tick_sequence() const noexcept { return tick_sequence_; }
  [[nodiscard]] std::span<const PlayerSnapshot> players() const& noexcept { return players_; }
  [[nodiscard]] std::span<const PlayerSnapshot> players() const&& = delete;

  friend bool operator==(const WorldSnapshot&, const WorldSnapshot&) = default;

private:
  friend class GameSimulation;

  // Copies one state already committed by GameSimulation. The world invariant supplies order.
  [[nodiscard]] static WorldSnapshot from_world(TickSequence tick_sequence, const GameWorld& world);

  WorldSnapshot(TickSequence tick_sequence, std::vector<PlayerSnapshot> players) noexcept;

  TickSequence tick_sequence_;
  std::vector<PlayerSnapshot> players_;
};

} // namespace blob_royale::simulation

#endif
