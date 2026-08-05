#ifndef BLOB_ROYALE_SIMULATION_VECTOR2_HPP
#define BLOB_ROYALE_SIMULATION_VECTOR2_HPP

#include <string_view>

namespace blob_royale::simulation {

class Vector2 final {
public:
  [[nodiscard]] static Vector2 create(double x, double y);

  Vector2(const Vector2&) = default;
  Vector2(Vector2&&) noexcept = default;
  Vector2& operator=(const Vector2&) = default;
  Vector2& operator=(Vector2&&) noexcept = default;
  ~Vector2() = default;

  [[nodiscard]] double x() const noexcept { return x_; }
  [[nodiscard]] double y() const noexcept { return y_; }

  [[nodiscard]] Vector2 operator-() const;
  [[nodiscard]] Vector2 operator+(const Vector2& other) const;
  [[nodiscard]] Vector2 operator-(const Vector2& other) const;
  [[nodiscard]] Vector2 operator*(double scalar) const;
  [[nodiscard]] Vector2 operator/(double scalar) const;

  [[nodiscard]] double dot(const Vector2& other) const noexcept;
  [[nodiscard]] double magnitude() const noexcept;

  friend Vector2 operator*(const double scalar, const Vector2& vector) { return vector * scalar; }

  friend bool operator==(const Vector2&, const Vector2&) = default;

private:
  Vector2(double x, double y) noexcept : x_(x), y_(y) {}

  [[nodiscard]] static Vector2 create_for_operation(double x, double y, std::string_view operation);

  double x_;
  double y_;
};

} // namespace blob_royale::simulation

#endif
