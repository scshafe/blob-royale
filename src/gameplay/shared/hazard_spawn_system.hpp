#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_HAZARD_SPAWN_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_HAZARD_SPAWN_SYSTEM_HPP

#include "shared/hazard_archetype.hpp"
#include "simulation_system.hpp"

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {

// canonical: hazard_spawn -- the one system that turns a configured archetype into a body.
// @extension-point simulation_system
//
// It reads `GameModeConfiguration::hazards`, which the application has already validated, and seats
// a crossing body for each kind whose interval is due this tick. It adds no configuration and no
// new rejection: every value it reads was checked at startup by `HazardArchetype::create`.
//
// **Every geometric choice comes from `GameWorld::random()`, in one fixed order, and nothing else
// is a randomness source.** Per spawned hazard the draws are exactly three, always in this order:
// the entry edge, the point along that edge, and the point on the opposite edge it is aimed at. A
// replay of `(map, mode configuration, seed, command log)` therefore reproduces every crossing
// exactly, which is `docs/architecture/0004-gameplay-architecture.md` § "Determinism obligations
// for framework code". `draw_count` is committed in every snapshot, so two runs that diverge in how
// many hazards they drew for diverge visibly at the first differing tick.
//
// **It runs at `kLifecycle`, and that is a design decision rather than a scheduling convenience.**
// Creating an entity is roster bookkeeping, which is what the stage is for and what
// `placement_recorder` already does there. It also happens to be the stage that makes the entity
// budget work, and the two reasons agree. Every tick's `EntityIdReservation` is exactly
// `spawn_count + kSystemCreatedEntityHeadroom` ids wide and the headroom is **one**
// (`src/runtime/runtime_limits.hpp`), because it was sized for the single entity royale's
// `zone_shrink` creates on its first running tick. Widening it would advance the allocator's
// monotonic cursor on every tick and renumber every simulation-created id in every recorded replay,
// so the budget is fixed and this system lives inside it. Running at `kLifecycle` puts this system
// after the `kPostKernel` zone systems, so royale's zone takes the id it needs first and the
// spawner takes whatever is left -- which is the correct precedence anyway, since a match without
// its zone is not a match and a match without a comet is merely calmer.
//
// **What happens when there is no id left.** The system reads
// `GameWorld::entity_id_reservation()` before each draw and stops when it is empty, so exhaustion
// is never reached and `create_entity`'s hard failure never fires. In practice one id is available
// per tick, so at most one hazard is seated per tick. Two kinds due on the same tick therefore
// resolve by declaration order: the earlier-declared one is seated and the later one is **skipped
// for that tick**, not queued. Skipped rather than deferred is deliberate and is the honest word: a
// true deferral needs per-kind state carried between ticks, a system may hold nothing but immutable
// configuration (`simulation/simulation_system.hpp`), and the only durable homes are a component --
// which would need an entity that does not exist yet -- or mode state, which is one mode's and this
// mechanic is no mode's. Nothing drifts as a result, because due-ness is a pure function of the
// tick: a kind that misses one appearance is back on schedule at its next multiple. With intervals
// of hundreds of ticks and a handful of kinds, a collision is rare and costs one appearance.
//
// **What happens at the entity bound.** `kMaximumEntityCount` is 4,096 seats and a component store
// throws when a write would exceed it. The system checks the `PhysicsBody` store's occupancy before
// seating and stops when the world is full, so a full arena quietly stops producing hazards instead
// of failing the tick. It cannot fill the world on its own: every hazard carries a `Lifetime` sized
// to its own crossing, and `LifetimeExpirySystem` despawns it when that runs out, so the standing
// population is bounded by `crossing_ticks / spawn_interval_ticks` per kind and is a small constant
// for any interval a designer would author. The seat check is the backstop for the case that bound
// does not cover -- a very fast spawn interval against a very slow hazard -- and it fails soft
// because a missing comet is a lesser harm than a stopped match.
//
// It lives in `shared/` and royale declares it; sandbox does not, which is the test that the
// mechanic is optional rather than ambient.
// related: hazard_archetype.hpp -- the validated configuration this reads.
// related: lifetime_expiry_system.hpp -- what removes the bodies this creates.
// related: lethal_hazard_contact_rule.hpp -- the row that reads the marker this attaches.
class HazardSpawnSystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "hazard_spawn";

  // The four arena edges a hazard may enter through, in the order the entry draw indexes them.
  // Closed and ordered, because the draw is `next_below(kEntryEdgeCount)` and the mapping from a
  // drawn integer to a geometry must be one written thing rather than an arithmetic accident.
  static constexpr std::uint64_t kEntryEdgeCount = 4;

  // How far outside the arena a hazard's centre starts, as a multiple of its own radius. Two
  // radii puts the whole disc clear of the edge it enters through, so the body is unambiguously
  // outside on the tick it is seated and enters under its own velocity rather than starting in
  // contact with a player standing at the wall.
  static constexpr double kEntryClearanceRadii = 2.0;

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(std::vector<HazardArchetype> archetypes);

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

  explicit HazardSpawnSystem(std::vector<HazardArchetype> archetypes) noexcept
      : archetypes_(std::move(archetypes)) {}

private:
  std::vector<HazardArchetype> archetypes_;
};

} // namespace blob_royale::gameplay

#endif
