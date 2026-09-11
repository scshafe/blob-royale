#include "king_of_the_hill/hill_roaming.hpp"

#include "gameplay_validation_error.hpp"
#include "physics.hpp"

#include <cmath>
#include <cstdint>

namespace blob_royale::gameplay {
namespace {

struct HillAxisMotion final {
  double position;
  double velocity;
};

[[nodiscard]] HillAxisMotion cancel_boundary_axis(const double start, const double proposed,
                                                  const double velocity, const double upper) {
  if (proposed < 0.0 || proposed > upper) {
    return HillAxisMotion{start, 0.0};
  }
  if ((proposed == 0.0 && velocity < 0.0) || (proposed == upper && velocity > 0.0)) {
    return HillAxisMotion{proposed, 0.0};
  }
  return HillAxisMotion{proposed, velocity};
}

} // namespace

HillRoamSelection sample_hill_roam(simulation::DeterministicRandom& random,
                                   const KingOfTheHillConfiguration& configuration) {
  const double parameter = random.next_unit_interval();
  const std::uint64_t quadrant = random.next_below(4);
  const double speed_fraction = random.next_unit_interval();
  const std::uint64_t retarget_span =
      configuration.hill_retarget_maximum_ticks() - configuration.hill_retarget_minimum_ticks() + 1;
  const std::uint64_t retarget_offset = random.next_below(retarget_span);

  const double parameter_squared = parameter * parameter;
  const double denominator = 1.0 + parameter_squared;
  const double first = (1.0 - parameter_squared) / denominator;
  const double second = (2.0 * parameter) / denominator;
  const double direction_length = std::sqrt((first * first) + (second * second));
  const double normalized_first = first / direction_length;
  const double normalized_second = second / direction_length;
  const double selected_speed =
      configuration.hill_speed_minimum() +
      ((configuration.hill_speed_maximum() - configuration.hill_speed_minimum()) * speed_fraction);

  double direction_x = normalized_first;
  double direction_y = normalized_second;
  if (quadrant == 1) {
    direction_x = -normalized_second;
    direction_y = normalized_first;
  } else if (quadrant == 2) {
    direction_x = -normalized_first;
    direction_y = -normalized_second;
  } else if (quadrant == 3) {
    direction_x = normalized_second;
    direction_y = -normalized_first;
  }

  return HillRoamSelection{
      simulation::Vector2::create(direction_x * selected_speed, direction_y * selected_speed),
      configuration.hill_retarget_minimum_ticks() + retarget_offset};
}

HillRoamMotion advance_hill_roam(const simulation::Vector2& center,
                                 const simulation::Vector2& velocity,
                                 const simulation::ArenaBounds& bounds,
                                 const simulation::FixedDelta fixed_delta) {
  if (!bounds.contains(center)) {
    throw GameplayValidationError{
        GameplayValidationCode::kKingOfTheHillScalarOutOfRange, "hill_roaming.center",
        "the committed hill center must lie in the closed outer map rectangle"};
  }

  const auto proposed_motion = simulation::resolve_unbounded_motion(velocity, fixed_delta);
  const auto proposed_center =
      simulation::integrate_position(center, proposed_motion.displacement());
  const auto x_motion =
      cancel_boundary_axis(center.x(), proposed_center.x(), velocity.x(), bounds.width());
  const auto y_motion =
      cancel_boundary_axis(center.y(), proposed_center.y(), velocity.y(), bounds.height());
  return HillRoamMotion{simulation::Vector2::create(x_motion.position, y_motion.position),
                        simulation::Vector2::create(x_motion.velocity, y_motion.velocity)};
}

} // namespace blob_royale::gameplay
