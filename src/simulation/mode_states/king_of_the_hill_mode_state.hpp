#ifndef BLOB_ROYALE_SIMULATION_MODE_STATES_KING_OF_THE_HILL_MODE_STATE_HPP
#define BLOB_ROYALE_SIMULATION_MODE_STATES_KING_OF_THE_HILL_MODE_STATE_HPP

#include <cstdint>

namespace blob_royale::simulation {

// canonical: king_of_the_hill_mode_state -- the hill's contribution to every published snapshot.
//
// Everything king of the hill owns that is entity-shaped is a component: the hill is a `Hill` on
// its own entity, presence toward the next point is a `HillPresence` per entity, and the score is
// the engine's `Score`. What is left is **three declared constants and nothing observed**: the
// three denominators a client cannot compute from the frame it holds. It can already count the
// elapsed running time from `tick_sequence` and `phase_started_tick`, read every score, and read
// every presence counter; it cannot know how many points win, how many ticks a point takes, or
// when the clock runs out, and a counter published without its bound is what made royale's
// elimination read as arbitrary (`mode_states/royale_placements_mode_state.hpp`).
//
// They are stamped every tick by `hill_rules_publisher`, declared last at `kLifecycle`, so no
// committed tick publishes an unstamped zero. **No hill rule reads them back**: `hill_scoring` and
// `HillObjective` hold their own copies of the validated configuration, and the three cannot
// disagree because `KingOfTheHillMode::systems()` and `objective()` build all of them from one
// `KingOfTheHillConfiguration` (`docs/architecture/0007-king-of-the-hill-and-race-modes.md`
// § "Mode state and the wire").
// related: mode_match_state_registry.hpp -- the closed list this is an arm of.
// related: ../../gameplay/king_of_the_hill/hill_rules_publisher_system.hpp -- the only writer.
struct KingOfTheHillModeState final {
  std::uint64_t points_to_win{};
  // `I`: the consecutive inside ticks that earn one point, the denominator of
  // `HillPresence::inside_ticks`. Zero is legal and means a point on the first inside tick.
  std::uint64_t point_interval_ticks{};
  std::uint64_t time_limit_ticks{};

  friend bool operator==(const KingOfTheHillModeState&, const KingOfTheHillModeState&) = default;
};

} // namespace blob_royale::simulation

#endif
