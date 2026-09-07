#ifndef BLOB_ROYALE_SIMULATION_COMMANDS_SPAWN_COMMAND_HPP
#define BLOB_ROYALE_SIMULATION_COMMANDS_SPAWN_COMMAND_HPP

#include "controller_id.hpp"

namespace blob_royale::simulation {

// canonical: spawn_command -- the request for a body for one controller.
//
// The command names the deciding agent and nothing else. The engine draws the new EntityId from
// the tick's EntityIdReservation and the mode's SpawnPolicy chooses the seat, so no command source
// can pick an id or a spawn point
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick").
//
// This is why the identity a spawn addresses is its ControllerId rather than an EntityId: it is
// the only identity the command carries, so it is what InputBatch orders and de-duplicates on.
// A controller therefore asks for at most one body per tick.
// related: command_registry.hpp -- the closed list of command kinds.
// related: entity_id_reservation.hpp -- the ids a spawn may be given.
struct SpawnCommand final {
  ControllerId controller;

  friend bool operator==(const SpawnCommand&, const SpawnCommand&) = default;
};

} // namespace blob_royale::simulation

#endif
