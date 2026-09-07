#ifndef BLOB_ROYALE_GAMEPLAY_SANDBOX_NEXT_FREE_SPAWN_POINT_POLICY_HPP
#define BLOB_ROYALE_GAMEPLAY_SANDBOX_NEXT_FREE_SPAWN_POINT_POLICY_HPP

#include "entity_id.hpp"
#include "game_world.hpp"
#include "spawn_policy.hpp"
#include "tick_context.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace blob_royale::gameplay {

// canonical: next_free_spawn_point_policy -- seat at the next free point, in every phase.
//
// Sandbox seats whoever asks, whenever they ask, wherever there is room: it probes forward from the
// world-owned rotation counter the SpawnSystem offers and takes the first free point, so
// consecutive joiners spread around the map instead of queueing at point zero. It reads no phase,
// which is exactly how "sandbox seats joiners mid-match" is expressed
// (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle").
//
// It is **pure**, as the seam requires: the engine's SpawnSystem owns which entities are offered,
// the occupancy test that produced `spawn_point_is_free`, the counter and its advance, and the
// seating write. This computes an index and nothing else.
//
// A map with every point taken yields nullopt, which defers the entity one tick; it is offered
// again in the same ascending order on a later tick. A map with no points at all cannot reach here,
// because `SandboxMode::validate_map` rejects it at construction.
//
// `docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle" sketches
// this policy as `AnyFreeSpawnPoint`. The name here says what it actually does: it takes the *next*
// free point in rotation order rather than an arbitrary one, which is what spreads joiners.
// related: sandbox_mode.hpp -- the one declaration that returns this.
// related: spawn_system.hpp -- the engine mechanism that owns everything but this choice.
class NextFreeSpawnPointPolicy final : public simulation::SpawnPolicy {
public:
  NextFreeSpawnPointPolicy() = default;

  [[nodiscard]] std::optional<std::size_t>
  choose_spawn_point(const simulation::GameWorld&, const simulation::TickContext&,
                     simulation::EntityId, const std::size_t rotation_counter,
                     const std::span<const bool> spawn_point_is_free) const override {
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
