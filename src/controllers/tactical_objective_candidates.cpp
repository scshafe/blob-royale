#include "tactical_objective_candidates.hpp"

#include "components/hill_component.hpp"
#include "components/race_progress_component.hpp"
#include "components/zone_component.hpp"
#include "controller_observation_queries.hpp"
#include "controller_steering.hpp"
#include "controllers_limits.hpp"
#include "controllers_validation_error.hpp"
#include "mode_match_state_registry.hpp"
#include "terrain_queries.hpp"

#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <variant>

namespace blob_royale::controllers {
namespace {

void require_candidate_count(const std::size_t count) {
  if (count > kMaximumTacticalObjectiveCandidateCount) {
    throw ControllersValidationError(ControllersValidationCode::kTacticalCandidateLimitExceeded,
                                     "tactical_controller.objectives",
                                     "raw objective count exceeds 32");
  }
}

void require_radius(const double radius, const bool positive) {
  if (!std::isfinite(radius) || radius < 0.0 || (positive && radius == 0.0) ||
      radius > simulation::kMaximumPhysicalComponentMagnitude) {
    throw ControllersValidationError(ControllersValidationCode::kTacticalObjectiveInvalid,
                                     "tactical_controller.objective.radius",
                                     "published objective radius is outside its finite domain");
  }
}

[[nodiscard]] TacticalObjectiveCandidate candidate(const TacticalObjectiveKey key,
                                                   const simulation::Vector2& target,
                                                   const double radius,
                                                   const simulation::PhysicsBody& body) {
  return {key, target, radius,
          controller_squared_magnitude(controller_target_offset(body.position(), target))};
}

template <typename Circle, TacticalObjectiveKind Kind>
[[nodiscard]] TacticalObjectiveCandidates circles(const Observation& observation,
                                                  const simulation::PhysicsBody& body) {
  const auto entries = observation.snapshot().components<Circle>();
  require_candidate_count(entries.size());
  TacticalObjectiveCandidates result;
  result.candidates.reserve(entries.size());
  for (const auto& entry : entries) {
    require_radius(entry.value.radius, false);
    result.candidates.push_back(
        candidate({Kind, entry.entity.value()}, entry.value.center, entry.value.radius, body));
  }
  return result;
}

[[nodiscard]] TacticalObjectiveCandidates race(const Observation& observation,
                                               const simulation::PhysicsBody& body) {
  if (!observation.entity()) {
    return {TacticalObjectiveDisposition::kWaiting, {}};
  }
  const auto* progress = find_observed_component<simulation::RaceProgress>(observation.snapshot(),
                                                                           *observation.entity());
  if (progress == nullptr) {
    return {TacticalObjectiveDisposition::kWaiting, {}};
  }
  const auto& course =
      std::get<simulation::RaceModeState>(observation.snapshot().match().mode_state());
  const auto* road = observation.terrain().find_corridor(course.road.value());
  if (road == nullptr || course.checkpoints.empty() ||
      progress->next_checkpoint > course.checkpoints.size()) {
    throw ControllersValidationError(ControllersValidationCode::kTacticalObjectiveInvalid,
                                     "tactical_controller.objective.race",
                                     "published road binding, gates, or progress is invalid");
  }
  require_radius(course.checkpoint_radius, true);
  if (course.checkpoint_radius > road->half_width()) {
    throw ControllersValidationError(ControllersValidationCode::kTacticalObjectiveInvalid,
                                     "tactical_controller.objective.race.checkpoint_radius",
                                     "gate radius exceeds the selected road half width");
  }
  if (progress->next_checkpoint == course.checkpoints.size()) {
    return {TacticalObjectiveDisposition::kFinished, {}};
  }
  const auto nearest = simulation::corridor_project_to_centreline(*road, body.position());
  if (nearest.distance > kDefaultRacerCautionFraction * road->half_width()) {
    return {TacticalObjectiveDisposition::kReady,
            {candidate({TacticalObjectiveKind::kRaceRecovery, progress->next_checkpoint},
                       nearest.point, 0.0, body)}};
  }
  return {TacticalObjectiveDisposition::kReady,
          {candidate({TacticalObjectiveKind::kRaceGate, progress->next_checkpoint},
                     course.checkpoints[static_cast<std::size_t>(progress->next_checkpoint)],
                     course.checkpoint_radius, body)}};
}

using Provider = TacticalObjectiveCandidates (*)(const Observation&,
                                                 const simulation::PhysicsBody&);
struct Registration final {
  std::string_view schema_id;
  Provider provider;
};
// @extension-point tactical_objective_provider -- one explicit row per supported public schema.
// Providers know public data only; there is no policy/factory callback into gameplay or the world.
constexpr std::array<Registration, 3> kProviders{
    {{simulation::mode_match_state_schema_id<simulation::KingOfTheHillModeState>,
      &circles<simulation::Hill, TacticalObjectiveKind::kHill>},
     {simulation::mode_match_state_schema_id<simulation::RoyalePlacementsModeState>,
      &circles<simulation::Zone, TacticalObjectiveKind::kZone>},
     {simulation::mode_match_state_schema_id<simulation::RaceModeState>, &race}}};

} // namespace

TacticalObjectiveCandidates
collect_tactical_objective_candidates(const Observation& observation,
                                      const simulation::PhysicsBody& body) {
  const auto schema =
      simulation::mode_match_state_schema_id_of(observation.snapshot().match().mode_state());
  Provider provider = nullptr;
  for (const auto& registration : kProviders) {
    if (registration.schema_id == schema) {
      provider = registration.provider;
      break;
    }
  }
  if (provider == nullptr) {
    throw ControllersValidationError(ControllersValidationCode::kTacticalModeUnsupported,
                                     "tactical_controller.objective.schema_id",
                                     "unsupported running schema " + std::string{schema});
  }
  auto raw = provider(observation, body);
  require_candidate_count(raw.candidates.size());
  TacticalObjectiveCandidates result{raw.disposition, {}};
  result.candidates.reserve(raw.candidates.size());
  for (const auto& value : raw.candidates) {
    if (!simulation::terrain_supports_point(observation.terrain(), value.target)) {
      continue;
    }
    const auto delta = controller_target_offset(body.position(), value.target);
    const auto displacement = simulation::Vector2::create(delta.x, delta.y);
    if (!simulation::first_support_exit(observation.terrain(), body.position(), displacement)) {
      result.candidates.push_back(value);
    }
  }
  return result;
}

bool tactical_candidate_precedes(const TacticalObjectiveCandidate& left,
                                 const TacticalObjectiveCandidate& right) noexcept {
  if (left.squared_distance != right.squared_distance) {
    return left.squared_distance < right.squared_distance;
  }
  return left.key < right.key;
}

} // namespace blob_royale::controllers
