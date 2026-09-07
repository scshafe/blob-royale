#ifndef BLOB_ROYALE_SIMULATION_COMMANDS_DESPAWN_COMMAND_HPP
#define BLOB_ROYALE_SIMULATION_COMMANDS_DESPAWN_COMMAND_HPP

#include "entity_id.hpp"

namespace blob_royale::simulation {

// canonical: despawn_command -- the request to destroy one entity.
//
// A despawn is applied before any other command of the tick, so the destroyed entity is absent
// from every pair built in the same tick
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick").
//
// A despawn naming an entity that does not exist in the committed world is ignored by the tick
// rather than failing it, because the command source is a network session and a hard failure
// would let one client stop the match. The one despawn InputBatch::create rejects outright is one
// naming an id inside the tick's own EntityIdReservation, which no committed entity can hold.
// related: command_registry.hpp -- the closed list of command kinds.
struct DespawnCommand final {
  EntityId entity;

  friend bool operator==(const DespawnCommand&, const DespawnCommand&) = default;
};

} // namespace blob_royale::simulation

#endif
