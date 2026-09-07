#ifndef BLOB_ROYALE_SIMULATION_EVENTS_DESPAWN_EVENT_HPP
#define BLOB_ROYALE_SIMULATION_EVENTS_DESPAWN_EVENT_HPP

#include "entity_id.hpp"

namespace blob_royale::simulation {

// canonical: despawn_event -- one roster removal a stage decided and the commit applies.
//
// A system that decides an entity must leave the roster emits this rather than destroying the
// entity itself, so every removal a tick performs lands at one point: the commit destroys each
// named entity from every component store and then rebuilds the spatial index from the survivors
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick" phase 10).
// An event naming an entity that no longer exists is applied as a no-op, because `destroy_entity`
// is total.
//
// This is not `DespawnCommand`: a command is a client request applied in phase 0, before any pair
// is built; this event is an engine-side consequence applied at the commit of the tick that
// emitted it.
// related: world_event_registry.hpp -- the closed list of event kinds.
// related: commands/despawn_command.hpp -- the client-side request with the same effect.
struct DespawnEvent final {
  EntityId entity;

  friend bool operator==(const DespawnEvent&, const DespawnEvent&) = default;
};

} // namespace blob_royale::simulation

#endif
