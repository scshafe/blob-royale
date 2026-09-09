#ifndef BLOB_ROYALE_SIMULATION_COMMANDS_CLEAR_SEAT_COMMAND_HPP
#define BLOB_ROYALE_SIMULATION_COMMANDS_CLEAR_SEAT_COMMAND_HPP

#include "controller_id.hpp"

#include <cstdint>

namespace blob_royale::simulation {

// canonical: clear_seat_command -- empty one seat that holds a declared NPC.
//
// The command names the deciding agent and the seat. **`controller` is the server's own stamp**
// rather than a client field, exactly as it is for every other lobby command
// (`set_seat_count_command.hpp`).
//
// **It clears an `NpcSeat` and leaves a `ControllerSeat` alone**, and the reason is authority
// rather than politeness. A `ControllerSeat` is held by a live session, and the only thing that
// knows a session exists is the runtime; a tick that emptied a connected person's seat would be
// contradicted by the next reconciliation, which seats that session again because it is still
// connected. The command would not be an action, it would be a lie the world corrects one tick
// later. Who may unseat a person is a runtime question and is plan Step 7's, and this command
// deliberately does not answer it.
//
// Clearing a seat that is already empty is a no-op, so pressing the same control twice is one
// decision rather than an error.
// related: command_registry.hpp -- the closed list of command kinds.
// related: seat_npc_command.hpp -- the other half of "replace who is in this seat".
struct ClearSeatCommand final {
  ControllerId controller;
  std::uint64_t seat_index;

  friend bool operator==(const ClearSeatCommand&, const ClearSeatCommand&) = default;
};

} // namespace blob_royale::simulation

#endif
