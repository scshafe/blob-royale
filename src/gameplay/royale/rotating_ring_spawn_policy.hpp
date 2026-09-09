#ifndef BLOB_ROYALE_GAMEPLAY_ROYALE_ROTATING_RING_SPAWN_POLICY_HPP
#define BLOB_ROYALE_GAMEPLAY_ROYALE_ROTATING_RING_SPAWN_POLICY_HPP

#include "entity_id.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "spawn_policy.hpp"
#include "tick_context.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace blob_royale::gameplay {

// canonical: rotating_ring_spawn_policy -- royale's seating rule: the next free point, and only
// between matches.
// @extension-point game_mode
//
// Spawn positions are map data. This chooses an index into `map.spawn_points()`, or returns nullopt
// to defer the entity one tick. It computes no geometry and holds no state
// (`docs/architecture/0005-royale-mode.md` § "Spawning"):
//
//     if the committed phase is running or ended: return nullopt
//     if the committed phase is lobby and previous_phase is ended: return nullopt
//     n = spawn_point_is_free.size()
//     for probe in [0, n):
//       k = (rotation_counter + probe) mod n
//       if spawn_point_is_free[k]: return k
//     return nullopt
//
// **Deferring during `running` and `ended` is the mode's rule, and it is what makes a match a
// closed field.** A joiner who arrives mid-match waits for the next `lobby` rather than appearing
// inside a shrinking circle with no chance of placing. The second deferral covers the single
// `lobby` tick on which `placement_recorder` clears the arena; this policy and the recorder both
// read the engine's `previous_phase`, which the lifecycle system recorded at the end of the
// previous tick and nothing rewrites before the end of this one, so the two rules cannot disagree
// about which tick that is (`match_state.hpp`).
//
// The rotation is the whole of the rest: consecutive joiners are spread around the map's points
// instead of stacking on the first free one, and the counter is world state, so seating is a
// deterministic function of the committed world.
//
// The engine's `SpawnSystem` owns everything else -- ascending-`EntityId` iteration over pending
// entities, the occupancy test that produced `spawn_point_is_free`, the counter and its advance,
// and the seating write that gives the entity a `PhysicsBody` at rest. A full ring defers the
// entity one tick; no point can free while positions are frozen, so the deferred entity loses
// nothing by waiting, and the per-tick probe cost is bounded by one pass over the points per
// pending entity.
//
// A map with more than 32 points, or fewer, is valid; the count is data. A layout whose adjacent
// chord falls below `2r` is also valid: the occupancy test simply seats fewer entities per tick and
// the rest defer.
// related: spawn_system.hpp -- the engine mechanism that owns everything but this choice.
// related: royale/placement_recorder_system.hpp -- the other reader of `previous_phase`.
class RotatingRingSpawnPolicy final : public simulation::SpawnPolicy {
public:
  RotatingRingSpawnPolicy() = default;

  [[nodiscard]] std::optional<std::size_t>
  choose_spawn_point(const simulation::GameWorld& world, const simulation::TickContext&,
                     simulation::EntityId, const std::size_t rotation_counter,
                     const std::span<const bool> spawn_point_is_free) const override {
    const simulation::MatchPhase phase = world.match().phase;
    if (phase == simulation::MatchPhase::kRunning || phase == simulation::MatchPhase::kEnded) {
      return std::nullopt;
    }
    if (phase == simulation::MatchPhase::kLobby &&
        world.match().previous_phase == simulation::MatchPhase::kEnded) {
      return std::nullopt;
    }
    const std::size_t point_count = spawn_point_is_free.size();
    for (std::size_t probe = 0; probe < point_count; ++probe) {
      const std::size_t index = (rotation_counter + probe) % point_count;
      if (spawn_point_is_free[index]) {
        return index;
      }
    }
    return std::nullopt;
  }
};

} // namespace blob_royale::gameplay

#endif
