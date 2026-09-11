#include "shared/locomotion.hpp"

#include "gameplay_validation_error.hpp"
#include "movement_tuning.hpp"
#include "physics.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace blob_royale::gameplay {
namespace {

[[noreturn]] void precision_lost(const std::string_view detail) {
  throw GameplayValidationError(GameplayValidationCode::kLocomotionPrecisionLost,
                                "locomotion.normal_propulsion", std::string(detail));
}

} // namespace

simulation::Vector2 normalized_thrust_intent(const simulation::Vector2& direction) {
  const double x = direction.x();
  const double y = direction.y();
  const double magnitude = std::sqrt((x * x) + (y * y));
  const double scale = magnitude <= 1.0 ? 1.0 : 1.0 / magnitude;
  return simulation::Vector2::create(x * scale, y * scale);
}

simulation::Vector2 thrust_acceleration_from_intent(const simulation::Vector2& intent,
                                                    const double acceleration) {
  return simulation::Vector2::create(intent.x() * acceleration, intent.y() * acceleration);
}

simulation::Vector2 limit_normal_propulsion(const simulation::Vector2& velocity,
                                            const simulation::Vector2& requested_acceleration,
                                            const double normal_top_speed,
                                            const simulation::FixedDelta delta) {
  const double ceiling =
      simulation::MovementTuning::create(0.0, normal_top_speed).normal_top_speed();
  const auto requested_endpoint =
      simulation::integrate_accelerated_velocity(velocity, requested_acceleration, delta);
  const double bound_squared = std::max(ceiling * ceiling, velocity.dot(velocity));
  const double endpoint_squared = requested_endpoint.dot(requested_endpoint);
  if (endpoint_squared <= bound_squared) {
    return requested_acceleration;
  }

  const auto zero = simulation::Vector2::create(0.0, 0.0);
  const double requested_acceleration_squared = requested_acceleration.dot(requested_acceleration);
  double projection_scale = std::sqrt(bound_squared) / std::sqrt(endpoint_squared);
  const auto first_target = requested_endpoint * projection_scale;
  if (first_target == velocity) {
    // This is the defined identity of the first rounded projection, not a collinearity test and
    // not a correction fallback. Latch it before any directed rounding can change the target.
    return zero;
  }

  for (std::size_t radial_step = 0; radial_step <= kMaximumLocomotionRoundingStepCount;
       ++radial_step) {
    // Reconstruct each target from the original canonical endpoint, never the previous target.
    const auto target = radial_step == 0 ? first_target : requested_endpoint * projection_scale;
    const auto projected_acceleration = (target - velocity) / delta.seconds();
    const double projected_squared = projected_acceleration.dot(projected_acceleration);
    double acceleration_scale = projected_squared > requested_acceleration_squared
                                    ? std::min(1.0, std::sqrt(requested_acceleration_squared) /
                                                        std::sqrt(projected_squared))
                                    : 1.0;
    for (std::size_t magnitude_step = 0; magnitude_step <= kMaximumLocomotionRoundingStepCount;
         ++magnitude_step) {
      const auto candidate = acceleration_scale == 1.0
                                 ? projected_acceleration
                                 : projected_acceleration * acceleration_scale;
      if (candidate == zero) {
        precision_lost("rounding correction erased a nonidentity projected step");
      }
      const auto candidate_endpoint =
          simulation::integrate_accelerated_velocity(velocity, candidate, delta);
      if (candidate_endpoint == velocity) {
        precision_lost(
            "rounding correction materialized an identity instead of the projected step");
      }
      if (candidate.dot(candidate) <= requested_acceleration_squared &&
          candidate_endpoint.dot(candidate_endpoint) <= bound_squared) {
        return candidate;
      }
      acceleration_scale = std::nextafter(acceleration_scale, 0.0);
    }
    projection_scale = std::nextafter(projection_scale, 0.0);
  }
  precision_lost("bounded projection rounding could not preserve speed and propulsion magnitude");
}

} // namespace blob_royale::gameplay
