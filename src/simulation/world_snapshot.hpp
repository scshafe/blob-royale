#ifndef BLOB_ROYALE_SIMULATION_WORLD_SNAPSHOT_HPP
#define BLOB_ROYALE_SIMULATION_WORLD_SNAPSHOT_HPP

#include "component_registry.hpp"
#include "component_store.hpp"
#include "entity_id.hpp"
#include "match_snapshot.hpp"
#include "player_snapshot.hpp"
#include "terrain_definition.hpp"
#include "tick_sequence.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <vector>

namespace blob_royale::simulation {

class GameWorld;
class GameSimulation;
class MapDefinition;

// canonical: world_snapshot -- complete immutable copy of one committed simulation state.
//
// The component sections are generated from ComponentRegistry: the snapshot copies every
// registered store, so a kind added to the registry is published without editing this file, and
// the snapshot is complete at compile time rather than by convention.
//
// **`entities()` is derived from the stores this snapshot holds**, not copied from the world's
// answer to the same question, so a published component whose entity is missing from
// `entities()` is not a defect this file has to avoid -- it is unrepresentable (engine review
// finding 2; `entity_roster.hpp`).
//
// **A published component is what its kind declares it publishes**, which is how tick-local state
// stops at this boundary: `Controllable::commands_this_tick` is this tick's live input for one
// entity and is stripped here rather than handed to every reader of a snapshot (engine review
// finding 4; `component_publication.hpp`).
//
// `players()` is the protocol v1 projection: the ascending entities carrying both a PhysicsBody
// and a Controllable. It is materialized once at construction so the encoder walks a contiguous
// span rather than merging two stores per frame.
//
// `match()` is the generic lifecycle section and `random_draw_count()` is the generator's draw
// count, which is committed in every snapshot so two runs that diverge in how many draws they took
// diverge visibly at the first differing tick
// (`docs/architecture/0004-gameplay-architecture.md` § "Snapshots and protocol shape").
//
// `terrain()` retains the actual immutable terrain member of the simulation's map. All snapshots
// from that simulation share that same object, including after the simulation is destroyed.
// Session welcome publishes it once; frame encoding does not serialize it again.
// related: component_publication.hpp -- what each kind publishes.
// related: match_snapshot.hpp -- the match section this carries.
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

  [[nodiscard]] const MatchSnapshot& match() const& noexcept { return match_; }
  [[nodiscard]] const MatchSnapshot& match() const&& = delete;

  [[nodiscard]] std::uint64_t random_draw_count() const noexcept { return random_draw_count_; }

  [[nodiscard]] const TerrainDefinition& terrain() const& noexcept { return *terrain_; }
  [[nodiscard]] const TerrainDefinition& terrain() const&& = delete;

  // Equality is committed value equality, including authored terrain rather than its owner.
  friend bool operator==(const WorldSnapshot& left, const WorldSnapshot& right);

private:
  friend class GameSimulation;

  // Copies one state already committed by GameSimulation, publishing each store through its kind's
  // ComponentPublication and deriving the roster from the copies it just made.
  [[nodiscard]] static WorldSnapshot from_world(TickSequence tick_sequence, const GameWorld& world,
                                                std::string mode_name,
                                                const std::shared_ptr<const MapDefinition>& map);

  WorldSnapshot(TickSequence tick_sequence, std::vector<EntityId> entities,
                ComponentStores<ComponentRegistry> stores, std::vector<PlayerSnapshot> players,
                MatchSnapshot match, std::uint64_t random_draw_count,
                std::shared_ptr<const TerrainDefinition> terrain) noexcept;

  TickSequence tick_sequence_;
  std::vector<EntityId> entities_;
  ComponentStores<ComponentRegistry> stores_;
  std::vector<PlayerSnapshot> players_;
  MatchSnapshot match_;
  std::uint64_t random_draw_count_;
  std::shared_ptr<const TerrainDefinition> terrain_;
};

} // namespace blob_royale::simulation

#endif
