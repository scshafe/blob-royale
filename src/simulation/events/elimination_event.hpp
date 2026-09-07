#ifndef BLOB_ROYALE_SIMULATION_EVENTS_ELIMINATION_EVENT_HPP
#define BLOB_ROYALE_SIMULATION_EVENTS_ELIMINATION_EVENT_HPP

#include "entity_id.hpp"

namespace blob_royale::simulation {

// canonical: elimination_event -- one entity a mode's rule removed from contention this tick.
//
// **Elimination and roster removal are two events because they belong to two stages**, which is
// the engine's own stage contract and not one mode's protocol: a `kPostKernel` system reads
// committed positions and writes consequences, and roster bookkeeping is `kLifecycle` work
// (`docs/architecture/0004-gameplay-architecture.md` § "The tick: one fixed kernel, three named
// stages"). A `kPostKernel` system therefore names the entity here, and a `kLifecycle` system reads
// the whole tick's set at once and decides what removal means.
//
// The event carries the entity and nothing else. That is deliberate and is the reason it is
// mode-agnostic: what a set of eliminations *means* -- a shared rank, a respawn timer, a life
// decrement, a spectator seat -- is the consuming system's decision, and every one of those needs
// the whole tick's set rather than one entity at a time.
//
// Its producer and consumer are `royale`: `zone_elimination` at `kPostKernel` emits one per entity
// whose grace has run out, and `placement_recorder` at `kLifecycle` computes one shared placement
// over the set, destroys each entity, and emits a DespawnEvent for each
// (`docs/architecture/0005-royale-mode.md` § "Elimination and placement"). A second mode that
// eliminates -- last-team-standing, a life-count deathmatch -- reuses this kind with its own
// consumer and adds nothing here.
// related: world_event_registry.hpp -- the closed list of event kinds.
// related: events/despawn_event.hpp -- the roster removal an elimination leads to.
struct EliminationEvent final {
  EntityId entity;

  friend bool operator==(const EliminationEvent&, const EliminationEvent&) = default;
};

} // namespace blob_royale::simulation

#endif
