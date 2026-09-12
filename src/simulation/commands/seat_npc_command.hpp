#ifndef BLOB_ROYALE_SIMULATION_COMMANDS_SEAT_NPC_COMMAND_HPP
#define BLOB_ROYALE_SIMULATION_COMMANDS_SEAT_NPC_COMMAND_HPP

#include "controller_id.hpp"
#include "seat_roster.hpp"

#include <cstdint>

namespace blob_royale::simulation {

// canonical: seat_npc_command -- fill one empty seat with a declaration for a named NPC kind.
//
// The command names the deciding agent, the seat, and the kind. **`controller` is the server's own
// stamp** rather than a client field, exactly as it is for every other lobby command
// (`set_seat_count_command.hpp`).
//
// **The kind is validated as a name here and as a *registration* somewhere else.**
// `SeatKindName::create` enforces the published `kind_name` grammar, which is all the simulation
// can enforce: the registry that says whether `chaser` exists lives in `blob_controllers`, which
// the simulation cannot reach. The boundary that decodes this command holds the same closed list
// the `welcome` frame published to the client, so a kind no row registers is refused before it
// reaches a tick and an `NpcSeat` naming an unbuildable bot cannot be committed
// (`src/protocol/command_decoding.hpp`).
//
// **It fills an empty seat and never replaces an occupied one.** Two clients seating the same seat
// in one tick therefore resolve by the batch's existing order -- ascending `ControllerId` within
// this kind -- and the second is a no-op rather than an error, which is the rule
// `game_simulation.cpp`'s phase-0 pass states and applies. First-wins rather than last-wins is what
// keeps a click from evicting the person or bot already sitting there: replacing an occupant is
// `clear_seat` followed by this, in that order, which the application ranks permit within a single
// tick.
// related: command_registry.hpp -- the closed list of command kinds.
// related: clear_seat_command.hpp -- the other half of "replace who is in this seat".
struct SeatNpcCommand final {
  ControllerId controller;
  std::uint64_t seat_index;
  SeatKindName kind;
  std::optional<BotProfileName> profile_name{};

  [[nodiscard]] NpcDeclaration declaration() const { return {kind, profile_name}; }

  friend bool operator==(const SeatNpcCommand&, const SeatNpcCommand&) = default;
};

} // namespace blob_royale::simulation

#endif
