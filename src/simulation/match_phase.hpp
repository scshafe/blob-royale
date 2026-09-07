#ifndef BLOB_ROYALE_SIMULATION_MATCH_PHASE_HPP
#define BLOB_ROYALE_SIMULATION_MATCH_PHASE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace blob_royale::simulation {

// canonical: match_phase -- the four states of the engine's generic match machine.
//
// The machine is engine-owned and identical under every mode; a mode supplies three predicates and
// two durations through MatchObjective and declares no transition logic of its own
// (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle").
// related: match_lifecycle_system.hpp -- the one place a transition is decided.
enum class MatchPhase : std::uint8_t {
  kLobby = 0,
  kCountdown = 1,
  kRunning = 2,
  kEnded = 3,
};

// The phases in machine order, so no diagnostic maintains a second list.
inline constexpr std::array<MatchPhase, 4> kMatchPhases{MatchPhase::kLobby, MatchPhase::kCountdown,
                                                        MatchPhase::kRunning, MatchPhase::kEnded};

inline constexpr std::size_t kMatchPhaseCount = kMatchPhases.size();

// The declared name of one phase, for diagnostics, fixtures, and the protocol boundary.
[[nodiscard]] constexpr std::string_view match_phase_name(const MatchPhase phase) noexcept {
  switch (phase) {
  case MatchPhase::kLobby:
    return "lobby";
  case MatchPhase::kCountdown:
    return "countdown";
  case MatchPhase::kRunning:
    return "running";
  case MatchPhase::kEnded:
    return "ended";
  }
  return "match_phase_invalid";
}

} // namespace blob_royale::simulation

#endif
