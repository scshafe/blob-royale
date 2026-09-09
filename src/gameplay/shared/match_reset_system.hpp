#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_MATCH_RESET_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_MATCH_RESET_SYSTEM_HPP

#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: match_reset -- the restart wipe, for every mode whose match ends.
// @extension-point simulation_system
//
// On the single `lobby` tick whose `previous_phase` is `ended`, it destroys every participant --
// every entity carrying a `Controllable`, whether alive, out of play with a respawn timer running,
// or still awaiting its first seat -- and nothing else. Static bodies, a zone, a hill, and every
// other entity that is not somebody's survive it, and so does the seat roster: seats are engine
// state and a person keeps theirs across matches.
//
// It is royale's restart wipe (`placement_recorder` step 3) generalized from "every alive entity"
// to "every participant", because a mode whose fallen come back carries state on entities that
// have no body: a respawning entity with a score, a racer with its gate count. Left standing, they
// would carry it into the next match. Sessions already re-request a spawn when they own no entity
// (`src/server/session_websocket_session.cpp`, `request_spawn_if_absent`), so what a person sees is
// what the royale winner already sees: the blob vanishes on the lobby tick and reappears on a seat
// (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "Shared rules promoted, because
// they now have a second customer").
//
// It reads the engine's `previous_phase` (`match_state.hpp`), which is what distinguishes
// `ended -> lobby` from `countdown -> lobby`; a field that broke up during the countdown is not
// wiped. It runs at `kLifecycle`, before the engine's transition for this tick, so a mode's spawn
// policy that defers on the same tick (`next_free_spawn_point_policy.hpp`, royale's ring) and this
// wipe read the same field and never meet.
// related: ../royale/placement_recorder_system.hpp -- royale's own wipe, which this generalizes.
// related: roster.hpp -- the definition of "participant" this destroys.
class MatchResetSystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "match_reset";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem> create();

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

  MatchResetSystem() = default;
};

} // namespace blob_royale::gameplay

#endif
