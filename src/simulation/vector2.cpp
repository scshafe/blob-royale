#include "vector2.hpp"

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <cmath>
#include <string>
#include <string_view>

namespace blob_royale::simulation {
namespace {

[[nodiscard]] double validate_component(const double value, const std::string_view context) {
  if (!std::isfinite(value)) {
    throw SimulationValidationError(SimulationValidationCode::kPhysicalScalarNotFinite,
                                    std::string(context), "component must be finite");
  }
  if (std::abs(value) > kMaximumPhysicalComponentMagnitude) {
    throw SimulationValidationError(SimulationValidationCode::kPhysicalScalarOutOfRange,
                                    std::string(context),
                                    "component magnitude exceeds the accepted simulation limit");
  }
  return value == 0.0 ? 0.0 : value;
}

[[nodiscard]] double validate_scalar_operand(const double value, const std::string_view context) {
  if (!std::isfinite(value)) {
    throw SimulationValidationError(SimulationValidationCode::kPhysicalScalarNotFinite,
                                    std::string(context), "scalar operand must be finite");
  }
  return value == 0.0 ? 0.0 : value;
}

} // namespace

Vector2 Vector2::create(const double x, const double y) {
  return create_for_operation(x, y, "vector2.create");
}

Vector2 Vector2::operator-() const { return create_for_operation(-x_, -y_, "vector2.negate"); }

Vector2 Vector2::operator+(const Vector2& other) const {
  return create_for_operation(x_ + other.x_, y_ + other.y_, "vector2.add");
}

Vector2 Vector2::operator-(const Vector2& other) const {
  return create_for_operation(x_ - other.x_, y_ - other.y_, "vector2.subtract");
}

Vector2 Vector2::operator*(const double scalar) const {
  const double validated_scalar = validate_scalar_operand(scalar, "vector2.scale.scalar");
  return create_for_operation(x_ * validated_scalar, y_ * validated_scalar, "vector2.scale");
}

Vector2 Vector2::operator/(const double scalar) const {
  const double validated_scalar = validate_scalar_operand(scalar, "vector2.divide.scalar");
  if (validated_scalar == 0.0) {
    throw SimulationValidationError(SimulationValidationCode::kVectorDivisionByZero,
                                    "vector2.divide.scalar", "scalar divisor must be nonzero");
  }
  return create_for_operation(x_ / validated_scalar, y_ / validated_scalar, "vector2.divide");
}

double Vector2::dot(const Vector2& other) const noexcept {
  return (x_ * other.x_) + (y_ * other.y_);
}

double Vector2::magnitude() const noexcept { return std::hypot(x_, y_); }

Vector2 Vector2::create_for_operation(const double x, const double y,
                                      const std::string_view operation) {
  const std::string context_prefix(operation);
  const double validated_x = validate_component(x, context_prefix + ".x");
  const double validated_y = validate_component(y, context_prefix + ".y");
  return Vector2(validated_x, validated_y);
}

} // namespace blob_royale::simulation
