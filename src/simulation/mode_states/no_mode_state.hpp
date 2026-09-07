#ifndef BLOB_ROYALE_SIMULATION_MODE_STATES_NO_MODE_STATE_HPP
#define BLOB_ROYALE_SIMULATION_MODE_STATES_NO_MODE_STATE_HPP

#include <string_view>

namespace blob_royale::simulation {

// canonical: no_mode_state -- the mode-state block of a mode that declares none.
//
// Every mode whose non-entity state is empty carries this, which is every mode whose state is
// entity-shaped: the royale zone is a `Zone` component, capture the flag's flags are `Flag`
// components, and mode state should be components wherever it can be
// (`docs/architecture/0004-gameplay-architecture.md` § "Snapshots and protocol shape").
//
// It exists so the mode-state seam has a defined value from day one rather than an empty variant:
// `MatchState` always holds one alternative, a snapshot always publishes one schema id, and Step
// 21 adds `royale_placements` as one more arm plus one registration line without touching the
// kernel.
// related: mode_match_state_registry.hpp -- the closed list this is the first arm of.
struct NoModeState final {
  friend bool operator==(const NoModeState&, const NoModeState&) = default;
};

} // namespace blob_royale::simulation

#endif
