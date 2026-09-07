#ifndef BLOB_ROYALE_SIMULATION_EVENTS_SPAWN_EVENT_HPP
#define BLOB_ROYALE_SIMULATION_EVENTS_SPAWN_EVENT_HPP

#include "controller_id.hpp"
#include "entity_id.hpp"

namespace blob_royale::simulation {

// canonical: spawn_event -- one entity brought into existence during this tick.
//
// The event names the id the tick drew from its EntityIdReservation and the controller that asked
// for a body, which is the pairing a consuming system needs and the one place the two identity
// spaces meet outside `Controllable`. Phase 0's spawn seating is the producer and arrives with the
// mode's spawn policy in Step 19; nothing emits one yet.
// related: world_event_registry.hpp -- the closed list of event kinds.
// related: commands/spawn_command.hpp -- the request this event answers.
struct SpawnEvent final {
  EntityId entity;
  ControllerId controller;

  friend bool operator==(const SpawnEvent&, const SpawnEvent&) = default;
};

} // namespace blob_royale::simulation

#endif
