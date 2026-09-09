#ifndef BLOB_ROYALE_SIMULATION_COMMANDS_SET_SEAT_COUNT_COMMAND_HPP
#define BLOB_ROYALE_SIMULATION_COMMANDS_SET_SEAT_COUNT_COMMAND_HPP

#include "controller_id.hpp"

#include <cstdint>

namespace blob_royale::simulation {

// canonical: set_seat_count_command -- how many seats the lobby should have.
//
// The command names the deciding agent and the number, and nothing else. **`controller` is the
// server's own stamp**, not a field a client fills in: the wire envelope for this kind carries only
// `seat_count`, and the session boundary writes in the identity it issued
// (`docs/protocol/v2.md` § "Client command model"). That is what makes the identity unforgeable and
// what makes this command orderable at all -- see `command_registry.hpp`, `addressed_identity_of`,
// for why a lobby command's addressed identity is its sender rather than an entity.
//
// **Anyone in the lobby may send this**, which is a deliberate trust choice recorded once in
// `game_simulation.cpp` at the pass that applies it. It is safe here for a narrower reason worth
// stating separately: a seat count is a bounded integer, `InputBatch::create` rejects one outside
// `[1, kMaximumLobbySeatCount]`, and the tick refuses a shrink that would remove an occupied seat,
// so the worst a hostile value can do is resize a lobby somebody has to resize back.
//
// `seat_count` is the count the sender wants, not a delta, so two clients who both press "4"
// agree rather than compounding. A count equal to the current one is a no-op the tick still
// applies, and applying it is cheaper than asking whether it changed anything.
// related: command_registry.hpp -- the closed list of command kinds.
// related: seat_roster.hpp -- the roster this resizes, and its bounds.
struct SetSeatCountCommand final {
  ControllerId controller;
  std::uint64_t seat_count;

  friend bool operator==(const SetSeatCountCommand&, const SetSeatCountCommand&) = default;
};

} // namespace blob_royale::simulation

#endif
