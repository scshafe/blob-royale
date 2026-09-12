#ifndef BLOB_ROYALE_CONTROLLERS_CONTROLLER_STEERING_HPP
#define BLOB_ROYALE_CONTROLLERS_CONTROLLER_STEERING_HPP

#include "vector2.hpp"

namespace blob_royale::controllers {

// Raw coordinate differences deliberately are not a Vector2: two valid signed physical positions
// can differ by more than Vector2's component bound. No new validation tightens that old domain.
struct ControllerTargetOffset final {
  double x;
  double y;
};

// canonical: controller_target_geometry -- the diagnostic bots' written subtraction/product/sqrt
// order, without hypot, reassociation, normalization, or materialized bounded-vector differences.
// Valid Vector2 inputs produce finite offsets and squared magnitudes. These functions never throw.
[[nodiscard]] ControllerTargetOffset
controller_target_offset(const simulation::Vector2& origin,
                         const simulation::Vector2& target) noexcept;
[[nodiscard]] double controller_squared_magnitude(ControllerTargetOffset offset) noexcept;
[[nodiscard]] double controller_magnitude(ControllerTargetOffset offset) noexcept;

// canonical: controller_direction_component_clamp -- the source-side [-1,1] command range only.
// This is not locomotion's vector-magnitude clamp. Preserves interior values, including signed
// zero; infinities clamp and NaN passes through for the existing Vector2 validation to reject
// later.
[[nodiscard]] double clamp_controller_direction_component(double component) noexcept;

} // namespace blob_royale::controllers

#endif
