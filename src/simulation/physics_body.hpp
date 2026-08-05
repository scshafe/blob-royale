#ifndef BLOB_ROYALE_SIMULATION_PHYSICS_BODY_HPP
#define BLOB_ROYALE_SIMULATION_PHYSICS_BODY_HPP

#include "vector2.hpp"

namespace blob_royale::simulation {

class PhysicsBody final {
public:
  [[nodiscard]] static PhysicsBody create(Vector2 position, Vector2 velocity, Vector2 acceleration);

  PhysicsBody(const PhysicsBody&) = default;
  PhysicsBody(PhysicsBody&&) noexcept = default;
  PhysicsBody& operator=(const PhysicsBody&) = default;
  PhysicsBody& operator=(PhysicsBody&&) noexcept = default;
  ~PhysicsBody() = default;

  [[nodiscard]] const Vector2& position() const& noexcept { return position_; }
  [[nodiscard]] const Vector2& position() const&& = delete;
  [[nodiscard]] const Vector2& velocity() const& noexcept { return velocity_; }
  [[nodiscard]] const Vector2& velocity() const&& = delete;
  [[nodiscard]] const Vector2& acceleration() const& noexcept { return acceleration_; }
  [[nodiscard]] const Vector2& acceleration() const&& = delete;

  [[nodiscard]] PhysicsBody with_position(Vector2 position) const;
  [[nodiscard]] PhysicsBody with_velocity(Vector2 velocity) const;
  [[nodiscard]] PhysicsBody with_acceleration(Vector2 acceleration) const;

  friend bool operator==(const PhysicsBody&, const PhysicsBody&) = default;

private:
  PhysicsBody(Vector2 position, Vector2 velocity, Vector2 acceleration) noexcept;

  Vector2 position_;
  Vector2 velocity_;
  Vector2 acceleration_;
};

} // namespace blob_royale::simulation

#endif
