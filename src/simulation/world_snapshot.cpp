#include "world_snapshot.hpp"

#include "components/controllable_component.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace blob_royale::simulation {
namespace {

// An ordered merge of two ascending stores, which is the join every multi-component reader
// performs: a player is an entity carrying both a body and a controller link.
[[nodiscard]] std::vector<PlayerSnapshot> project_players(const GameWorld& world) {
  const std::span<const ComponentStore<PhysicsBody>::Entry> bodies =
      world.store<PhysicsBody>().entries();
  const std::span<const ComponentStore<Controllable>::Entry> controllables =
      world.store<Controllable>().entries();

  std::vector<PlayerSnapshot> players;
  players.reserve(bodies.size() < controllables.size() ? bodies.size() : controllables.size());
  std::size_t controllable_index = 0;
  for (const ComponentStore<PhysicsBody>::Entry& body : bodies) {
    while (controllable_index < controllables.size() &&
           controllables[controllable_index].entity < body.entity) {
      ++controllable_index;
    }
    if (controllable_index < controllables.size() &&
        controllables[controllable_index].entity == body.entity) {
      players.push_back(PlayerSnapshot::from_body(body.entity, body.value));
    }
  }
  return players;
}

} // namespace

WorldSnapshot WorldSnapshot::from_world(const TickSequence tick_sequence, const GameWorld& world) {
  const std::span<const EntityId> entities = world.entities();

  // Every registered store is copied by one tuple copy, so the snapshot cannot omit a kind.
  ComponentStores<ComponentRegistry> stores;
  ComponentRegistry::for_each_kind([&world, &stores]<typename Component>() {
    std::get<ComponentStore<Component>>(stores) = world.store<Component>();
  });

  return WorldSnapshot(tick_sequence, std::vector<EntityId>(entities.begin(), entities.end()),
                       std::move(stores), project_players(world));
}

WorldSnapshot::WorldSnapshot(const TickSequence tick_sequence, std::vector<EntityId> entities,
                             ComponentStores<ComponentRegistry> stores,
                             std::vector<PlayerSnapshot> players) noexcept
    : tick_sequence_(tick_sequence), entities_(std::move(entities)), stores_(std::move(stores)),
      players_(std::move(players)) {}

} // namespace blob_royale::simulation
