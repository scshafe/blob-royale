#include "shared/guarded_pair_contact.hpp"

#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/lethal_on_contact_component.hpp"
#include "contact_rule_name.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "physics.hpp"
#include "simulation_limits.hpp"
#include "simulation_tolerance.hpp"
#include "simulation_validation_error.hpp"

#include <cmath>
#include <cstddef>
#include <string_view>
#include <utility>

namespace blob_royale::gameplay {
namespace {

// The exact ceiling this composition can emit, stated by the header: at most two recipient facts
// in ascending EntityId order, then exactly one canonical contact fact. Reserving it once is not a
// micro-optimization -- every branch below appends into the same vector, so a single allocation of
// the known bound is the honest shape -- and it is also what keeps this translation unit buildable
// at -O3 under GCC 13. Since plan Step 18 moved the lethal predicates into this file, the optimizer
// inlines them, sees through `append_recipients`, and reports a false
// `-Wstringop-overflow` "writing 1 byte into a region of size 0" against `std::variant`'s
// discriminant inside `vector::_M_realloc_insert` (the reallocating path this reserve removes).
// Giving the vector its capacity up front is the fix rather than a suppression: no warning is
// disabled, the emitted facts and their order are unchanged, and the bound is the one the header
// already promises.
constexpr std::size_t kMaximumConsequenceCount = 3;

[[nodiscard]] bool is_guarded(const GuardState guard) noexcept {
  return guard != GuardState::kNone;
}

void require_guard_facts(const simulation::ContactRule::Subject& subject, const GuardState guard) {
  if (guard != GuardState::kNone && guard != GuardState::kOrdinary &&
      guard != GuardState::kPerfect) {
    throw simulation::SimulationValidationError{
        simulation::SimulationValidationCode::kGuardedPairFactsInvalid, "guarded_pair.guard",
        "guard must be none, ordinary, or perfect"};
  }
  if (subject.body.is_static() && is_guarded(guard)) {
    throw simulation::SimulationValidationError{
        simulation::SimulationValidationCode::kGuardedPairFactsInvalid, "guarded_pair.static_body",
        "a static contact subject must have no guard"};
  }
}

[[nodiscard]] simulation::Vector2 motion_velocity(const simulation::PhysicsBody& body) {
  // Static fields remain authored values; integration gives static geometry zero motion.
  return body.is_static() ? simulation::Vector2::create(0.0, 0.0) : body.velocity();
}

[[nodiscard]] double normal_speed(const simulation::Vector2& velocity,
                                  const simulation::Vector2& normal) noexcept {
  return (velocity.x() * normal.x()) + (velocity.y() * normal.y());
}

[[nodiscard]] double relative_normal_speed(const simulation::PhysicsBody& first,
                                           const simulation::PhysicsBody& second,
                                           const simulation::Vector2& normal) {
  const simulation::Vector2 first_velocity = motion_velocity(first);
  const simulation::Vector2 second_velocity = motion_velocity(second);
  return ((second_velocity.x() - first_velocity.x()) * normal.x()) +
         ((second_velocity.y() - first_velocity.y()) * normal.y());
}

[[nodiscard]] bool non_closing(const double speed) noexcept {
  return simulation::greater_than_or_approximately_equal(speed, 0.0,
                                                         simulation::kVelocityTolerance);
}

[[nodiscard]] double require_finite_correction(const double value) {
  if (!std::isfinite(value)) {
    throw simulation::SimulationValidationError{
        simulation::SimulationValidationCode::kGuardedPairSeparationFailed,
        "guarded_pair.separation", "normal separation correction must remain finite"};
  }
  return value;
}

[[nodiscard]] simulation::Vector2 quarter_received_delta(const simulation::Vector2& before,
                                                         const simulation::Vector2& ordinary) {
  // Scalar intermediates deliberately precede Vector2 construction: two accepted opposite
  // velocities may have a delta outside Vector2's component bound while the final result fits.
  return simulation::Vector2::create(before.x() + ((ordinary.x() - before.x()) * 0.25),
                                     before.y() + ((ordinary.y() - before.y()) * 0.25));
}

void separate_pair(simulation::PhysicsBody& first, simulation::PhysicsBody& second,
                   const bool first_stopped, const bool second_stopped,
                   const simulation::Vector2& normal) {
  const double speed = relative_normal_speed(first, second, normal);
  if (non_closing(speed)) {
    return;
  }
  const double first_weight =
      first.is_static() || first_stopped ? 0.0 : require_finite_correction(1.0 / first.mass());
  const double second_weight =
      second.is_static() || second_stopped ? 0.0 : require_finite_correction(1.0 / second.mass());
  const double weight_sum = require_finite_correction(first_weight + second_weight);
  const double normal_squared =
      require_finite_correction((normal.x() * normal.x()) + (normal.y() * normal.y()));
  const double denominator = require_finite_correction(weight_sum * normal_squared);
  if (denominator <= 0.0) {
    throw simulation::SimulationValidationError{
        simulation::SimulationValidationCode::kGuardedPairSeparationFailed,
        "guarded_pair.separation", "a closing pair requires positive movable correction weight"};
  }
  const double impulse = require_finite_correction(-speed / denominator);
  const double first_delta = require_finite_correction(impulse * first_weight);
  const double second_delta = require_finite_correction(impulse * second_weight);
  if (first_weight != 0.0) {
    first = first.with_velocity(
        simulation::Vector2::create(first.velocity().x() - (first_delta * normal.x()),
                                    first.velocity().y() - (first_delta * normal.y())));
  }
  if (second_weight != 0.0) {
    second = second.with_velocity(
        simulation::Vector2::create(second.velocity().x() + (second_delta * normal.x()),
                                    second.velocity().y() + (second_delta * normal.y())));
  }
  const double residual = relative_normal_speed(first, second, normal);
  if (!std::isfinite(residual) || !non_closing(residual)) {
    throw simulation::SimulationValidationError{
        simulation::SimulationValidationCode::kGuardedPairSeparationFailed,
        "guarded_pair.separation",
        "binary64 projection left closing motion beyond velocity tolerance"};
  }
}

template <class Fact>
void append_recipients(std::vector<GuardedPairConsequence>& effects,
                       const simulation::EntityId first, const bool include_first,
                       const simulation::EntityId second, const bool include_second) {
  if (first < second) {
    if (include_first) {
      effects.emplace_back(Fact{first});
    }
    if (include_second) {
      effects.emplace_back(Fact{second});
    }
  } else {
    if (include_second) {
      effects.emplace_back(Fact{second});
    }
    if (include_first) {
      effects.emplace_back(Fact{first});
    }
  }
}

} // namespace

// Moved verbatim from the deleted `shared/lethal_hazard_contact_rule.cpp`, operation for operation,
// because the composition below is their only non-test caller and the plan's promotion rule makes a
// rehoming a byte-for-byte move rather than an opportunity to restate anything.
bool body_is_lethal_hazard(const simulation::GameWorld& world, const simulation::EntityId entity) {
  // The phase is read here, on the hazard's side, because "lethal" is a property a hazard has only
  // while a match is on: outside `running` the same body still carries the marker and is still a
  // heavy disc the impulse equations resolve, it just cannot kill. See the header for why the gate
  // is the admission's and not the recorder's.
  return world.match().phase == simulation::MatchPhase::kRunning &&
         world.store<simulation::LethalOnContact>().find(entity) != nullptr;
}

bool body_is_player_driven(const simulation::GameWorld& world, const simulation::EntityId entity) {
  return world.store<simulation::Controllable>().find(entity) != nullptr;
}

GuardedPairOutcome compose_guarded_pair(const simulation::GameWorld& committed,
                                        const simulation::ContactRule::Subject& first,
                                        const simulation::ContactRule::Subject& second,
                                        const simulation::PairContactObservation& observation,
                                        const simulation::TickContext&,
                                        const PairGuardFacts& guards) {
  require_guard_facts(first, guards.first);
  require_guard_facts(second, guards.second);
  if (first.entity == second.entity) {
    throw simulation::SimulationValidationError{
        simulation::SimulationValidationCode::kGuardedPairFactsInvalid, "guarded_pair.subjects",
        "a pair must name two distinct entities"};
  }
  GuardedPairOutcome outcome{{first.body}, {second.body}, {}};
  outcome.effects.reserve(kMaximumConsequenceCount);
  if (!observation.touch.is_contact() ||
      (!observation.impact.has_value() && !observation.first_effect_eligible &&
       !observation.second_effect_eligible)) {
    return outcome;
  }

  const bool first_eliminated = observation.second_effect_eligible && !is_guarded(guards.first) &&
                                body_is_player_driven(committed, first.entity) &&
                                body_is_lethal_hazard(committed, second.entity);
  const bool second_eliminated = observation.first_effect_eligible && !is_guarded(guards.second) &&
                                 body_is_player_driven(committed, second.entity) &&
                                 body_is_lethal_hazard(committed, first.entity);
  if (first_eliminated || second_eliminated) {
    // Preserve the accepted lethal pass-through branch for BOTH bodies, including the survivor.
    outcome.first.disposition = first_eliminated ? simulation::MotionDisposition::kTerminate
                                                 : simulation::MotionDisposition::kContinue;
    outcome.second.disposition = second_eliminated ? simulation::MotionDisposition::kTerminate
                                                   : simulation::MotionDisposition::kContinue;
    append_recipients<GuardedPairEliminationFact>(outcome.effects, first.entity, first_eliminated,
                                                  second.entity, second_eliminated);
    outcome.effects.emplace_back(GuardedPairContactFact{simulation::contact_event_of(
        first, second, observation.touch,
        simulation::ContactRuleName::create(kLethalHazardContactRuleName))});
    return outcome;
  }

  if (!observation.impact.has_value()) {
    // A geometric touch may block lethality, but it cannot create quartered impulse, separation
    // correction, or a perfect stop. The solver owns the independent closing-impact admission.
    outcome.effects.emplace_back(GuardedPairContactFact{simulation::contact_event_of(
        first, second, observation.touch, simulation::ContactRuleName::create("guarded_pair"))});
    return outcome;
  }
  const simulation::PlayerPairContact& contact = *observation.impact;
  if (!first.body.is_static() && !second.body.is_static()) {
    const simulation::PlayerPairCollisionResult collision =
        simulation::body_has_baseline_physics(first.body) &&
                simulation::body_has_baseline_physics(second.body)
            ? simulation::resolve_player_pair_collision(first.body, second.body, contact)
            : simulation::resolve_general_pair_collision(first.body, second.body, contact);
    outcome.first.body = first.body.with_velocity(collision.first_velocity());
    outcome.second.body = second.body.with_velocity(collision.second_velocity());
  } else if (!non_closing(contact.relative_normal_speed())) {
    if (!first.body.is_static()) {
      outcome.first.body = first.body.with_velocity(
          simulation::reflect_static_contact_velocity(first.body.velocity(), contact.normal()));
    }
    if (!second.body.is_static()) {
      outcome.second.body = second.body.with_velocity(
          simulation::reflect_static_contact_velocity(second.body.velocity(), contact.normal()));
    }
  }
  if (is_guarded(guards.first)) {
    outcome.first.body = outcome.first.body.with_velocity(
        quarter_received_delta(first.body.velocity(), outcome.first.body.velocity()));
  }
  if (is_guarded(guards.second)) {
    outcome.second.body = outcome.second.body.with_velocity(
        quarter_received_delta(second.body.velocity(), outcome.second.body.velocity()));
  }

  // Incoming means world-frame motion into the defender, not merely a closing relative speed.
  // Evaluate both from the same pre-response values before zeroing either recipient.
  const bool closing = !non_closing(contact.relative_normal_speed());
  const bool first_stunned =
      guards.second == GuardState::kPerfect && !first.body.is_static() && closing &&
      normal_speed(motion_velocity(first.body), contact.normal()) > simulation::kVelocityTolerance;
  const bool second_stunned = guards.first == GuardState::kPerfect && !second.body.is_static() &&
                              closing &&
                              normal_speed(motion_velocity(second.body), contact.normal()) <
                                  -simulation::kVelocityTolerance;
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  if (first_stunned) {
    outcome.first.body = outcome.first.body.with_velocity(zero).with_acceleration(zero);
  }
  if (second_stunned) {
    outcome.second.body = outcome.second.body.with_velocity(zero).with_acceleration(zero);
  }
  if (is_guarded(guards.first) || is_guarded(guards.second)) {
    // Defense owns this extra projection; an unguarded pair preserves the selected accepted
    // equation byte for byte, including restitution-zero roundoff. The solver's treatment of an
    // inadmissible residual is its explicit precision policy, never an invisible gameplay impulse.
    separate_pair(outcome.first.body, outcome.second.body, first_stunned, second_stunned,
                  contact.normal());
  }
  append_recipients<GuardedPairStunFact>(outcome.effects, first.entity, first_stunned,
                                         second.entity, second_stunned);
  outcome.effects.emplace_back(GuardedPairContactFact{simulation::contact_event_of(
      first, second, observation.touch, simulation::ContactRuleName::create("guarded_pair"))});
  return outcome;
}

} // namespace blob_royale::gameplay
