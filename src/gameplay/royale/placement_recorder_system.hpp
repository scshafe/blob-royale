#ifndef BLOB_ROYALE_GAMEPLAY_ROYALE_PLACEMENT_RECORDER_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_ROYALE_PLACEMENT_RECORDER_SYSTEM_HPP

#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: placement_recorder -- royale's only kLifecycle system: ranking, removal, and the
// restart wipe.
// @extension-point simulation_system
//
// The engine appends its own `MatchLifecycleSystem` last at `kLifecycle` and it is not removable,
// so this always runs **before** this tick's phase transition is evaluated
// (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle"). Every
// phase this system reads is therefore the phase the previous tick committed.
//
// Each tick it performs four steps in this order, and **the written order is the contract**
// (`docs/architecture/0005-royale-mode.md` § "Elimination and placement"):
//
//   1. `running` with a `previous_phase` of `countdown` -- the first tick of a match -- clears the
//      placement list.
//   2. Reads this tick's `EliminationEvent`s, destroys each named entity and emits a
//      `DespawnEvent` for it, then computes one placement for the whole set and appends one entry
//      per entity in ascending `EntityId` order.
//   3. `lobby` with a `previous_phase` of `ended` destroys every alive entity: the restart wipe.
//   4. Records the committed phase as the block's `previous_phase`, the published mirror of the
//      engine field steps 1 and 3 read (`match_state.hpp`).
//
// The reason the order is written down is the one case where two steps coincide. Steps 1 and 3 are
// mutually exclusive because one tick's phase cannot be both `running` and `lobby`, and step 3
// never coincides with step 2 because eliminations occur only during `running`. Step 1 *can*
// coincide with step 2 -- under `zone_shrink_seconds = 0` and `elimination_grace_seconds = 0` a
// match's first `running` tick both starts the match and eliminates -- and step 1 running first is
// what guarantees the previous match's ranking is cleared before this match's first placements are
// appended rather than after.
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
// **The restart wipe is the one rule royale attaches to a transition.** Without it the winner would
// carry its position, velocity, and stored acceleration into the next match and would never be
// re-seated on the ring. `RotatingRingSpawnPolicy` defers seating on that same `lobby` tick, from
// the same `previous_phase`, so the wipe cannot delete an entity the tick just seated.
// related: royale/zone_elimination_system.hpp -- the kPostKernel producer of what this consumes.
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
