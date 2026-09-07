#ifndef BLOB_ROYALE_SIMULATION_ENTITY_ROSTER_HPP
#define BLOB_ROYALE_SIMULATION_ENTITY_ROSTER_HPP

#include "component_list.hpp"
#include "component_registry.hpp"
#include "component_store.hpp"
#include "entity_id.hpp"

#include <cstddef>
#include <span>
#include <tuple>
#include <vector>

namespace blob_royale::simulation {

// canonical: entity_roster -- the one answer to "which entities exist".
//
// An entity is an EntityId and nothing else, and everything an entity *is* on a given tick is the
// set of components keyed by its id
// (`docs/architecture/0004-gameplay-architecture.md` § "Entities, components, and stores"). The
// roster is therefore **derived** from the component stores rather than stored beside them: an
// entity exists exactly while some registered store holds its id.
//
// This is the resolution of engine review finding 2. A stored roster is a second answer to the
// same question, and `GameWorld::mutable_store<C>()` lets either answer drift from the other, so a
// snapshot could publish a component for an entity its own `entities()` omitted. Deriving the
// roster makes that disagreement unrepresentable rather than merely detected: there is one source
// of truth and the other reader is gone.
//
// The two costs are worth stating. `roster_of` allocates and is linear in the total number of
// component entries, so it is called where a roster is *published* -- the snapshot, and a system
// that walks every entity -- and never inside a kernel phase, every one of which walks
// the store it actually reads. And an entity that carries no component at all is not
// representable, which is the ADR's own definition rather than a limitation: `GameWorld::
// create_entity()` draws an id and the entity comes into existence when its first component is
// written, which for a spawn is the `Controllable` phase 0 writes in the same statement.
// related: game_world.hpp -- the world whose roster this derives.
// related: world_snapshot.hpp -- the publication that derives its roster from its own copies.

// Every entity id some registered store holds, ascending and distinct.
//
// Every store is ascending by construction, so this is a successive ordered union -- one linear
// merge per registered kind -- rather than a sort of the concatenation. The runtime takes a
// snapshot on every committed tick, so this runs at tick rate and the log factor a sort would add
// is not worth paying for data that already arrives sorted.
[[nodiscard]] inline std::vector<EntityId>
roster_of(const ComponentStores<ComponentRegistry>& stores) {
  std::vector<EntityId> roster;
  std::vector<EntityId> merged;
  ComponentRegistry::for_each_kind([&stores, &roster, &merged]<typename Component>() {
    const std::span<const typename ComponentStore<Component>::Entry> entries =
        std::get<ComponentStore<Component>>(stores).entries();
    if (entries.empty()) {
      return;
    }
    if (roster.empty()) {
      roster.reserve(entries.size());
      for (const typename ComponentStore<Component>::Entry& entry : entries) {
        roster.push_back(entry.entity);
      }
      return;
    }
    merged.clear();
    merged.reserve(roster.size() + entries.size());
    std::size_t held = 0;
    std::size_t added = 0;
    while (held < roster.size() && added < entries.size()) {
      if (roster[held] < entries[added].entity) {
        merged.push_back(roster[held++]);
      } else if (entries[added].entity < roster[held]) {
        merged.push_back(entries[added++].entity);
      } else {
        merged.push_back(roster[held++]);
        ++added;
      }
    }
    for (; held < roster.size(); ++held) {
      merged.push_back(roster[held]);
    }
    for (; added < entries.size(); ++added) {
      merged.push_back(entries[added].entity);
    }
    roster.swap(merged);
  });
  return roster;
}

// Whether any registered store holds this id, which is exactly "this entity exists". Total and
// allocation-free: one binary search per registered kind.
[[nodiscard]] inline bool roster_contains(const ComponentStores<ComponentRegistry>& stores,
                                          const EntityId entity) noexcept {
  bool found = false;
  ComponentRegistry::for_each_kind([&stores, entity, &found]<typename Component>() {
    found = found || std::get<ComponentStore<Component>>(stores).find(entity) != nullptr;
  });
  return found;
}

} // namespace blob_royale::simulation

#endif
