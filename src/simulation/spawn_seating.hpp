#ifndef BLOB_ROYALE_SIMULATION_SPAWN_SEATING_HPP
#define BLOB_ROYALE_SIMULATION_SPAWN_SEATING_HPP

#include "component_store.hpp"
#include "entity_id.hpp"
#include "physics_body.hpp"
#include "vector2.hpp"

#include <span>

namespace blob_royale::simulation {

class GameWorld;
class TerrainDefinition;

// canonical: spawn_seating -- the occupancy predicate and the at-rest seating write, shared by
// every site that puts a body on the arena.
//
// Both were private to `spawn_system.cpp` while the engine's `SpawnSystem` was the only site that
// seated a body. A mode that returns a player to a point of its own choosing -- a race putting a
// fallen racer back at the last gate it took -- has to seat exactly as the engine does, or it could
// seat an unsupported body or one in contact with another. Two seating
// sites, one implementation (`docs/architecture/0007-king-of-the-hill-and-race-modes.md`
// § "Where the framework has to move"). `SpawnSystem` still owns everything else about seating a
// joiner: which entities are offered, the policy call, the free-point vector, and the rotation
// counter.
//
// Neither function is a system and neither reads the tick: they are the two value operations a
// seating site composes, and each is total over any world.
// related: spawn_system.hpp -- the engine's seating site, and the one that owns the policy socket.

// A seat must support the complete player disc and be beyond contact range of every actual body.
// Uses canonical terrain support and each body's effective radius, including undeclared fallback;
// store order does not affect the answer. Unsupported and occupied seats both defer seating.
[[nodiscard]] bool
seat_is_supported_and_unoccupied(const Vector2& point,
                                 std::span<const ComponentStore<PhysicsBody>::Entry> bodies,
                                 double player_radius, const TerrainDefinition& terrain);

// Gives `entity` a `PhysicsBody` at rest at `position`: zero velocity and zero stored
// acceleration, so a seated entity moves only once its controller asks it to, and carrying the
// **configured** radius rather than `PhysicsBody::kUndeclaredRadius`. Existing obstacles may have
// other effective radii; the shared admission query accounts for them. The new player carries
// the default mass, restitution, and collision layers of an
// ordinary ground-bound blob. A body the entity already carries is replaced; whether that is the
// right thing to do is the caller's decision, and the engine's seating site never asks it of an
// entity with one. Clears a controller's prior normalized thrust intent, if present, without
// changing this tick's recorded commands. Thus even zero-delay replacement requires fresh input,
// while a command already received for this tick may steer the newly seated body.
void seat_body_at_rest(GameWorld& world, EntityId entity, const Vector2& position,
                       double player_radius);

} // namespace blob_royale::simulation

#endif
