#ifndef BLOB_ROYALE_RUNTIME_RUNTIME_LIMITS_HPP
#define BLOB_ROYALE_RUNTIME_RUNTIME_LIMITS_HPP

#include "simulation_limits.hpp"

#include <cstddef>
#include <cstdint>

namespace blob_royale::runtime {

// canonical: runtime_limits -- every bound the runtime's boundary values enforce.
//
// The runtime is the only place untrusted input becomes simulation input, so every unbounded
// thing a command source can do is bounded here rather than in the value that consumes it. Each
// bound is tied by a static assertion to the simulation limit it must not exceed, so a widened
// bound fails the build instead of failing a tick.
// related: simulation_limits.hpp -- the deterministic domain's own ceilings.
// related: command_mailbox.hpp -- the bounded buffer these limits describe.

// One tick's pending commands. The mailbox supersedes rather than appends when a command of the
// same kind for the same identity is already pending, so this is a bound on *distinct commanded
// identities* and not on submission rate: one client spamming thrusts occupies one slot and can
// never evict another client's input.
inline constexpr std::size_t kMaximumMailboxCommandCount = 2'048;

// A tick's reservation is `spawn_count + kSystemCreatedEntityHeadroom` ids and its width is
// validated against the world's seat count, so a mailbox that could hold more spawns than the
// widest legal reservation would turn a full mailbox into a hard tick failure.
static_assert(kMaximumMailboxCommandCount + 1 <= simulation::kMaximumEntityIdReservationCount,
              "a full mailbox of spawns plus the system headroom must still be a legal "
              "EntityIdReservation");
static_assert(kMaximumMailboxCommandCount <= simulation::kMaximumInputBatchCommandCount,
              "a full mailbox must still be a legal InputBatch submission");

// The width of every tick's EntityIdReservation beyond that tick's spawn count is
// `simulation::kSystemCreatedEntityHeadroom`, which this library reads rather than restates. It
// used to be declared here and again in `tests/fixtures/replay_fixture.hpp` with a comment saying
// the two must agree; it now has one definition in the one library both can reach, so agreement is
// structural instead of requested.

// Concurrently open controllers. A controller drives at most one live entity at a time, so the
// world's seat count is the honest ceiling: more open controllers than seats could never all be
// embodied. Entries live exactly as long as their session, so this bounds concurrency and not
// lifetime churn.
inline constexpr std::size_t kMaximumControllerDirectoryEntryCount =
    simulation::kMaximumEntityCount;

// Presentation strings the directory holds. Both arrive from a trusted proxy's headers or from a
// bot roster, so both are bounded and control-character-free before they are stored; this is the
// boundary that keeps proxy-supplied strings out of the deterministic core
// (`docs/architecture/0004-gameplay-architecture.md`, amendment of 2026-09-06).
inline constexpr std::size_t kMaximumControllerKindLength = 64;
inline constexpr std::size_t kMaximumDisplayNameLength = 64;

} // namespace blob_royale::runtime

#endif
