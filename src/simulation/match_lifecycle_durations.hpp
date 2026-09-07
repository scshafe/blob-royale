#ifndef BLOB_ROYALE_SIMULATION_MATCH_LIFECYCLE_DURATIONS_HPP
#define BLOB_ROYALE_SIMULATION_MATCH_LIFECYCLE_DURATIONS_HPP

#include <cstdint>

namespace blob_royale::simulation {

// canonical: match_lifecycle_durations -- the two engine-timed phase lengths one mode declares.
//
// These are the only two durations the generic machine needs, which is why they are the third
// member of MatchObjective rather than a seventh declaration on GameMode: nothing else reads them
// (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle").
//
// Both are **integer tick counts**, converted once at configuration load. `blob_simulation` names
// no clock type, and a duration expressed in seconds anywhere below the loader would reintroduce
// one (`docs/architecture/0004-gameplay-architecture.md`
// § "Determinism obligations for framework code").
//
// Zero is legal and means the phase occupies exactly one tick, because the machine commits at most
// one transition per tick; that bound is what keeps an all-zero configuration total instead of
// chaining every transition inside one tick
// (`docs/architecture/0005-royale-mode.md` § "Match lifecycle").
// related: match_objective.hpp -- the declaration that returns these.
struct MatchLifecycleDurations final {
  std::uint64_t countdown_ticks{};
  std::uint64_t restart_delay_ticks{};

  friend bool operator==(const MatchLifecycleDurations&, const MatchLifecycleDurations&) = default;
};

} // namespace blob_royale::simulation

#endif
