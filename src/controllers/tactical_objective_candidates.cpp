#include "tactical_objective_candidates.hpp"

#include "components/hill_component.hpp"
#include "components/hill_motion_component.hpp"
#include "components/race_progress_component.hpp"
#include "components/zone_component.hpp"
#include "controller_observation_queries.hpp"
#include "controller_steering.hpp"
#include "controllers_limits.hpp"
#include "controllers_validation_error.hpp"
#include "mode_match_state_registry.hpp"
#include "simulation_limits.hpp"
#include "terrain_queries.hpp"

#include <array>
#include <cmath>
#include <string>
#include <type_traits>
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

// One circle provider for two kinds. The hill is the only published circle that carries a committed
// velocity, so the intercept is compiled in for it and compiled out for the zone rather than being
// tested per entry: `zone_component.hpp` publishes a centre and a radius and no motion at all, and
// a runtime lookup for a component that cannot exist would read as though one day it might.
template <typename Circle, TacticalObjectiveKind Kind>
[[nodiscard]] TacticalObjectiveCandidates circles(const Observation& observation,
                                                  const simulation::PhysicsBody& body,
                                                  const TacticalObjectivePolicy& policy) {
  const auto& snapshot = observation.snapshot();
  const auto entries = snapshot.components<Circle>();
  require_candidate_count(entries.size());
  TacticalObjectiveCandidates result;
  result.candidates.reserve(entries.size());
  for (const auto& entry : entries) {
    require_radius(entry.value.radius, false);
    auto target = entry.value.center;
    if constexpr (std::is_same_v<Circle, simulation::Hill>) {
      const auto* motion = find_observed_component<simulation::HillMotion>(snapshot, entry.entity);
      if (motion != nullptr && policy.prediction_horizon_ticks > 0) {
        ++result.work.prediction_step_count;
        target = tactical_predicted_center(entry.value.center, motion->velocity,
                                           policy.prediction_horizon_ticks);
      }
    }
    result.candidates.push_back(
        candidate({Kind, entry.entity.value()}, target, entry.value.radius, body));
  }
  return result;
}

// A race gate and a recovery point are authored terrain, not moving state, so this provider has
// nothing to intercept and reads no horizon.
[[nodiscard]] TacticalObjectiveCandidates
race(const Observation& observation, const simulation::PhysicsBody& body,
     [[maybe_unused]] const TacticalObjectivePolicy& policy) {
  if (!observation.entity()) {
    return {TacticalObjectiveDisposition::kWaiting, {}, {}};
  }
  const auto* progress = find_observed_component<simulation::RaceProgress>(observation.snapshot(),
                                                                           *observation.entity());
  if (progress == nullptr) {
    return {TacticalObjectiveDisposition::kWaiting, {}, {}};
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
    return {TacticalObjectiveDisposition::kFinished, {}, {}};
  }
  const auto nearest = simulation::corridor_project_to_centreline(*road, body.position());
  if (nearest.distance > kDefaultRacerCautionFraction * road->half_width()) {
    return {TacticalObjectiveDisposition::kReady,
            {candidate({TacticalObjectiveKind::kRaceRecovery, progress->next_checkpoint},
                       nearest.point, 0.0, body)},
            {}};
  }
  return {TacticalObjectiveDisposition::kReady,
          {candidate({TacticalObjectiveKind::kRaceGate, progress->next_checkpoint},
                     course.checkpoints[static_cast<std::size_t>(progress->next_checkpoint)],
                     course.checkpoint_radius, body)},
          {}};
}

// How far this bot could travel toward anything before the horizon ends: the published room top
// speed times the horizon in seconds, clamped to the arena diagonal because no supported point lies
// beyond it. The clamp is also what keeps the ray a representable `Vector2` for a published top
// speed of ten thousand world units a second at the longest authorable horizon.
[[nodiscard]] double horizon_reach(const Observation& observation,
                                   const TacticalObjectivePolicy& policy,
                                   const double diagonal) noexcept {
  if (policy.prediction_horizon_ticks == 0) {
    return 0.0;
  }
  const double horizon_seconds =
      static_cast<double>(policy.prediction_horizon_ticks) * simulation::kFixedDeltaSeconds;
  const double top_speed = observation.snapshot().match().movement().current.normal_top_speed();
  const double travelled = top_speed * horizon_seconds;
  return travelled < diagonal ? travelled : diagonal;
}

// The escape ray itself: the approach direction, at the horizon's length, asked of the one
// canonical support query. `distance` is the caller's already-computed magnitude of `delta`.
[[nodiscard]] bool horizon_exits_support(const simulation::TerrainDefinition& terrain,
                                         const simulation::Vector2& origin,
                                         const ControllerTargetOffset delta, const double distance,
                                         const double reach) {
  const double unit_x = delta.x / distance;
  const double unit_y = delta.y / distance;
  const auto displacement = simulation::Vector2::create(unit_x * reach, unit_y * reach);
  return simulation::first_support_exit(terrain, origin, displacement).has_value();
}

using Provider = TacticalObjectiveCandidates (*)(const Observation&, const simulation::PhysicsBody&,
                                                 const TacticalObjectivePolicy&);
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
                                      const simulation::PhysicsBody& body,
                                      const TacticalObjectivePolicy& policy) {
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
  auto raw = provider(observation, body, policy);
  require_candidate_count(raw.candidates.size());
  TacticalObjectiveCandidates result{raw.disposition, {}, raw.work};
  result.work.raw_candidate_count = raw.candidates.size();
  result.candidates.reserve(raw.candidates.size());
  // Validated arena bounds are finite and greater than zero, so the diagonal is a positive divisor.
  const auto& bounds = observation.terrain().bounds();
  const double diagonal = controller_magnitude({bounds.width(), bounds.height()});
  const double reach = horizon_reach(observation, policy, diagonal);
  for (const auto& value : raw.candidates) {
    if (!simulation::terrain_supports_point(observation.terrain(), value.target)) {
      continue;
    }
    const auto delta = controller_target_offset(body.position(), value.target);
    const auto displacement = simulation::Vector2::create(delta.x, delta.y);
    if (simulation::first_support_exit(observation.terrain(), body.position(), displacement)) {
      continue;
    }
    auto screened = value;
    const double distance = controller_magnitude(delta);
    const double normalized = distance / diagonal;
    screened.normalized_distance = normalized < 1.0 ? normalized : 1.0;
    // A bot standing on its target has no approach direction to screen, and a zero horizon casts
    // no ray. Neither is a prediction, so neither is counted as one.
    if (reach > 0.0 && distance > 0.0) {
      ++result.work.prediction_step_count;
      screened.escape_blocked =
          horizon_exits_support(observation.terrain(), body.position(), delta, distance, reach);
    }
    result.candidates.push_back(screened);
  }
  result.work.screened_candidate_count = result.candidates.size();
  return result;
}

simulation::Vector2 tactical_predicted_center(const simulation::Vector2& center,
                                              const simulation::Vector2& velocity,
                                              const std::uint64_t horizon_ticks) {
  const double horizon_seconds =
      static_cast<double>(horizon_ticks) * simulation::kFixedDeltaSeconds;
  const double x = center.x() + (velocity.x() * horizon_seconds);
  const double y = center.y() + (velocity.y() * horizon_seconds);
  if (!std::isfinite(x) || !std::isfinite(y) ||
      std::abs(x) > simulation::kMaximumPhysicalComponentMagnitude ||
      std::abs(y) > simulation::kMaximumPhysicalComponentMagnitude) {
    return center;
  }
  return simulation::Vector2::create(x, y);
}

double tactical_candidate_score(const TacticalObjectiveCandidate& candidate,
                                const TacticalObjectivePolicy& policy, const bool held) noexcept {
  const double weight =
      policy.objective_weights[tactical_objective_kind_ordinal(candidate.key.kind)];
  const double proximity = 1.0 - candidate.normalized_distance;
  const double preference = weight * proximity;
  const double penalty = candidate.escape_blocked ? 1.0 - policy.risk_tolerance : 0.0;
  const double bonus = held ? kTacticalHeldTargetBonus : 0.0;
  return (preference - penalty) + bonus;
}

std::size_t tactical_select_candidate(const std::span<const TacticalObjectiveCandidate> candidates,
                                      const TacticalObjectivePolicy& policy,
                                      const std::optional<TacticalObjectiveKey>& held) noexcept {
  std::size_t best = candidates.size();
  double best_score = 0.0;
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    const auto& value = candidates[index];
    const double score =
        tactical_candidate_score(value, policy, held.has_value() && *held == value.key);
    if (best == candidates.size() || score > best_score ||
        (score == best_score && tactical_candidate_precedes(value, candidates[best]))) {
      best = index;
      best_score = score;
    }
  }
  return best;
}

bool tactical_candidate_precedes(const TacticalObjectiveCandidate& left,
                                 const TacticalObjectiveCandidate& right) noexcept {
  if (left.squared_distance != right.squared_distance) {
    return left.squared_distance < right.squared_distance;
  }
  return left.key < right.key;
}

} // namespace blob_royale::controllers
