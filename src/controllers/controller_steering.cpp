#include "controller_steering.hpp"

#include "simulation_limits.hpp"

#include <cmath>

namespace blob_royale::controllers {

ControllerTargetOffset controller_target_offset(const simulation::Vector2& origin,
                                                const simulation::Vector2& target) noexcept {
  const double delta_x = target.x() - origin.x();
  const double delta_y = target.y() - origin.y();
  return {delta_x, delta_y};
}

double controller_squared_magnitude(const ControllerTargetOffset offset) noexcept {
  return (offset.x * offset.x) + (offset.y * offset.y);
}

double controller_magnitude(const ControllerTargetOffset offset) noexcept {
  return std::sqrt(controller_squared_magnitude(offset));
}

double clamp_controller_direction_component(const double component) noexcept {
  const double limit = simulation::kMaximumThrustDirectionComponentMagnitude;
  if (component > limit) {
    return limit;
  }
  if (component < -limit) {
    return -limit;
  }
  return component;
}

} // namespace blob_royale::controllers
