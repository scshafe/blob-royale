#include "world_snapshot.hpp"

#include "component_join.hpp"
#include "component_publication.hpp"
#include "components/controllable_component.hpp"
#include "entity_roster.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "physics_body.hpp"

#include <utility>
#include <vector>

namespace blob_royale::simulation {
namespace {

// The protocol v1 player projection: a player is an entity carrying both a body and a controller
// link, so this is the canonical two-store join and nothing else. The hand-written merge that used
// to live here was the only one in the tree, which is exactly why it was the one to replace
// (engine review finding 9; `component_join.hpp`).
[[nodiscard]] std::vector<PlayerSnapshot> project_players(const GameWorld& world) {
  const ComponentStore<PhysicsBody>& bodies = world.store<PhysicsBody>();
  const ComponentStore<Controllable>& controllables = world.store<Controllable>();

  std::vector<PlayerSnapshot> players;
  players.reserve(bodies.size() < controllables.size() ? bodies.size() : controllables.size());
  for_each_entity_with_both(
      bodies, controllables,
      [&players](const EntityId entity, const PhysicsBody& body, const Controllable&) {
        players.push_back(PlayerSnapshot::from_body(entity, body));
      });
  return players;
}

// One kind's published store: the committed store copied, then each value replaced by what its
// kind declares it publishes. The projection preserves every id and their ascending order, so no
// re-sort and no re-validation is needed and the copy stays one pass.
template <typename Component>
[[nodiscard]] ComponentStore<Component> published_store_of(const GameWorld& world) {
  ComponentStore<Component> published = world.store<Component>();
  for (Component& value : published.mutable_values()) {
    value = published_component<Component>(std::move(value));
  }
  return published;
}

} // namespace

WorldSnapshot WorldSnapshot::from_world(const TickSequence tick_sequence, const GameWorld& world,
                                        std::string mode_name,
                                        const std::shared_ptr<const MapDefinition>& map) {
  // Every registered store is visited, so the snapshot cannot omit a kind.
  ComponentStores<ComponentRegistry> stores;
  ComponentRegistry::for_each_kind([&world, &stores]<typename Component>() {
    std::get<ComponentStore<Component>>(stores) = published_store_of<Component>(world);
  });

  // Derived from the stores above rather than from the world, so the published roster and the
  // published components are two readings of one value and cannot disagree.
  std::vector<EntityId> entities = roster_of(stores);

  return WorldSnapshot(
      tick_sequence, std::move(entities), std::move(stores), project_players(world),
      MatchSnapshot::create(std::move(mode_name), world.match()), world.random_draw_counts(),
      std::shared_ptr<const TerrainDefinition>{map, &map->terrain()});
}

WorldSnapshot::WorldSnapshot(const TickSequence tick_sequence, std::vector<EntityId> entities,
                             ComponentStores<ComponentRegistry> stores,
                             std::vector<PlayerSnapshot> players, MatchSnapshot match,
                             const RandomDrawCounts random_draw_counts,
                             std::shared_ptr<const TerrainDefinition> terrain) noexcept
    : tick_sequence_(tick_sequence), entities_(std::move(entities)), stores_(std::move(stores)),
      players_(std::move(players)), match_(std::move(match)), random_draw_counts_(random_draw_counts),
      terrain_(std::move(terrain)) {}

bool operator==(const WorldSnapshot& left, const WorldSnapshot& right) {
  return left.tick_sequence_ == right.tick_sequence_ && left.entities_ == right.entities_ &&
         left.stores_ == right.stores_ && left.players_ == right.players_ &&
         left.match_ == right.match_ && left.random_draw_counts_ == right.random_draw_counts_ &&
         *left.terrain_ == *right.terrain_;
}

} // namespace blob_royale::simulation
