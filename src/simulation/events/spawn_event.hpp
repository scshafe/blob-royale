#ifndef BLOB_ROYALE_SIMULATION_EVENTS_SPAWN_EVENT_HPP
#define BLOB_ROYALE_SIMULATION_EVENTS_SPAWN_EVENT_HPP

#include "controller_id.hpp"
#include "entity_id.hpp"

namespace blob_royale::simulation {

// canonical: spawn_event -- one entity brought into existence during this tick.
//
// The event names the id the tick drew from its EntityIdReservation and the controller that asked
// for a body, which is the pairing a consuming system needs and the one place the two identity
// spaces meet outside `Controllable`. Phase 0 creates the entity and the engine's SpawnSystem
// seats it, and neither emits this event: it has no consumer, and a produced event nothing reads
// would be structure without a reader. Step 21 is where a mode first consumes one.
// related: world_event_registry.hpp -- the closed list of event kinds.
// related: commands/spawn_command.hpp -- the request this event answers.
struct SpawnEvent final {
  EntityId entity;
  ControllerId controller;

  friend bool operator==(const SpawnEvent&, const SpawnEvent&) = default;
};

} // namespace blob_royale::simulation

#endif
