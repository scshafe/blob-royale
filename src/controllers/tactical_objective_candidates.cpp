#include "tactical_objective_candidates.hpp"

#include "component_join.hpp"
#include "components/controllable_component.hpp"
#include "components/hill_component.hpp"
#include "components/hill_motion_component.hpp"
#include "components/lethal_on_contact_component.hpp"
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

// The published arena diagonal: the one scale every arena-relative number in this file is measured
// against. Validated arena bounds are finite and greater than zero, so it is a positive divisor.
[[nodiscard]] double arena_diagonal(const Observation& observation) noexcept {
  const auto& bounds = observation.terrain().bounds();
  return controller_magnitude({bounds.width(), bounds.height()});
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

// One opponent the shove provider kept, flattened to scalars because `std::array` default-
// constructs its elements and neither `EntityId` nor `Vector2` has a default constructor. Every
// value here was read out of a published, already validated `PhysicsBody`.
struct ShoveOpponent final {
  std::uint64_t entity{};
  double x{};
  double y{};
  double radius{};
  double squared_distance{};
};

// The nearest-N total order: distance first, then the stable entity id. It ends at an id and never
// at a store position, so two toolchains keep the same N opponents out of an identical world.
[[nodiscard]] bool shove_opponent_precedes(const ShoveOpponent& left,
                                           const ShoveOpponent& right) noexcept {
  if (left.squared_distance != right.squared_distance) {
    return left.squared_distance < right.squared_distance;
  }
  return left.entity < right.entity;
}

// The fixed-size nearest-N filter itself. It is an insertion into a bounded array and deliberately
// not `std::nth_element` or `std::partial_sort`: neither states an order among equal elements, and
// two opponents at identical squared distance would then be kept or dropped per toolchain.
struct NearestOpponents final {
  std::array<ShoveOpponent, kMaximumTacticalShoveCandidateCount> entries{};
  std::size_t count{};
};

static_assert(kMaximumTacticalShoveCandidateCount > 0,
              "the nearest-N insertion below replaces its last element when full, which needs at "
              "least one element to replace");

void insert_nearest(NearestOpponents& nearest, const ShoveOpponent& opponent) noexcept {
  const std::size_t width = nearest.entries.size();
  if (nearest.count == width && !shove_opponent_precedes(opponent, nearest.entries[width - 1])) {
    return;
  }
  std::size_t index = nearest.count < width ? nearest.count : width - 1;
  while (index > 0 && shove_opponent_precedes(opponent, nearest.entries[index - 1])) {
    nearest.entries[index] = nearest.entries[index - 1];
    --index;
  }
  nearest.entries[index] = opponent;
  if (nearest.count < width) {
    ++nearest.count;
  }
}

// The four snapshot-visible badnesses a shove can aim an opponent at, in the order their ordinals
// break a distance tie. They are ordinals of one order, not a dispatch table.
enum class ShoveHazardSource : std::uint8_t {
  kHole = 0,
  kCorridorEdge = 1,
  kLethalEntity = 2,
  kHillExit = 3
};

// One badness, reduced to what the standing point needs: how far the opponent is from it, and the
// unit direction `unit(O - Hazard)` that points at the safe side the shover stands on.
struct ShoveHazard final {
  double distance{};
  double away_x{};
  double away_y{};
  ShoveHazardSource source{ShoveHazardSource::kHole};
  std::uint64_t identity{};
  bool found{false};
};

// The explicit total order the nearest badness is chosen under: smallest published distance, then
// the source ordinal, then the source's own stable id -- an authored index for terrain, an
// `EntityId` for a component. No term is a store position or a discovery order.
[[nodiscard]] bool shove_hazard_precedes(const ShoveHazard& left,
                                         const ShoveHazard& right) noexcept {
  if (left.distance != right.distance) {
    return left.distance < right.distance;
  }
  if (left.source != right.source) {
    return left.source < right.source;
  }
  return left.identity < right.identity;
}

void offer_hazard(ShoveHazard& best, const ShoveHazard& hazard) noexcept {
  if (!best.found || shove_hazard_precedes(hazard, best)) {
    best = hazard;
  }
}

// The badness nearest one opponent, over authored terrain and published centres only. No support
// query, no clearance query, and no extrapolation, which is why the header does not count this as a
// prediction step.
//
// A corridor edge and a hill exit are offered **only when the opponent is inside them**: shoving a
// body off a road it already left, or out of a hill it is not in, is not an objective, and
// admitting either would put a zero distance at the head of the order on every pass.
[[nodiscard]] ShoveHazard nearest_shove_hazard(const Observation& observation,
                                               const simulation::Vector2& position) {
  const auto& terrain = observation.terrain();
  ShoveHazard best;
  std::uint64_t hole_index = 0;
  for (const auto& hole : terrain.holes()) {
    const auto delta = controller_target_offset(hole.center(), position);
    const double distance = controller_magnitude(delta);
    if (distance > 0.0) {
      offer_hazard(best, {distance, delta.x / distance, delta.y / distance,
                          ShoveHazardSource::kHole, hole_index, true});
    }
    ++hole_index;
  }
  if (terrain.ground() == simulation::TerrainGround::kCorridors) {
    // The nearest centreline is chosen on the distance-only form of the canonical projection, and
    // only the winner materializes its point, so at most one corridor projection can throw.
    const simulation::TerrainCorridor* road = nullptr;
    double road_distance = 0.0;
    std::uint64_t road_index = 0;
    std::uint64_t corridor_index = 0;
    for (const auto& corridor : terrain.corridors()) {
      const double distance = simulation::corridor_distance_to_centreline(corridor, position);
      if (road == nullptr || distance < road_distance) {
        road = &corridor;
        road_distance = distance;
        road_index = corridor_index;
      }
      ++corridor_index;
    }
    if (road != nullptr && road_distance <= road->half_width()) {
      const auto projection = simulation::corridor_project_to_centreline(*road, position);
      const auto outward = controller_target_offset(projection.point, position);
      const double magnitude = controller_magnitude(outward);
      if (magnitude > 0.0) {
        // The badness is the void past the edge, so the safe side is the centreline's and the
        // published distance is how much road is left between the opponent and that edge.
        offer_hazard(best,
                     {road->half_width() - projection.distance, -outward.x / magnitude,
                      -outward.y / magnitude, ShoveHazardSource::kCorridorEdge, road_index, true});
      }
    }
  }
  const auto& snapshot = observation.snapshot();
  simulation::for_each_entity_with_both(
      snapshot.components<simulation::LethalOnContact>(),
      snapshot.components<simulation::PhysicsBody>(),
      [&best, &position](const simulation::EntityId entity, const simulation::LethalOnContact&,
                         const simulation::PhysicsBody& hazard) {
        const auto delta = controller_target_offset(hazard.position(), position);
        const double distance = controller_magnitude(delta);
        if (distance > 0.0) {
          offer_hazard(best, {distance, delta.x / distance, delta.y / distance,
                              ShoveHazardSource::kLethalEntity, entity.value(), true});
        }
      });
  for (const auto& entry : snapshot.components<simulation::Hill>()) {
    const auto delta = controller_target_offset(entry.value.center, position);
    const double distance = controller_magnitude(delta);
    // Out of the hill is the one badness that runs outward, so the safe side is the centre's. A
    // published radius that is not a number fails this comparison and offers nothing, which is the
    // same answer the circle provider's `require_radius` reaches by throwing -- and a throw here
    // would be a second rejection of a store this provider only reads.
    if (distance > 0.0 && distance <= entry.value.radius) {
      offer_hazard(best, {entry.value.radius - distance, -delta.x / distance, -delta.y / distance,
                          ShoveHazardSource::kHillExit, entry.entity.value(), true});
    }
  }
  return best;
}

// The unconditional provider: one shove candidate per nearby opponent that has somewhere to be
// shoved. Its target is the safe-side standing point S and never the opponent; the header states
// why at length, and a "simplification" to the opponent's position breaks `escape_blocked` and
// `risk_tolerance` together.
//
// An opponent is a body carrying a `Controllable` whose `controller_id` is not this observation's.
// Comparing the durable identity rather than the resolved `EntityId` costs no lookup, needs no
// `Observation::entity()`, and also excludes a second body of the bot's own controller.
[[nodiscard]] TacticalObjectiveCandidates
shove_setup(const Observation& observation, const simulation::PhysicsBody& body,
            [[maybe_unused]] const TacticalObjectivePolicy& policy) {
  const auto& snapshot = observation.snapshot();
  NearestOpponents nearest;
  simulation::for_each_entity_with_both(
      snapshot.components<simulation::PhysicsBody>(),
      snapshot.components<simulation::Controllable>(),
      [&nearest, &observation, &body](const simulation::EntityId entity,
                                      const simulation::PhysicsBody& other,
                                      const simulation::Controllable& controllable) {
        if (controllable.controller_id == observation.controller() || other.is_static()) {
          return;
        }
        const auto delta = controller_target_offset(body.position(), other.position());
        insert_nearest(nearest, {entity.value(), other.position().x(), other.position().y(),
                                 other.radius(), controller_squared_magnitude(delta)});
      });
  const double margin = arena_diagonal(observation) * kTacticalShoveStandoffDiagonalFraction;
  TacticalObjectiveCandidates result;
  result.candidates.reserve(nearest.count);
  for (std::size_t index = 0; index < nearest.count; ++index) {
    const auto& opponent = nearest.entries[index];
    // Both components came out of a published `Vector2`, so this cannot fail its own validation.
    const auto position = simulation::Vector2::create(opponent.x, opponent.y);
    const auto hazard = nearest_shove_hazard(observation, position);
    if (!hazard.found) {
      // Nothing on this map to shove them into. That is an absent candidate, not a failure.
      continue;
    }
    const double standoff = body.radius() + opponent.radius + margin;
    const double x = opponent.x + (hazard.away_x * standoff);
    const double y = opponent.y + (hazard.away_y * standoff);
    if (!std::isfinite(x) || !std::isfinite(y) ||
        std::abs(x) > simulation::kMaximumPhysicalComponentMagnitude ||
        std::abs(y) > simulation::kMaximumPhysicalComponentMagnitude) {
      // A standing point outside the representable domain is refused for the same reason
      // `tactical_predicted_center` refuses one: a throw would be isolated by the host and would
      // stop the bot for the whole pass over a single unreachable candidate.
      continue;
    }
    // Arrival is the margin, not the standoff: a bot one margin from S on the opponent's side is
    // exactly `r_self + r_opponent` from the opponent, which is contact.
    result.candidates.push_back(candidate({TacticalObjectiveKind::kShoveSetup, opponent.entity},
                                          simulation::Vector2::create(x, y), margin, body));
  }
  return result;
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

// The rows a `Registration` cannot express. A row is matched on a schema id, and there is no
// spelling of "every schema" that is not a sentinel a later reader has to be told about, so a
// provider that runs under all of them is a second table rather than a fourth row in the first one.
// Their dispositions are not read: the schema-keyed provider owns that answer, and these merge only
// when it says `kReady`.
constexpr std::array<Provider, 1> kUnconditionalProviders{{&shove_setup}};

static_assert(1 + kUnconditionalProviders.size() == kTacticalObjectiveProviderCount,
              "exactly one schema-keyed provider runs per pass, beside every unconditional one, "
              "and the derived prediction ceiling in the header counts both");

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
  // The budget is enforced on each provider's own raw count and never on the merged one: a shared
  // budget lets one provider exhaust the other's and turn a legal world into a throw the host
  // isolates, which leaves the bot silently inert for the pass.
  auto raw = provider(observation, body, policy);
  require_candidate_count(raw.candidates.size());
  if (raw.disposition == TacticalObjectiveDisposition::kReady) {
    for (const auto& unconditional : kUnconditionalProviders) {
      auto merged = unconditional(observation, body, policy);
      require_candidate_count(merged.candidates.size());
      // Zero today, because authored caution extrapolates no opponent; accumulated rather than
      // assumed, so a later unconditional row cannot under-report its own predictions.
      raw.work.prediction_step_count += merged.work.prediction_step_count;
      raw.candidates.insert(raw.candidates.end(), merged.candidates.begin(),
                            merged.candidates.end());
    }
  }
  TacticalObjectiveCandidates result{raw.disposition, {}, raw.work};
  result.work.raw_candidate_count = raw.candidates.size();
  result.candidates.reserve(raw.candidates.size());
  const double diagonal = arena_diagonal(observation);
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

const simulation::PhysicsBody*
tactical_shove_opponent_body(const simulation::WorldSnapshot& snapshot,
                             const TacticalObjectiveCandidate& candidate) noexcept {
  if (candidate.key.kind != TacticalObjectiveKind::kShoveSetup) {
    return nullptr;
  }
  for (const auto& entry : snapshot.components<simulation::PhysicsBody>()) {
    if (entry.entity.value() == candidate.key.subject) {
      return &entry.value;
    }
  }
  return nullptr;
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

bool tactical_charge_screen_admits(const Observation& observation,
                                   const simulation::PhysicsBody& body,
                                   const TacticalObjectiveCandidate& candidate,
                                   const TacticalObjectivePolicy& policy) {
  const auto* opponent = tactical_shove_opponent_body(observation.snapshot(), candidate);
  if (opponent == nullptr) {
    return false;
  }
  const auto delta = controller_target_offset(body.position(), opponent->position());
  const double distance = controller_magnitude(delta);
  if (distance <= 0.0) {
    return false;
  }
  const double ray_length = arena_diagonal(observation) * policy.charge_screen_diagonal_fraction;
  if (!(ray_length > 0.0)) {
    // A screen of no length examined nothing, and a refusal on no evidence is not a screen.
    return true;
  }
  const double unit_x = delta.x / distance;
  const double unit_y = delta.y / distance;
  const auto displacement = simulation::Vector2::create(unit_x * ray_length, unit_y * ray_length);
  const auto support_exit =
      simulation::first_support_exit(observation.terrain(), body.position(), displacement);
  if (!support_exit) {
    return true;
  }
  // The comparison, not `.has_value()`: a shove charge points at the hazard by construction, so a
  // presence test is always true and would veto every shove.
  const double contact_time = (distance - body.radius() - opponent->radius()) / ray_length;
  return support_exit->value() > contact_time;
}

bool tactical_charge_alignment_admits(const Observation& observation,
                                      const simulation::PhysicsBody& body,
                                      const TacticalObjectiveCandidate& candidate) noexcept {
  const auto* opponent = tactical_shove_opponent_body(observation.snapshot(), candidate);
  if (opponent == nullptr) {
    return false;
  }
  const auto delta = controller_target_offset(body.position(), opponent->position());
  const double distance = controller_magnitude(delta);
  if (distance <= 0.0) {
    return false;
  }
  const double unit_x = delta.x / distance;
  const double unit_y = delta.y / distance;
  // The component of committed velocity across the commanded ray, written out in this domain's
  // multiply-then-add order rather than taken from a cross product this library does not own.
  const double perpendicular = (body.velocity().x() * -unit_y) + (body.velocity().y() * unit_x);
  const double ceiling = observation.snapshot().match().movement().current.normal_top_speed();
  return std::abs(perpendicular) <= ceiling * kTacticalChargeAlignmentPerpendicularFraction;
}

bool tactical_opponent_closes_to_contact(const Observation& observation,
                                         const simulation::PhysicsBody& body,
                                         const TacticalObjectivePolicy& policy) {
  if (policy.shield_anticipation_ticks == 0) {
    return false;
  }
  const double window_seconds =
      static_cast<double>(policy.shield_anticipation_ticks) * simulation::kFixedDeltaSeconds;
  const double self_x = body.position().x() + (body.velocity().x() * window_seconds);
  const double self_y = body.position().y() + (body.velocity().y() * window_seconds);
  bool closing = false;
  simulation::for_each_entity_with_both(
      observation.snapshot().components<simulation::PhysicsBody>(),
      observation.snapshot().components<simulation::Controllable>(),
      [&closing, &observation, &body, window_seconds, self_x,
       self_y](const simulation::EntityId, const simulation::PhysicsBody& other,
               const simulation::Controllable& controllable) {
        if (closing || controllable.controller_id == observation.controller() ||
            other.is_static()) {
          return;
        }
        const double x = other.position().x() + (other.velocity().x() * window_seconds);
        const double y = other.position().y() + (other.velocity().y() * window_seconds);
        const double delta_x = x - self_x;
        const double delta_y = y - self_y;
        const double contact = body.radius() + other.radius();
        closing = ((delta_x * delta_x) + (delta_y * delta_y)) <= (contact * contact);
      });
  return closing;
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
