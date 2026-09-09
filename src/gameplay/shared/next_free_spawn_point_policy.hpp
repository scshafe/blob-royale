#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_NEXT_FREE_SPAWN_POINT_POLICY_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_NEXT_FREE_SPAWN_POINT_POLICY_HPP

#include "entity_id.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "shared/spawn_point_probe.hpp"
#include "spawn_policy.hpp"
#include "tick_context.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace blob_royale::gameplay {

// canonical: next_free_spawn_point_policy -- seat at the next free point, in every phase but one.
//
// The open-field rule: whoever asks is seated whenever they ask, wherever there is room, by the
// shared forward probe from the world-owned rotation counter (`shared/spawn_point_probe.hpp`), so
// consecutive joiners spread around the map instead of queueing at point zero. Sandbox declares it
// because free play seats joiners mid-match, and king of the hill declares it because a hill match
// loses nothing by someone arriving late
// (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle";
// `docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "King of the hill").
//
// **The one phase it defers in is the single `lobby` tick after `ended`.** A mode whose match
// ends wipes its field at `kLifecycle` of that tick, and a body seated at phase 0 of the same tick
// would be destroyed before it had moved; deferring there is what lets the wipe and the seating
// never meet, exactly as royale's ring policy defers on the same tick. It reads the engine's
// `previous_phase` to know the tick (`match_state.hpp`). Sandbox never reaches `ended`, so for free
// play the rule is what it always was: no phase is read that could ever say no.
//
// It is **pure**, as the seam requires: the engine's SpawnSystem owns which entities are offered,
// the occupancy test that produced `spawn_point_is_free`, the counter and its advance, and the
// seating write. This computes an index and nothing else. A map with every point taken yields
// nullopt, which defers the entity one tick; a map with no points cannot reach here, because every
// declaring mode's `validate_map` rejects it at construction.
//
// It moved here from `sandbox/` the day a second mode declared it, which is the rule of
// `README.md`. `docs/architecture/0004-gameplay-architecture.md` sketches it as
// `AnyFreeSpawnPoint`; the name says what it does: the *next* free point in rotation order, not an
// arbitrary one. related: ../sandbox/sandbox_mode.hpp -- the first declaration that returns this.
// related: spawn_point_probe.hpp -- the probe this wraps one phase rule around.
class NextFreeSpawnPointPolicy final : public simulation::SpawnPolicy {
public:
  NextFreeSpawnPointPolicy() = default;

  [[nodiscard]] std::optional<std::size_t>
  choose_spawn_point(const simulation::GameWorld& world, const simulation::TickContext&,
                     simulation::EntityId, const std::size_t rotation_counter,
                     const std::span<const bool> spawn_point_is_free) const override {
    const simulation::MatchState& match = world.match();
    if (match.phase == simulation::MatchPhase::kLobby &&
        match.previous_phase == simulation::MatchPhase::kEnded) {
      return std::nullopt;
    }
    return next_free_spawn_point(rotation_counter, spawn_point_is_free);
  }
};

} // namespace blob_royale::gameplay

#endif
