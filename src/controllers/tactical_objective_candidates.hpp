#ifndef BLOB_ROYALE_CONTROLLERS_TACTICAL_OBJECTIVE_CANDIDATES_HPP
#define BLOB_ROYALE_CONTROLLERS_TACTICAL_OBJECTIVE_CANDIDATES_HPP

#include "observation.hpp"
#include "physics_body.hpp"

#include <compare>
#include <cstdint>
#include <vector>

namespace blob_royale::controllers {

// Explicit stable ordinals break equal-distance ties, never variant or registration order.
enum class TacticalObjectiveKind : std::uint8_t {
  kHill = 0,
  kZone = 1,
  kRaceGate = 2,
  kRaceRecovery = 3
};
struct TacticalObjectiveKey final {
  TacticalObjectiveKind kind;
  // Hill/zone entity, or the race's next gate index. Recovery changes kind but keeps that gate.
  std::uint64_t subject;
  friend auto operator<=>(const TacticalObjectiveKey&, const TacticalObjectiveKey&) = default;
};
struct TacticalObjectiveCandidate final {
  TacticalObjectiveKey key;
  simulation::Vector2 target;
  double arrival_radius;
  double squared_distance;
  friend bool operator==(const TacticalObjectiveCandidate&,
                         const TacticalObjectiveCandidate&) = default;
};
enum class TacticalObjectiveDisposition { kReady, kWaiting, kFinished };
struct TacticalObjectiveCandidates final {
  TacticalObjectiveDisposition disposition{TacticalObjectiveDisposition::kReady};
  std::vector<TacticalObjectiveCandidate> candidates{};
};

// canonical: tactical_objective_candidates -- closed public-schema providers and terrain screening.
// The body is the caller's resolved owned dynamic body. At most 32 raw candidates, checked before
// terrain work; malformed objectives and unsupported schemas throw CONTROLLERS.*. Canonical
// terrain numerical failures propagate, never become a partial or silently repaired result.
[[nodiscard]] TacticalObjectiveCandidates
collect_tactical_objective_candidates(const Observation& observation,
                                      const simulation::PhysicsBody& body);
[[nodiscard]] bool tactical_candidate_precedes(const TacticalObjectiveCandidate& left,
                                               const TacticalObjectiveCandidate& right) noexcept;

} // namespace blob_royale::controllers

#endif
