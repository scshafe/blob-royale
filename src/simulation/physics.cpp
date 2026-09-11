#include "physics.hpp"

#include "simulation_limits.hpp"
#include "simulation_tolerance.hpp"
#include "simulation_validation_error.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace blob_royale::simulation {
namespace {

[[nodiscard]] double normalize_signed_zero(const double value) noexcept {
  return value == 0.0 ? 0.0 : value;
}

[[nodiscard]] double require_finite_result(const double value, const std::string_view context) {
  if (!std::isfinite(value)) {
    throw SimulationValidationError{SimulationValidationCode::kPhysicalScalarNotFinite,
                                    std::string{context}, "physics result must be finite"};
  }
  return normalize_signed_zero(value);
}

[[nodiscard]] double require_positive_physical_scalar(const double value,
                                                      const std::string_view context) {
  if (!std::isfinite(value)) {
    throw SimulationValidationError{SimulationValidationCode::kPhysicalScalarNotFinite,
                                    std::string{context}, "value must be finite"};
  }
  if (value <= 0.0 || value > kMaximumPhysicalComponentMagnitude) {
    throw SimulationValidationError{
        SimulationValidationCode::kPhysicalScalarOutOfRange, std::string{context},
        "value must be positive and within the accepted physical component limit"};
  }
  return value;
}

struct AxisMotion final {
  double displacement;
  double terminal_velocity;
};

[[nodiscard]] AxisMotion resolve_wall_axis(const double position, const double velocity,
                                           const double lower, const double upper,
                                           const FixedDelta fixed_delta,
                                           const std::string_view context) {
  const double span = require_finite_result(upper - lower, std::string{context} + ".span");
  if (span <= 0.0) {
    throw SimulationValidationError{SimulationValidationCode::kPhysicalScalarOutOfRange,
                                    std::string{context},
                                    "permitted center interval must have positive width"};
  }

  if (position < lower || position > upper) {
    throw SimulationValidationError{SimulationValidationCode::kPhysicalScalarOutOfRange,
                                    std::string{context} + ".position",
                                    "committed center must lie inside the permitted interval"};
  }

  const double period = require_finite_result(2.0 * span, std::string{context} + ".period");
  if (period <= 0.0) {
    throw SimulationValidationError{SimulationValidationCode::kPhysicalScalarOutOfRange,
                                    std::string{context} + ".period",
                                    "wall-folding period must be positive"};
  }

  const double proposed_endpoint = require_finite_result(
      position + (velocity * fixed_delta.seconds()), std::string{context} + ".proposed_endpoint");
  double reduced = require_finite_result(std::fmod(proposed_endpoint - lower, period),
                                         std::string{context} + ".reduced_endpoint");
  if (reduced < 0.0) {
    reduced = require_finite_result(reduced + period,
                                    std::string{context} + ".positive_reduced_endpoint");
  }

  double folded_endpoint = 0.0;
  double terminal_velocity = 0.0;
  if (reduced <= span) {
    folded_endpoint =
        require_finite_result(lower + reduced, std::string{context} + ".ascending_endpoint");
    terminal_velocity = velocity;
  } else {
    folded_endpoint = require_finite_result(upper - (reduced - span),
                                            std::string{context} + ".descending_endpoint");
    terminal_velocity =
        require_finite_result(-velocity, std::string{context} + ".reflected_velocity");
  }

  if (reduced == 0.0) {
    folded_endpoint = lower;
    terminal_velocity = std::abs(velocity);
  } else if (reduced == span) {
    folded_endpoint = upper;
    terminal_velocity = -std::abs(velocity);
  } else {
    const double lower_distance = std::abs(folded_endpoint - lower);
    const double upper_distance = std::abs(folded_endpoint - upper);
    const bool near_lower = approximately_equal(folded_endpoint, lower, kPositionTolerance);
    const bool near_upper = approximately_equal(folded_endpoint, upper, kPositionTolerance);
    // A very small valid span can be within tolerance of both walls. The nearest wall wins, with
    // the lower wall as the deterministic tie-breaker; exact endpoints were handled above.
    if (near_lower && (!near_upper || lower_distance <= upper_distance)) {
      folded_endpoint = lower;
      terminal_velocity = std::abs(velocity);
    } else if (near_upper) {
      folded_endpoint = upper;
      terminal_velocity = -std::abs(velocity);
    }
  }

  const double displacement =
      require_finite_result(folded_endpoint - position, std::string{context} + ".displacement");
  return AxisMotion{displacement, normalize_signed_zero(terminal_velocity)};
}

} // namespace

PlayerPairContact::PlayerPairContact(const bool is_contact, Vector2 normal,
                                     const double center_distance,
                                     const double relative_normal_speed) noexcept
    : is_contact_(is_contact), normal_(normal), center_distance_(center_distance),
      relative_normal_speed_(relative_normal_speed) {}

PlayerPairCollisionResult::PlayerPairCollisionResult(PlayerPairContact contact,
                                                     const bool impulse_applied,
                                                     Vector2 first_velocity,
                                                     Vector2 second_velocity) noexcept
    : contact_(contact), impulse_applied_(impulse_applied), first_velocity_(first_velocity),
      second_velocity_(second_velocity) {}

WallMotionResult::WallMotionResult(Vector2 displacement, Vector2 terminal_velocity) noexcept
    : displacement_(displacement), terminal_velocity_(terminal_velocity) {}

Vector2 integrate_accelerated_velocity(const Vector2& velocity, const Vector2& stored_acceleration,
                                       const FixedDelta fixed_delta) {
  const double delta_seconds = fixed_delta.seconds();
  const double accelerated_x =
      require_finite_result(velocity.x() + (stored_acceleration.x() * delta_seconds),
                            "physics.integrate_accelerated_velocity.x");
  const double accelerated_y =
      require_finite_result(velocity.y() + (stored_acceleration.y() * delta_seconds),
                            "physics.integrate_accelerated_velocity.y");
  return Vector2::create(accelerated_x, accelerated_y);
}

Vector2 apply_velocity_drag(const Vector2& accelerated_velocity, const double drag_per_second,
                            const FixedDelta fixed_delta) {
  if (!std::isfinite(drag_per_second) || drag_per_second < 0.0) {
    throw SimulationValidationError{SimulationValidationCode::kPhysicalScalarOutOfRange,
                                    "physics.apply_velocity_drag.drag_per_second",
                                    "drag must be finite and greater than or equal to zero"};
  }
  const double delta_seconds = fixed_delta.seconds();
  const double drag_factor = require_finite_result(
      std::max(0.0, 1.0 - (drag_per_second * delta_seconds)), "physics.apply_velocity_drag.factor");
  return accelerated_velocity * drag_factor;
}

Vector2 integrate_position(const Vector2& position, const Vector2& tick_displacement) {
  const double integrated_x =
      require_finite_result(position.x() + tick_displacement.x(), "physics.integrate_position.x");
  const double integrated_y =
      require_finite_result(position.y() + tick_displacement.y(), "physics.integrate_position.y");
  return Vector2::create(integrated_x, integrated_y);
}

PlayerPairContact detect_player_pair_contact(const PhysicsBody& first_body,
                                             const PhysicsBody& second_body,
                                             const double player_radius) {
  const double validated_radius =
      require_positive_physical_scalar(player_radius, "physics.player_pair.player_radius");
  // `2 * r` is exact in binary64 and `require_positive_physical_scalar` has already bounded `r`, so
  // hoisting the contact distance above the geometry cannot throw and cannot change a value: the
  // returned contact is bit-identical to the one this function produced before it delegated.
  const double contact_distance = require_finite_result(
      2.0 * validated_radius, "physics.detect_player_pair_contact.contact_distance");
  return detect_pair_contact(first_body, second_body, contact_distance);
}

double pair_contact_distance(const PhysicsBody& first_body, const PhysicsBody& second_body,
                             const double configured_radius) {
  const double validated_configured_radius = require_positive_physical_scalar(
      configured_radius, "physics.pair_contact_distance.configured_radius");
  const double first_radius =
      require_positive_physical_scalar(effective_radius(first_body, validated_configured_radius),
                                       "physics.pair_contact_distance.first_radius");
  const double second_radius =
      require_positive_physical_scalar(effective_radius(second_body, validated_configured_radius),
                                       "physics.pair_contact_distance.second_radius");
  return require_finite_result(first_radius + second_radius,
                               "physics.pair_contact_distance.contact_distance");
}

PlayerPairContact detect_pair_contact(const PhysicsBody& first_body, const PhysicsBody& second_body,
                                      const double contact_distance) {
  // Finite and positive, but deliberately *not* bounded by the physical component limit: the
  // accepted path reaches here with `2 * player_radius`, which may legitimately be twice that
  // limit, and rejecting it would narrow a rule this refactor must leave exactly as it was.
  if (!std::isfinite(contact_distance)) {
    throw SimulationValidationError{SimulationValidationCode::kPhysicalScalarNotFinite,
                                    "physics.detect_pair_contact.contact_distance",
                                    "contact distance must be finite"};
  }
  if (contact_distance <= 0.0) {
    throw SimulationValidationError{SimulationValidationCode::kPhysicalScalarOutOfRange,
                                    "physics.detect_pair_contact.contact_distance",
                                    "contact distance must be greater than zero"};
  }

  // The failure contexts below keep the accepted function's name. This is the one detection and
  // `detect_player_pair_contact` is still what names it in the contract, so renaming them would
  // change accepted diagnostics to say nothing new.
  const double delta_x =
      require_finite_result(second_body.position().x() - first_body.position().x(),
                            "physics.detect_player_pair_contact.delta_x");
  const double delta_y =
      require_finite_result(second_body.position().y() - first_body.position().y(),
                            "physics.detect_player_pair_contact.delta_y");
  const double center_distance = require_finite_result(
      std::hypot(delta_x, delta_y), "physics.detect_player_pair_contact.center_distance");

  double normal_x = 1.0;
  double normal_y = 0.0;
  if (!approximately_equal(center_distance, 0.0, kPositionTolerance)) {
    normal_x = require_finite_result(delta_x / center_distance,
                                     "physics.detect_player_pair_contact.normal_x");
    normal_y = require_finite_result(delta_y / center_distance,
                                     "physics.detect_player_pair_contact.normal_y");
  } else {
    const double relative_velocity_x =
        require_finite_result(first_body.velocity().x() - second_body.velocity().x(),
                              "physics.detect_player_pair_contact.coincident_relative_velocity_x");
    const double relative_velocity_y =
        require_finite_result(first_body.velocity().y() - second_body.velocity().y(),
                              "physics.detect_player_pair_contact.coincident_relative_velocity_y");
    const double relative_velocity_magnitude = require_finite_result(
        std::hypot(relative_velocity_x, relative_velocity_y),
        "physics.detect_player_pair_contact.coincident_relative_velocity_magnitude");
    if (!approximately_equal(relative_velocity_magnitude, 0.0, kVelocityTolerance)) {
      normal_x = require_finite_result(relative_velocity_x / relative_velocity_magnitude,
                                       "physics.detect_player_pair_contact.coincident_normal_x");
      normal_y = require_finite_result(relative_velocity_y / relative_velocity_magnitude,
                                       "physics.detect_player_pair_contact.coincident_normal_y");
    }
  }

  const Vector2 normal = Vector2::create(normal_x, normal_y);
  const double relative_normal_speed = require_finite_result(
      ((second_body.velocity().x() - first_body.velocity().x()) * normal.x()) +
          ((second_body.velocity().y() - first_body.velocity().y()) * normal.y()),
      "physics.detect_player_pair_contact.relative_normal_speed");
  const bool is_contact =
      less_than_or_approximately_equal(center_distance, contact_distance, kPositionTolerance);

  return PlayerPairContact{is_contact, normal, center_distance, relative_normal_speed};
}

PlayerPairCollisionResult resolve_player_pair_collision(const PhysicsBody& first_body,
                                                        const PhysicsBody& second_body,
                                                        const double player_radius) {
  const PlayerPairContact contact =
      detect_player_pair_contact(first_body, second_body, player_radius);
  return resolve_player_pair_collision(first_body, second_body, contact);
}

double combined_restitution(const double first_restitution,
                            const double second_restitution) noexcept {
  return std::min(first_restitution, second_restitution);
}

PlayerPairCollisionResult resolve_general_pair_collision(const PhysicsBody& first_body,
                                                         const PhysicsBody& second_body,
                                                         const double configured_radius) {
  // The same detection and the same rejection as the accepted narrow phase, measured at the pair's
  // own contact distance rather than at twice one common radius. `configured_radius` is the
  // fallback for a body that declares no size, which is why it is still an argument.
  const PlayerPairContact contact = detect_pair_contact(
      first_body, second_body, pair_contact_distance(first_body, second_body, configured_radius));
  return resolve_general_pair_collision(first_body, second_body, contact);
}

PlayerPairCollisionResult resolve_player_pair_collision(const PhysicsBody& first_body,
                                                        const PhysicsBody& second_body,
                                                        const PlayerPairContact& contact) {
  if (!contact.is_contact() || greater_than_or_approximately_equal(contact.relative_normal_speed(),
                                                                   0.0, kVelocityTolerance)) {
    return PlayerPairCollisionResult{contact, false, first_body.velocity(), second_body.velocity()};
  }

  const double first_normal_speed =
      require_finite_result((first_body.velocity().x() * contact.normal().x()) +
                                (first_body.velocity().y() * contact.normal().y()),
                            "physics.resolve_player_pair_collision.first_normal_speed");
  const double second_normal_speed =
      require_finite_result((second_body.velocity().x() * contact.normal().x()) +
                                (second_body.velocity().y() * contact.normal().y()),
                            "physics.resolve_player_pair_collision.second_normal_speed");

  const double first_normal_delta =
      require_finite_result(second_normal_speed - first_normal_speed,
                            "physics.resolve_player_pair_collision.first_normal_delta");
  const double second_normal_delta =
      require_finite_result(first_normal_speed - second_normal_speed,
                            "physics.resolve_player_pair_collision.second_normal_delta");

  const Vector2 first_velocity = Vector2::create(
      require_finite_result(first_body.velocity().x() + (first_normal_delta * contact.normal().x()),
                            "physics.resolve_player_pair_collision.first_velocity.x"),
      require_finite_result(first_body.velocity().y() + (first_normal_delta * contact.normal().y()),
                            "physics.resolve_player_pair_collision.first_velocity.y"));
  const Vector2 second_velocity =
      Vector2::create(require_finite_result(
                          second_body.velocity().x() + (second_normal_delta * contact.normal().x()),
                          "physics.resolve_player_pair_collision.second_velocity.x"),
                      require_finite_result(
                          second_body.velocity().y() + (second_normal_delta * contact.normal().y()),
                          "physics.resolve_player_pair_collision.second_velocity.y"));

  return PlayerPairCollisionResult{contact, true, first_velocity, second_velocity};
}

PlayerPairCollisionResult resolve_general_pair_collision(const PhysicsBody& first_body,
                                                         const PhysicsBody& second_body,
                                                         const PlayerPairContact& contact) {
  if (!contact.is_contact() || greater_than_or_approximately_equal(contact.relative_normal_speed(),
                                                                   0.0, kVelocityTolerance)) {
    return PlayerPairCollisionResult{contact, false, first_body.velocity(), second_body.velocity()};
  }

  // The written operation order, which
  // `docs/architecture/0003-deterministic-simulation-contract.md` § "Floating-point contract"
  // requires be preserved and which no reassociation or contraction may alter:
  //
  //   1. inverse_first_mass   = 1 / m_a
  //   2. inverse_second_mass  = 1 / m_b
  //   3. inverse_mass_sum     = inverse_first_mass + inverse_second_mass
  //   4. restitution          = min(e_a, e_b)
  //   5. impulse_numerator    = -((1 + restitution) * (v_rel . n))
  //   6. impulse_scalar       = impulse_numerator / inverse_mass_sum
  //   7. first_normal_delta   = impulse_scalar / m_a
  //      second_normal_delta  = impulse_scalar / m_b
  //   8. v_a'.x = v_a.x - (first_normal_delta * n.x), then v_a'.y likewise
  //      v_b'.x = v_b.x + (second_normal_delta * n.x), then v_b'.y likewise
  //
  // `(1 + restitution)` is formed before it multiplies the relative normal speed, and the negation
  // is applied to that whole product; the two per-body scalings divide the one impulse scalar by
  // each mass rather than multiplying by an already-rounded reciprocal, which is the equation as
  // written. `v_rel . n` is the contact's own `relative_normal_speed`, which is `(v_b - v_a) . n`
  // with `n` directed from a to b: an approaching pair makes it negative, so the impulse scalar is
  // positive and `b` is pushed along `+n` while `a` is pushed along `-n`.
  //
  // **Both masses are strictly positive because both bodies are dynamic, not because every
  // PhysicsBody has a positive mass.** A static body is deliberately permitted to carry zero, since
  // nothing reads a wall's mass. What rules a wall out here is the caller: `variable_impulse`
  // declares `body_is_variable_dynamic` and `body_is_dynamic` as its predicates, so both subjects
  // are dynamic, and `PhysicsBody` validation rejects a non-positive mass on a dynamic body. The
  // two reciprocals are therefore defined and their sum is strictly positive; the finiteness checks
  // still stand because a validly tiny mass can overflow its own reciprocal.
  const double inverse_first_mass = require_finite_result(
      1.0 / first_body.mass(), "physics.resolve_general_pair_collision.inverse_first_mass");
  const double inverse_second_mass = require_finite_result(
      1.0 / second_body.mass(), "physics.resolve_general_pair_collision.inverse_second_mass");
  const double inverse_mass_sum =
      require_finite_result(inverse_first_mass + inverse_second_mass,
                            "physics.resolve_general_pair_collision.inverse_mass_sum");
  const double restitution =
      combined_restitution(first_body.restitution(), second_body.restitution());
  const double impulse_numerator =
      require_finite_result(-((1.0 + restitution) * contact.relative_normal_speed()),
                            "physics.resolve_general_pair_collision.impulse_numerator");
  const double impulse_scalar =
      require_finite_result(impulse_numerator / inverse_mass_sum,
                            "physics.resolve_general_pair_collision.impulse_scalar");
  const double first_normal_delta =
      require_finite_result(impulse_scalar / first_body.mass(),
                            "physics.resolve_general_pair_collision.first_normal_delta");
  const double second_normal_delta =
      require_finite_result(impulse_scalar / second_body.mass(),
                            "physics.resolve_general_pair_collision.second_normal_delta");

  const Vector2 first_velocity = Vector2::create(
      require_finite_result(first_body.velocity().x() - (first_normal_delta * contact.normal().x()),
                            "physics.resolve_general_pair_collision.first_velocity.x"),
      require_finite_result(first_body.velocity().y() - (first_normal_delta * contact.normal().y()),
                            "physics.resolve_general_pair_collision.first_velocity.y"));
  const Vector2 second_velocity =
      Vector2::create(require_finite_result(
                          second_body.velocity().x() + (second_normal_delta * contact.normal().x()),
                          "physics.resolve_general_pair_collision.second_velocity.x"),
                      require_finite_result(
                          second_body.velocity().y() + (second_normal_delta * contact.normal().y()),
                          "physics.resolve_general_pair_collision.second_velocity.y"));

  return PlayerPairCollisionResult{contact, true, first_velocity, second_velocity};
}

Vector2 reflect_static_contact_velocity(const Vector2& velocity, const Vector2& normal) {
  const double normal_speed = (velocity.x() * normal.x()) + (velocity.y() * normal.y());
  const Vector2 reflected_velocity =
      Vector2::create(velocity.x() - (2.0 * normal_speed * normal.x()),
                      velocity.y() - (2.0 * normal_speed * normal.y()));
  return reflected_velocity;
}

WallMotionResult resolve_player_wall_motion(const Vector2& position, const Vector2& velocity,
                                            const double world_width, const double world_height,
                                            const double player_radius,
                                            const FixedDelta fixed_delta) {
  const double validated_world_width = require_positive_physical_scalar(
      world_width, "physics.resolve_player_wall_motion.world_width");
  const double validated_world_height = require_positive_physical_scalar(
      world_height, "physics.resolve_player_wall_motion.world_height");
  const double validated_radius = require_positive_physical_scalar(
      player_radius, "physics.resolve_player_wall_motion.player_radius");

  if (validated_world_width <= 2.0 * validated_radius ||
      validated_world_height <= 2.0 * validated_radius) {
    throw SimulationValidationError{
        SimulationValidationCode::kPhysicalScalarOutOfRange,
        "physics.resolve_player_wall_motion.geometry",
        "world width and height must each exceed twice the player radius"};
  }

  const AxisMotion x_motion = resolve_wall_axis(
      position.x(), velocity.x(), validated_radius, validated_world_width - validated_radius,
      fixed_delta, "physics.resolve_player_wall_motion.x");
  const AxisMotion y_motion = resolve_wall_axis(
      position.y(), velocity.y(), validated_radius, validated_world_height - validated_radius,
      fixed_delta, "physics.resolve_player_wall_motion.y");

  return WallMotionResult{Vector2::create(x_motion.displacement, y_motion.displacement),
                          Vector2::create(x_motion.terminal_velocity, y_motion.terminal_velocity)};
}

WallMotionResult resolve_unbounded_motion(const Vector2& velocity, const FixedDelta fixed_delta) {
  // No interval, no fold, no reflection: the proposed motion *is* the resolved motion. The written
  // order is one multiplication per axis, x before y, exactly as the folding path forms its
  // proposed endpoint.
  const double delta_seconds = fixed_delta.seconds();
  const double displacement_x = require_finite_result(
      velocity.x() * delta_seconds, "physics.resolve_unbounded_motion.displacement_x");
  const double displacement_y = require_finite_result(
      velocity.y() * delta_seconds, "physics.resolve_unbounded_motion.displacement_y");

  return WallMotionResult{Vector2::create(displacement_x, displacement_y), velocity};
}

} // namespace blob_royale::simulation
