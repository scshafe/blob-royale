#ifndef BLOB_ROYALE_SIMULATION_MODE_STATES_ROYALE_PLACEMENTS_MODE_STATE_HPP
#define BLOB_ROYALE_SIMULATION_MODE_STATES_ROYALE_PLACEMENTS_MODE_STATE_HPP

#include "controller_id.hpp"
#include "entity_id.hpp"
#include "match_phase.hpp"
#include "tick_sequence.hpp"

#include <cstdint>
#include <vector>

namespace blob_royale::simulation {

// canonical: royale_placements_mode_state -- royale's match-wide state that is not entity-shaped.
//
// Everything royale owns that *is* entity-shaped is a component: the safe zone is a `Zone` on its
// own entity and the grace counter is a `ZoneExposure` per entity. What is left is the two values
// below, and neither belongs to an entity (`docs/architecture/0005-royale-mode.md` § "Where zone
// and elimination state live").
//
// This is the second arm of the `ModeMatchState` seam, and adding it edited no kernel file: one
// type in the variant and one `ModeMatchStateSchemaId` specialization, both in
// `mode_match_state_registry.hpp` (`docs/architecture/0004-gameplay-architecture.md` § "Game modes
// and the match lifecycle").
// related: mode_match_state_registry.hpp -- the closed list this is an arm of.
// related: match_state.hpp -- the world state that holds one arm.

// One eliminated entity's finishing position. `placement` is one-based and shared by every entity
// eliminated on the same tick, because elimination is evaluated once per tick and a finer rank
// would need time-of-impact resolution inside the tick.
struct RoyalePlacement final {
  EntityId entity;
  // The controller that was driving `entity` when it was eliminated.
  //
  // **It is recorded here because nothing else can recover it.** The tick that records a placement
  // also destroys the entity, so no committed snapshot carries the `Controllable` that held the
  // link, while `match-data.schema.json` requires `controller_id` on every placement entry so a
  // client can recognize its own result after its `entity_id` is gone (`docs/protocol/v2.md`
  // § "snapshot"). The recorder holds the link at the one instant it exists, so it writes it down
  // rather than leaving the encoder to ask a directory that has no answer for a closed session.
  ControllerId controller;
  // `alive_after + 1`, computed once over the whole eliminated set. The winner receives no entry
  // and holds placement 1 implicitly through the committed `MatchOutcome`; a mutual finish gives
  // every final entity the shared placement 1.
  std::uint64_t placement{};
  // The tick sequence being committed when the elimination was recorded.
  TickSequence elimination_tick;

  friend bool operator==(const RoyalePlacement&, const RoyalePlacement&) = default;
};

struct RoyalePlacementsModeState final {
  // Append order is the elimination order: ascending tick, and ascending `EntityId` within one
  // tick. Bounded by `kMaximumPlayerCount`, the same bound the roster already carries. Cleared on
  // the first `running` tick of a match, which is also the first tick that can append to it, so a
  // client has the finished ranking on screen for the whole restart delay.
  std::vector<RoyalePlacement> placements;
  // The lifecycle phase royale observed on the previous tick. It is what lets the restart wipe and
  // the placement clear be expressed as observations of committed state rather than as a hook into
  // the engine's transition, and it is what distinguishes `ended -> lobby` from
  // `countdown -> lobby`.
  MatchPhase previous_phase{MatchPhase::kLobby};

  friend bool operator==(const RoyalePlacementsModeState&,
                         const RoyalePlacementsModeState&) = default;
};

} // namespace blob_royale::simulation

#endif
