#ifndef BLOB_ROYALE_SIMULATION_SPAWN_SYSTEM_HPP
#define BLOB_ROYALE_SIMULATION_SPAWN_SYSTEM_HPP

#include "map_definition.hpp"
#include "simulation_config.hpp"
#include "spawn_policy.hpp"

#include <cstddef>
#include <memory>

namespace blob_royale::simulation {

class GameWorld;
class TickContext;

// canonical: spawn_system -- the engine mechanism that seats entities awaiting a body.
//
// This runs **inside kernel phase 0**, immediately after the tick's batch has been applied. It is
// not a staged SimulationSystem and cannot be declared, reordered, or removed by a mode: seating
// is kernel mechanism with exactly one policy socket cut into it, the mode's SpawnPolicy
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick";
// `docs/architecture/0004-gameplay-architecture.md` § "The tick: one fixed kernel, three named
// stages").
//
// The split is the whole point of the seam. This class owns:
//
//   * **which entities are offered.** An entity awaits a body when it carries a `Controllable` and
//     no `PhysicsBody`, which is exactly what a spawn command leaves behind. They are offered in
//     ascending EntityId order, which is where the ordering guarantee comes from.
//   * **the occupancy test.** A spawn point is occupied when some live body's centre lies within
//     `2r + kPositionTolerance` of it -- the same predicate `detect_player_pair_contact` applies,
//     so two entities are never seated in contact. An entity seated earlier in this same phase
//     occupies its point for every later seating in the same phase. The predicate itself and the
//     at-rest write below are `spawn_seating.hpp`, shared with any mode system that returns a
//     player to a point of its own; this class owns *when* they are applied and to *whom*.
//   * **the rotation counter**, which lives in `MatchState` and advances to one past the index a
//     seating used, so a policy that probes forward from it spreads consecutive joiners.
//   * **the seating write**: a `PhysicsBody` at rest -- zero velocity and zero stored acceleration
//     -- at the chosen point.
//
// The policy owns exactly one decision: which index, or none. A policy that returns an
// out-of-range index or names an occupied point is a mode defect and fails the tick, because
// either would seat a body the world cannot represent.
// related: spawn_policy.hpp -- the one decision this mechanism delegates.
// related: game_simulation.hpp -- the kernel phase that runs this.
class SpawnSystem final {
public:
  // Takes the mode's declared policy, read once at construction like every other declaration.
  explicit SpawnSystem(std::unique_ptr<const SpawnPolicy> policy) noexcept;

  SpawnSystem(const SpawnSystem&) = delete;
  SpawnSystem(SpawnSystem&&) noexcept = default;
  SpawnSystem& operator=(const SpawnSystem&) = delete;
  SpawnSystem& operator=(SpawnSystem&&) noexcept = default;
  ~SpawnSystem() = default;

  // Offers every entity awaiting a body to the policy and performs the seatings it chose. Returns
  // how many entities were seated, which is what tells the kernel whether the intake spatial index
  // has to be re-derived before phase 2 queries it.
  //
  // Throws SimulationValidationError when the policy returns an index that is out of range or
  // already taken.
  [[nodiscard]] std::size_t seat_pending_entities(GameWorld& world,
                                                  const TickContext& context) const;

private:
  std::unique_ptr<const SpawnPolicy> policy_;
};

// Rejects a map whose spawn points cannot seat a disc of the configured radius.
//
// A spawn point is content and a radius is configuration, so a map is authored without knowing one
// (`map_definition.hpp`). This is the one place the two meet, and it runs at construction so a
// mismatch is a startup rejection with a named cause rather than a bounds failure on the tick that
// first seated an entity there. Throws SimulationValidationError naming the point's index.
void require_spawn_points_are_seatable(const SimulationConfig& configuration,
                                       const MapDefinition& map);

} // namespace blob_royale::simulation

#endif
