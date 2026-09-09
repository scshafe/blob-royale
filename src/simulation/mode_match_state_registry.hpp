#ifndef BLOB_ROYALE_SIMULATION_MODE_MATCH_STATE_REGISTRY_HPP
#define BLOB_ROYALE_SIMULATION_MODE_MATCH_STATE_REGISTRY_HPP

#include "mode_states/no_mode_state.hpp"
#include "mode_states/royale_placements_mode_state.hpp"

#include <string_view>
#include <type_traits>
#include <variant>

namespace blob_royale::simulation {

// canonical: mode_match_state_registry -- the closed list of mode-owned match-state blocks.
// @extension-point game_mode
//
// `MatchState` carries the engine's own fields plus exactly one of these, identified on the wire
// by its schema id (`docs/architecture/0004-gameplay-architecture.md` § "Snapshots and protocol
// shape"). This is **the seam a mode's non-entity state goes through**, and it is the reason
// adding a mode does not edit the kernel: a mode's system assigns its own arm the first tick it
// observes another, exactly as royale's `zone_shrink` creates the zone entity the first tick it
// observes none (`docs/architecture/0005-royale-mode.md` § "Where zone and elimination state
// live").
//
// **Mode state should be a component wherever it can be.** A component is snapshot-visible,
// erased by `destroy_entity`, and part of world equality for free, so only genuinely
// non-entity-shaped state belongs here -- royale's ordered placement list and the
// `previous_phase` it publishes as a mirror of the engine's, capture the flag's per-team scores.
//
// Adding a mode-state block:
//
//   new  src/simulation/mode_states/<schema>_mode_state.hpp  the value struct and its schema id
//   edit src/simulation/mode_match_state_registry.hpp        one type in the variant, one
//                                                            ModeMatchStateSchemaId specialization
//   new  src/gameplay/<mode>/...                             the system that writes it
//
// Two implementations of this seam: `NoModeState` for every mode whose state is entity-shaped, and
// royale's `royale_placements` block, which plan Step 21 added as one type below and one
// `ModeMatchStateSchemaId` specialization with no other kernel file edited.
// related: match_state.hpp -- the world state that holds one of these.
// related: component_registry.hpp -- the same closed-list shape for entity state.
using ModeMatchState = std::variant<NoModeState, RoyalePlacementsModeState>;

// A variant is nothrow-move-constructible exactly when every alternative is, so asking the variant
// asks about every alternative and cannot fall behind the list the way a hand-typed conjunction
// does. It is `MatchState`'s member, and `MatchState` is copied into every working world at the
// start of every tick.
static_assert(std::is_nothrow_move_constructible_v<ModeMatchState>,
              "every ModeMatchState alternative must be nothrow-move-constructible");

// canonical: mode_match_state_schema_id -- the one wire schema id of one mode-state block.
//
// Declared and never defined, so an arm that forgot its schema id fails to compile at the use site
// instead of publishing an empty one.
template <typename ModeStateType> struct ModeMatchStateSchemaId;

template <> struct ModeMatchStateSchemaId<NoModeState> {
  static constexpr std::string_view value = "none";
};

template <> struct ModeMatchStateSchemaId<RoyalePlacementsModeState> {
  static constexpr std::string_view value = "royale_placements";
};

template <typename ModeStateType>
inline constexpr std::string_view mode_match_state_schema_id =
    ModeMatchStateSchemaId<ModeStateType>::value;

// The schema id of one held block. Total over the closed variant and generated from the
// specializations, so a new arm cannot silently answer with an existing id.
[[nodiscard]] constexpr std::string_view
mode_match_state_schema_id_of(const ModeMatchState& mode_state) noexcept {
  return std::visit(
      []<typename ModeStateType>(const ModeStateType&) {
        return ModeMatchStateSchemaId<ModeStateType>::value;
      },
      mode_state);
}

} // namespace blob_royale::simulation

#endif
