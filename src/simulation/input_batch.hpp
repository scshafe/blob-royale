#ifndef BLOB_ROYALE_SIMULATION_INPUT_BATCH_HPP
#define BLOB_ROYALE_SIMULATION_INPUT_BATCH_HPP

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "entity_id_reservation.hpp"

#include <span>
#include <vector>

namespace blob_royale::simulation {

// canonical: input_batch -- the one validated command value a tick may read.
// @extension-point simulation_input
//
// `GameSimulation::step(FixedDelta, const InputBatch&)` reads exactly one input value per tick and
// never an ambient queue, callback, socket, clock, or global. A tick with no commands is that same
// call with `InputBatch::empty()`, not a different code path
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Accepted simulation input").
//
// **Creation canonicalizes and rejects; phase 0 repeats neither.** `create` performs, in this
// order:
//
//  1. Rejects a submitted command count above kMaximumInputBatchCommandCount.
//  2. Rejects any command whose kind is absent from `accepted_kinds`.
//  3. Rejects a thrust whose direction has a component outside [-1, 1].
//  4. Rejects a despawn naming an id inside this batch's own EntityIdReservation.
//  5. Rejects a lobby command naming a seat index at or above kMaximumLobbySeatCount, or a seat
//     count outside [1, kMaximumLobbySeatCount].
//  6. Keeps the last submitted command of each kind for each addressed identity, discarding the
//     earlier ones, so the batch carries at most one command of each kind per identity.
//  7. Orders the survivors by phase 0's application order.

// **Rule 5 is the engine's bound and deliberately not the lobby's.** A seat index this factory
// accepts may still name no seat in the roster the tick is about to read, because the roster's size
// is world state and this factory has no world; that disagreement is ignored by phase 0 exactly as
// a despawn for an entity that does not exist is. What rule 5 buys is that a client cannot make a
// seat index or a seat count unbounded, which is the only part of the question that can be answered
// without the world -- and it is the part that would otherwise be an allocation an attacker chose.
//
// **The canonical order** is the phase-0 application order of § "Canonical tick" -- despawns, then
// spawns, then every remaining kind in ascending enumerator value -- and, within one kind,
// ascending by the identity the command addresses. A spawn addresses no entity, because the engine
// chooses the EntityId, so spawns are ordered by ascending ControllerId and are placed as one
// contiguous group between the despawns and the remaining kinds, which is where phase 0 applies
// them. The four lobby kinds address a ControllerId for the same structural reason and are ordered
// by ascending sender, which is what makes "the first of two clients to seat one seat wins" a
// stated rule rather than an accident of arrival. The result is that phase 0 is one forward pass
// that neither sorts, regroups, de-duplicates, nor range-checks.
//
// **Deviations recorded at Step 16, where an accepted decision had to be pinned:**
//
//  * An unaccepted kind is **rejected** rather than discarded. ADR 0004 § "Commands" says
//    `CommandSink` already rejects an unaccepted kind at submission, so a kind reaching this
//    factory is the boundary disagreeing with the engine rather than a client disagreeing with
//    the world, and § "Accepted simulation input" keeps internal invariant violations hard
//    failures. Discarding here would also convert a boundary defect into a silently dropped
//    input, which is the failure mode a named validation code exists to prevent.
//  * "An entity that both spawns and despawns in one batch is rejected" is enforced as *a despawn
//    naming an id inside this batch's EntityIdReservation*. A SpawnCommand carries only a
//    ControllerId -- the engine chooses the id -- so the two identity spaces cannot be joined
//    without world state, which this factory deliberately does not have. The reservation is the
//    one place the spawn side of that rule is visible: an id inside it names no committed entity
//    and is exactly an id this tick may create.
//
// A value that fails these rules is not a simulation failure, because it never becomes an
// InputBatch.
// related: command_registry.hpp -- the closed list of kinds a batch may carry.
// related: entity_id_reservation.hpp -- the ids the tick may bring into existence.
class InputBatch final {
public:
  // Canonicalizes and validates one tick's submitted commands. Throws SimulationValidationError
  // for every rejection listed above.
  [[nodiscard]] static InputBatch create(std::vector<Command> commands,
                                         CommandKindMask accepted_kinds,
                                         EntityIdReservation entity_id_reservation);

  // The no-input tick: no commands and no reservation, so a tick that was handed nothing also
  // cannot create an entity. This is the same `step` call as any other tick.
  [[nodiscard]] static InputBatch empty();

  InputBatch(const InputBatch&) = default;
  InputBatch(InputBatch&&) noexcept = default;
  InputBatch& operator=(const InputBatch&) = default;
  InputBatch& operator=(InputBatch&&) noexcept = default;
  ~InputBatch() = default;

  [[nodiscard]] std::span<const Command> commands() const& noexcept { return commands_; }
  [[nodiscard]] std::span<const Command> commands() const&& = delete;

  // Returned by value: the world draws from its own copy, so the batch keeps describing the
  // unchanging input this tick was given.
  [[nodiscard]] EntityIdReservation entity_id_reservation() const noexcept {
    return entity_id_reservation_;
  }

  friend bool operator==(const InputBatch&, const InputBatch&) = default;

private:
  InputBatch(std::vector<Command> commands, EntityIdReservation entity_id_reservation) noexcept;

  std::vector<Command> commands_;
  EntityIdReservation entity_id_reservation_;
};

} // namespace blob_royale::simulation

#endif
