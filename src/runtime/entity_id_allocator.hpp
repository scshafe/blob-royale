#ifndef BLOB_ROYALE_RUNTIME_ENTITY_ID_ALLOCATOR_HPP
#define BLOB_ROYALE_RUNTIME_ENTITY_ID_ALLOCATOR_HPP

#include "entity_id.hpp"
#include "entity_id_reservation.hpp"
#include "game_simulation.hpp"
#include "runtime_limits.hpp"

#include <atomic>
#include <cstdint>

namespace blob_royale::runtime {

// canonical: entity_id_allocator -- the one monotonic issuer of EntityId values for one timeline.
//
// ADR 0003 § "Accepted simulation input" puts exactly one monotonic allocator outside the
// simulation and makes each tick's contiguous `EntityIdReservation` part of that tick's input
// value. This is that allocator. Every id a tick brings into existence -- a spawn command's body
// and any entity a system creates -- is drawn from the block this issues and from nowhere else,
// which is what makes replaying a recorded command log reproduce simulation-created ids exactly.
//
// **The policy, matching `tests/fixtures/replay_fixture.hpp` exactly.** Each tick receives
// `spawn_count(tick) + kSystemCreatedEntityHeadroom` ids from a cursor that advances by that same
// width whether or not the tick used them. The headroom is at least one, so **every tick receives a
// non-empty block and the runtime never hands a tick `InputBatch::empty()`**: royale creates its
// zone entity from the reservation on its first running tick, so an empty block there is a hard
// simulation failure rather than a tick that quietly does less
// (`docs/architecture/0005-royale-mode.md`; plan Step 21's execution note).
//
// **The cursor opens above everything already committed, and that is enforced rather than
// documented.** `GameWorld::create(configuration, map, seed)` numbers a map's static bodies
// `kMinimumEntityId + index`, and `ScenarioLoader` seeds entities at ids a CSV chose. If the cursor
// opened at `kMinimumEntityId`, the first spawn would be handed an id a wall or a seeded player
// already holds and `create_entity` would graft the new entity's components onto that body --
// silent map corruption with no failing assertion anywhere.
// `above_committed_state` therefore reads the committed roster and the map and opens at
// `max(kMinimumEntityId + static_body_count, highest committed id + 1)`, which covers both sources
// at once and cannot fall behind a construction path this file does not know about.
//
// **Thread-safety contract.** `reserve_for_tick` is called by the runtime worker and only by the
// runtime worker: it is the single writer. `next_entity_id` is a relaxed read for any thread, and
// `CommandSink` uses it to refuse a despawn naming an id no tick has issued yet -- which is what
// keeps a client from naming an id inside a future reservation, the one despawn
// `InputBatch::create` rejects outright. The cursor only ever rises, so an id below an observed
// cursor is below every later reservation as well and the check cannot go stale in the unsafe
// direction.
// related: entity_id_reservation.hpp -- the block value this issues.
// related: command_sink.hpp -- the boundary that reads the cursor.
// related: ../../tests/fixtures/replay_fixture.hpp -- the same policy, for a replay with no
// runtime.
class EntityIdAllocator final {
public:
  // Opens the cursor at an explicit id. Throws SimulationValidationError when the id is outside
  // `[kMinimumEntityId, kMaximumEntityId]`.
  [[nodiscard]] static EntityIdAllocator create(simulation::EntityId first_issuable_entity_id);

  // Opens the cursor above every id the simulation has already committed and above the whole block
  // its map reserves for static bodies. This is the production factory and the one the runtime
  // uses; `create` exists for the tests that name a cursor directly.
  [[nodiscard]] static EntityIdAllocator
  above_committed_state(const simulation::GameSimulation& game_simulation);

  EntityIdAllocator(const EntityIdAllocator&) = delete;
  EntityIdAllocator(EntityIdAllocator&&) = delete;
  EntityIdAllocator& operator=(const EntityIdAllocator&) = delete;
  EntityIdAllocator& operator=(EntityIdAllocator&&) = delete;
  ~EntityIdAllocator() = default;

  // The next tick's block: `spawn_count + kSystemCreatedEntityHeadroom` contiguous ids, never
  // empty. Advances the cursor by the block width whether or not the tick draws from it.
  //
  // Throws SimulationValidationError when the block would run past kMaximumEntityId or would be
  // wider than kMaximumEntityIdReservationCount, both of which are hard failures rather than a
  // narrowed block: a tick that silently received fewer ids than its spawns need would drop a
  // player's body with no failure anywhere.
  //
  // Called by the runtime worker only.
  [[nodiscard]] simulation::EntityIdReservation reserve_for_tick(std::uint64_t spawn_count);

  // The lowest id no tick has been given yet. Readable from any thread.
  [[nodiscard]] simulation::EntityId next_entity_id() const noexcept;

  // Ids handed to ticks so far, used or not. Readable from any thread.
  [[nodiscard]] std::uint64_t reserved_entity_id_count() const noexcept;

private:
  explicit EntityIdAllocator(simulation::EntityId first_issuable_entity_id) noexcept;

  simulation::EntityId::Value first_issuable_entity_id_;
  // Single-writer: only the runtime worker stores, every other thread loads.
  std::atomic<simulation::EntityId::Value> next_entity_id_;
};

} // namespace blob_royale::runtime

#endif
