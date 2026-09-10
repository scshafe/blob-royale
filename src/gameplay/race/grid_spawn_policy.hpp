#ifndef BLOB_ROYALE_GAMEPLAY_RACE_GRID_SPAWN_POLICY_HPP
#define BLOB_ROYALE_GAMEPLAY_RACE_GRID_SPAWN_POLICY_HPP

#include "components/race_progress_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "shared/next_free_spawn_point_policy.hpp"
#include "spawn_policy.hpp"
#include "tick_context.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace blob_royale::gameplay {

// canonical: grid_spawn_policy -- the race's closed field and its pre-gate return route.
//
// Between matches the shared next-free-point policy owns rotation, occupied-point deferral,
// timer deferral and the post-ended wipe tick. During running, only a racer with progress zero
// may return through the grid; a newcomer has no progress and waits for the next lobby. The
// checkpoint system owns returns after a gate. Ended admits nobody through the grid.
// related: checkpoint_respawn_system.hpp -- the other route, aligned to N + D + 1.
class GridSpawnPolicy final : public simulation::SpawnPolicy {
public:
  GridSpawnPolicy() = default;

  // Pure choice of the next free index, or nullopt to offer the same entity next tick.
  [[nodiscard]] std::optional<std::size_t>
  choose_spawn_point(const simulation::GameWorld& world, const simulation::TickContext& context,
                     const simulation::EntityId entity, const std::size_t rotation_counter,
                     const std::span<const bool> spawn_point_is_free) const override {
    if (world.match().phase == simulation::MatchPhase::kEnded) {
      return std::nullopt;
    }
    if (world.match().phase == simulation::MatchPhase::kRunning) {
      const simulation::RaceProgress* progress =
          world.store<simulation::RaceProgress>().find(entity);
      if (progress == nullptr || progress->next_checkpoint != 0) {
        return std::nullopt;
      }
    }
    return next_free_spawn_point_policy_.choose_spawn_point(world, context, entity,
                                                            rotation_counter, spawn_point_is_free);
  }

private:
  NextFreeSpawnPointPolicy next_free_spawn_point_policy_;
};

} // namespace blob_royale::gameplay

#endif
