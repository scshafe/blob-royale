#ifndef BLOB_ROYALE_SIMULATION_WORLD_SNAPSHOT_HPP
#define BLOB_ROYALE_SIMULATION_WORLD_SNAPSHOT_HPP

#include "component_registry.hpp"
#include "component_store.hpp"
#include "entity_id.hpp"
#include "player_snapshot.hpp"
#include "tick_sequence.hpp"

#include <span>
#include <tuple>
#include <vector>

namespace blob_royale::simulation {

class GameWorld;
class GameSimulation;

// canonical: world_snapshot -- complete immutable copy of one committed simulation state.
//
// The component sections are generated from ComponentRegistry: the snapshot copies the whole
// store tuple, so a kind added to the registry is published without editing this file, and the
// snapshot is complete at compile time rather than by convention.
//
// `players()` is the protocol v1 projection: the ascending entities carrying both a PhysicsBody
// and a Controllable. It is materialized once at construction so the encoder walks a contiguous
// span rather than merging two stores per frame.
class WorldSnapshot final {
public:
  WorldSnapshot(const WorldSnapshot&) = default;
  WorldSnapshot(WorldSnapshot&&) noexcept = default;
  WorldSnapshot& operator=(const WorldSnapshot&) = default;
  WorldSnapshot& operator=(WorldSnapshot&&) noexcept = default;
  ~WorldSnapshot() = default;

  [[nodiscard]] TickSequence tick_sequence() const noexcept { return tick_sequence_; }

  [[nodiscard]] std::span<const EntityId> entities() const& noexcept { return entities_; }
  [[nodiscard]] std::span<const EntityId> entities() const&& = delete;

  template <typename Component>
  [[nodiscard]] std::span<const typename ComponentStore<Component>::Entry>
  components() const& noexcept {
    return std::get<ComponentStore<Component>>(stores_).entries();
  }
  template <typename Component>
  [[nodiscard]] std::span<const typename ComponentStore<Component>::Entry>
  components() const&& = delete;

  [[nodiscard]] std::span<const PlayerSnapshot> players() const& noexcept { return players_; }
  [[nodiscard]] std::span<const PlayerSnapshot> players() const&& = delete;

  friend bool operator==(const WorldSnapshot&, const WorldSnapshot&) = default;

private:
  friend class GameSimulation;

  // Copies one state already committed by GameSimulation. The world invariant supplies order.
  [[nodiscard]] static WorldSnapshot from_world(TickSequence tick_sequence, const GameWorld& world);

  WorldSnapshot(TickSequence tick_sequence, std::vector<EntityId> entities,
                ComponentStores<ComponentRegistry> stores,
                std::vector<PlayerSnapshot> players) noexcept;

  TickSequence tick_sequence_;
  std::vector<EntityId> entities_;
  ComponentStores<ComponentRegistry> stores_;
  std::vector<PlayerSnapshot> players_;
};

} // namespace blob_royale::simulation

#endif
