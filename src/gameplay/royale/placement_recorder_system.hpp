#ifndef BLOB_ROYALE_GAMEPLAY_ROYALE_PLACEMENT_RECORDER_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_ROYALE_PLACEMENT_RECORDER_SYSTEM_HPP

#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: placement_recorder -- ranking and roster removal, at kLifecycle.
// @extension-point simulation_system
//
// The engine appends its own `MatchLifecycleSystem` last at `kLifecycle` and it is not removable,
// so this always runs **before** this tick's phase transition is evaluated
// (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle"). Every
// phase this system reads is therefore the phase the previous tick committed.
//
// Each tick it performs three steps in this order, and **the written order is the contract**
// (`docs/architecture/0005-royale-mode.md` § "Elimination and placement"):
//
//   1. `running` with a `previous_phase` of `countdown` -- the first tick of a match -- clears the
//      placement list.
//   2. Reads this tick's `EliminationEvent`s, destroys each named entity and emits a
//      `DespawnEvent` for it, then computes one placement for the whole set and appends one entry
//      per entity in ascending `EntityId` order.
//   3. Records the committed phase as the block's `previous_phase`, the published mirror of the
//      engine field step 1 reads (`match_state.hpp`).
//
// The reason the order is written down is the one case where two steps coincide. Step 1 *can*
// coincide with step 2 -- under `zone_shrink_seconds = 0` and `elimination_grace_seconds = 0` a
// match's first `running` tick both starts the match and eliminates -- and step 1 running first is
// what guarantees the previous match's ranking is cleared before this match's first placements are
// appended rather than after.
//
// **The restart wipe used to be this system's step 3 and is `shared/match_reset_system` now**,
// declared by royale right after this one. It was the one rule royale attached to a transition, and
// it is every ending mode's rule rather than royale's; the shared system wipes every participant
// where this one wiped every alive entity, and the replay fixtures' pinned counts are what proved
// the two agree on every tick royale has recorded.
//
// Placement is computed once per tick over the whole eliminated set, not once per entity:
//
//     eliminated_count = number of EliminationEvents this tick
//     alive_after      = alive count after step 2 has destroyed them
//     placement        = alive_after + 1     for every entity in the eliminated set
//
// A match that ends with a winner therefore assigns placement `2` to the last eliminated entity;
// the winner receives no entry and holds placement `1` implicitly through the committed
// `MatchOutcome`. A match that ends in a draw gives `alive_after = 0` and assigns the final
// entities the shared placement `1`, which is the correct report for a mutual finish.
//
// Because step 2 destroys before the engine's `MatchLifecycleSystem` reads `outcome`, an
// elimination and the end it causes commit in the same tick, and the placement list a snapshot
// carries always agrees with the alive count that snapshot reports.
//
// related: royale/zone_elimination_system.hpp -- the kPostKernel producer of what this consumes.
// related: ../shared/match_reset_system.hpp -- the restart wipe that follows this system.
// related: mode_states/royale_placements_mode_state.hpp -- the block this is the only writer of.
class PlacementRecorderSystem final : public simulation::SimulationSystem {
public:
  // The stable name the pipeline, diagnostics, and fixtures know this system by.
  static constexpr std::string_view kSystemName = "placement_recorder";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem> create();

  PlacementRecorderSystem() = default;

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  // Throws GameplayValidationError with `GAMEPLAY.ROYALE_PLACEMENT_LIMIT_EXCEEDED` when one match's
  // ranking would pass `kMaximumPlayerCount`, the same bound the roster carries. It holds no
  // configuration at all: every number it needs is committed world state.
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;
};

} // namespace blob_royale::gameplay

#endif
