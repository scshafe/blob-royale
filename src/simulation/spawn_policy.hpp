#ifndef BLOB_ROYALE_SIMULATION_SPAWN_POLICY_HPP
#define BLOB_ROYALE_SIMULATION_SPAWN_POLICY_HPP

#include "entity_id.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace blob_royale::simulation {

class GameWorld;
class TickContext;

// canonical: spawn_policy -- chooses which map spawn point an entity is seated at.
// @extension-point game_mode
//
// This is one of the kernel's exactly two policy sockets, evaluated at a fixed point in phase 0
// against declared data (`docs/architecture/0003-deterministic-simulation-contract.md`
// § "Canonical tick"). It is **pure**: the engine's SpawnSystem owns ascending-EntityId iteration
// over the entities awaiting a seat, the occupancy test that produced `spawn_point_is_free`, the
// world-owned rotation counter and its advance, and the seating write itself. A policy computes an
// index and nothing else, which is what keeps seating a deterministic function of the committed
// world however many modes exist.
//
// Returning nullopt **defers** the entity one tick; it is offered again in the same ascending
// order on a later tick. Deferral is the only way to decline, and it is a first-class answer: a
// full ring defers, and royale defers every joiner who arrives while a match is running so a match
// is a closed field (`docs/architecture/0005-royale-mode.md` § "Spawning").
//
// A returned index must be in range and must name a free point. Either violation is a mode defect
// rather than a client disagreement, so the SpawnSystem fails the tick rather than seating two
// entities in contact or reading past the map's points.
//
// Two implementations of this seam: `IdleSpawnPolicy`, which the engine declares when no mode
// does, and royale's `RotatingRingSpawnPolicy` in Step 21.
// related: spawn_system.hpp -- the engine mechanism that owns everything but this choice.
// related: map_definition.hpp -- the `spawn_points()` projection an index addresses.
class SpawnPolicy {
public:
  virtual ~SpawnPolicy() = default;
  SpawnPolicy(const SpawnPolicy&) = delete;
  SpawnPolicy& operator=(const SpawnPolicy&) = delete;

  // Returns an index into `context.map().spawn_points()`, or nullopt to defer this entity one
  // tick. `spawn_point_is_free` is parallel to that span and already accounts for every entity
  // seated earlier in this same phase, so an index this call accepts cannot collide with one an
  // earlier call took.
  [[nodiscard]] virtual std::optional<std::size_t>
  choose_spawn_point(const GameWorld& world, const TickContext& context, EntityId entity,
                     std::size_t rotation_counter,
                     std::span<const bool> spawn_point_is_free) const = 0;

protected:
  SpawnPolicy() = default;
};

} // namespace blob_royale::simulation

#endif
