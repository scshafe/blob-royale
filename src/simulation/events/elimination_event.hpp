#ifndef BLOB_ROYALE_SIMULATION_EVENTS_ELIMINATION_EVENT_HPP
#define BLOB_ROYALE_SIMULATION_EVENTS_ELIMINATION_EVENT_HPP

#include "entity_id.hpp"

namespace blob_royale::simulation {

// canonical: elimination_event -- one entity a mode's rule removed from contention this tick.
//
// Elimination and roster removal are deliberately two events. A `kPostKernel` system decides that
// an entity is out and emits this; a `kLifecycle` system reads the whole tick's set at once,
// records one shared placement for it, and emits a DespawnEvent for each member
// (`docs/architecture/0005-royale-mode.md` § "Elimination and placement"). Carrying only the
// entity is what lets placement be computed over the set rather than per entity.
// related: world_event_registry.hpp -- the closed list of event kinds.
// related: events/despawn_event.hpp -- the roster removal an elimination leads to.
struct EliminationEvent final {
  EntityId entity;

  friend bool operator==(const EliminationEvent&, const EliminationEvent&) = default;
};

} // namespace blob_royale::simulation

#endif
