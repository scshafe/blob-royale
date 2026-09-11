#include "arena_bounds.hpp"

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <cmath>
#include <string>
#include <string_view>

namespace blob_royale::simulation {
namespace {

void require_arena_scalar(const double value, const std::string_view context) {
  if (!std::isfinite(value)) {
    throw SimulationValidationError{SimulationValidationCode::kArenaBoundsScalarNotFinite,
                                    std::string{context}, "value must be finite"};
  }
  if (value <= 0.0 || value > kMaximumWorldDimension) {
    throw SimulationValidationError{SimulationValidationCode::kArenaBoundsScalarOutOfRange,
                                    std::string{context},
                                    "value must be greater than zero and at most 1000000000"};
  }
}

} // namespace

ArenaBounds ArenaBounds::create(const double width, const double height) {
  require_arena_scalar(width, "map_definition.bounds.width_world_units");
  require_arena_scalar(height, "map_definition.bounds.height_world_units");
  return ArenaBounds{width, height};
}

bool ArenaBounds::contains(const Vector2& point) const noexcept {
  return point.x() >= 0.0 && point.x() <= width_ && point.y() >= 0.0 && point.y() <= height_;
}

bool ArenaBounds::contains_disc_center(const Vector2& point, const double radius) const noexcept {
  return point.x() >= radius && point.x() <= width_ - radius && point.y() >= radius &&
         point.y() <= height_ - radius;
}

ArenaBounds::ArenaBounds(const double width, const double height) noexcept
    : width_(width), height_(height) {}

} // namespace blob_royale::simulation
