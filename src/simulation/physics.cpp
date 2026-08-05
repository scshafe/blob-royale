#include "physics.hpp"

#include "simulation_limits.hpp"
#include "simulation_tolerance.hpp"
#include "simulation_validation_error.hpp"

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
  const double contact_distance = require_finite_result(
      2.0 * validated_radius, "physics.detect_player_pair_contact.contact_distance");
  const bool is_contact =
      less_than_or_approximately_equal(center_distance, contact_distance, kPositionTolerance);

  return PlayerPairContact{is_contact, normal, center_distance, relative_normal_speed};
}

PlayerPairCollisionResult resolve_player_pair_collision(const PhysicsBody& first_body,
                                                        const PhysicsBody& second_body,
                                                        const double player_radius) {
  const PlayerPairContact contact =
      detect_player_pair_contact(first_body, second_body, player_radius);
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

} // namespace blob_royale::simulation
