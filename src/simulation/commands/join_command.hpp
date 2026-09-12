#ifndef BLOB_ROYALE_SIMULATION_COMMANDS_JOIN_COMMAND_HPP
#define BLOB_ROYALE_SIMULATION_COMMANDS_JOIN_COMMAND_HPP

#include "controller_id.hpp"
#include "npc_declaration.hpp"

#include <cstdint>
#include <optional>

namespace blob_royale::simulation {

// canonical: join_command -- one controller asks for a seat in the lobby.
//
// Server-issued, never a client's. A person's session submits one for itself whenever it observes
// that it holds no seat, exactly as it submits a spawn whenever it observes that it has no body;
// the bot reconciliation submits one for each bot it creates. A client cannot send one, because a
// client that could choose its seat could choose somebody else's.
//
// `seat_index` is the difference between the two callers. **Absent, the join is a person's**: phase
// 0 seats the controller in the lowest empty seat, and if none is empty and the match has not
// started -- `lobby` or `countdown` -- it takes the lowest-indexed NPC seat instead, which is the
// owner's 2026-09-08 decision that a person displaces a bot. **Present, the join is a bot's**: it
// fills exactly the seat that declared its kind and still has no controller, and nothing else, so a
// bot created for a seat that was cleared or resized away in the meantime seats nowhere and the
// reconciliation, which created it, retires it.
//
// A controller that already holds a seat is left where it is, so the person's session may keep
// asking without ever moving; a join that finds nothing changes nothing, and the session asks again
// a tenth of a second later. A displaced bot's controller then sits nowhere, which is what the
// reconciliation reads to close it (`docs/architecture/0006-lobbies-as-rooms.md` § "Seats, people,
// and bots").
// related: command_registry.hpp -- the closed list of command kinds.
// related: leave_command.hpp -- the other half of a controller's time in a seat.
struct JoinCommand final {
  ControllerId controller;
  std::optional<std::uint64_t> seat_index;
  // Reconciliation guards its queued join with the full observed declaration. Absence keeps
  // ordinary human and literal indexed replay behavior; it is never inferred from live state.
  std::optional<NpcDeclaration> expected_npc{};

  friend bool operator==(const JoinCommand&, const JoinCommand&) = default;
};

} // namespace blob_royale::simulation

#endif
