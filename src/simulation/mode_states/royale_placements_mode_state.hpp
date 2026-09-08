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
// own entity and the grace counter is a `ZoneExposure` per entity. What is left is the three values
// below, and none of them belongs to an entity (`docs/architecture/0005-royale-mode.md` § "Where
// zone and elimination state live").
//
// **Two of the three are observations and the third is a declaration**, and that distinction is the
// only interesting thing about this struct. `placements` and `previous_phase` are things royale
// *found out* by running; `elimination_grace_ticks` is a number an operator wrote in a
// configuration file. The reason a declared constant is allowed to sit beside two observations is
// that the block is not a scratchpad, it is **royale's contribution to every published snapshot**,
// and a reader of that snapshot cannot interpret `ZoneExposure::outside_ticks` without the bound it
// is counted against. Publishing the counter and withholding its denominator is what made the
// elimination rule read as arbitrary to the player who reported it.
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
  // `G`: the consecutive outside ticks `zone_elimination` allows before it names an entity, and the
  // denominator of the `ZoneExposure::outside_ticks` every snapshot already carries. Ticks rather
  // than seconds because `RoyaleConfiguration` stores ticks and nothing else on the wire is
  // expressed in seconds, so a conversion here would be a second unit for a reader to get wrong.
  //
  // **No royale rule reads this member.** `zone_elimination` reads its own copy of the validated
  // configuration and always will; this is the same number travelling to the client, and the two
  // cannot disagree because `RoyaleMode::systems()` builds both from one `RoyaleConfiguration`.
  // Naming that asymmetry is the point: it is a *published* value, not a piece of the simulation,
  // and a future rule that wants the grace must take the configuration rather than read this back.
  //
  // Zero is a legal configuration and means "eliminate on the first outside tick", because the
  // increment precedes the test. It is also the default this struct is born with, which is
  // deliberately *not* treated as a sentinel: `elimination_grace_publisher` runs last among
  // royale's `kLifecycle` systems, so every committed tick of a royale match carries the configured
  // value and no snapshot can publish an unstamped zero. A reader that wants to distinguish "grace
  // of zero" from "no royale block at all" reads the mode-state schema id, which already answers
  // it.
  std::uint64_t elimination_grace_ticks{};

  friend bool operator==(const RoyalePlacementsModeState&,
                         const RoyalePlacementsModeState&) = default;
};

} // namespace blob_royale::simulation

#endif
