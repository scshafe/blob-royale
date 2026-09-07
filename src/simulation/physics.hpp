#ifndef BLOB_ROYALE_SIMULATION_PHYSICS_HPP
#define BLOB_ROYALE_SIMULATION_PHYSICS_HPP

#include "fixed_delta.hpp"
#include "physics_body.hpp"
#include "vector2.hpp"

namespace blob_royale::simulation {

// canonical: deterministic_physics -- pure fixed-step integration, contact, and wall equations.
class PlayerPairContact;
class PlayerPairCollisionResult;
class WallMotionResult;

// Detects an equal-radius disc contact at committed positions. The radius must be finite and
// positive; invalid input or an invalid calculation throws SimulationValidationError.
[[nodiscard]] PlayerPairContact detect_player_pair_contact(const PhysicsBody& first_body,
                                                           const PhysicsBody& second_body,
                                                           double player_radius);

// Resolves one equal-unit-mass frictionless pair in first/second order. Non-contacting and
// stationary-or-separating pairs retain their velocities. Invalid calculations throw
// SimulationValidationError.
[[nodiscard]] PlayerPairCollisionResult
resolve_player_pair_collision(const PhysicsBody& first_body, const PhysicsBody& second_body,
                              double player_radius);

// Resolves the complete fixed-step motion against an axis-aligned world, x before y. Geometry
// must be finite, positive, and contain the committed position; invalid input or output throws
// SimulationValidationError.
[[nodiscard]] WallMotionResult
resolve_player_wall_motion(const Vector2& position, const Vector2& velocity, double world_width,
                           double world_height, double player_radius, FixedDelta fixed_delta);

class PlayerPairContact final {
public:
  PlayerPairContact(const PlayerPairContact&) = default;
  PlayerPairContact(PlayerPairContact&&) noexcept = default;
  PlayerPairContact& operator=(const PlayerPairContact&) = default;
  PlayerPairContact& operator=(PlayerPairContact&&) noexcept = default;
  ~PlayerPairContact() = default;

  [[nodiscard]] bool is_contact() const noexcept { return is_contact_; }
  [[nodiscard]] const Vector2& normal() const& noexcept { return normal_; }
  [[nodiscard]] const Vector2& normal() const&& = delete;
  [[nodiscard]] double center_distance() const noexcept { return center_distance_; }
  [[nodiscard]] double relative_normal_speed() const noexcept { return relative_normal_speed_; }

  friend bool operator==(const PlayerPairContact&, const PlayerPairContact&) = default;

private:
  friend PlayerPairContact detect_player_pair_contact(const PhysicsBody&, const PhysicsBody&,
                                                      double);

  PlayerPairContact(bool is_contact, Vector2 normal, double center_distance,
                    double relative_normal_speed) noexcept;

  bool is_contact_;
  Vector2 normal_;
  double center_distance_;
  double relative_normal_speed_;
};

class PlayerPairCollisionResult final {
public:
  PlayerPairCollisionResult(const PlayerPairCollisionResult&) = default;
  PlayerPairCollisionResult(PlayerPairCollisionResult&&) noexcept = default;
  PlayerPairCollisionResult& operator=(const PlayerPairCollisionResult&) = default;
  PlayerPairCollisionResult& operator=(PlayerPairCollisionResult&&) noexcept = default;
  ~PlayerPairCollisionResult() = default;

  [[nodiscard]] const PlayerPairContact& contact() const& noexcept { return contact_; }
  [[nodiscard]] const PlayerPairContact& contact() const&& = delete;
  [[nodiscard]] bool impulse_applied() const noexcept { return impulse_applied_; }
  [[nodiscard]] const Vector2& first_velocity() const& noexcept { return first_velocity_; }
  [[nodiscard]] const Vector2& first_velocity() const&& = delete;
  [[nodiscard]] const Vector2& second_velocity() const& noexcept { return second_velocity_; }
  [[nodiscard]] const Vector2& second_velocity() const&& = delete;

  friend bool operator==(const PlayerPairCollisionResult&,
                         const PlayerPairCollisionResult&) = default;

private:
  friend PlayerPairCollisionResult resolve_player_pair_collision(const PhysicsBody&,
                                                                 const PhysicsBody&, double);

  PlayerPairCollisionResult(PlayerPairContact contact, bool impulse_applied, Vector2 first_velocity,
                            Vector2 second_velocity) noexcept;

  PlayerPairContact contact_;
  bool impulse_applied_;
  Vector2 first_velocity_;
  Vector2 second_velocity_;
};

class WallMotionResult final {
public:
  WallMotionResult(const WallMotionResult&) = default;
  WallMotionResult(WallMotionResult&&) noexcept = default;
  WallMotionResult& operator=(const WallMotionResult&) = default;
  WallMotionResult& operator=(WallMotionResult&&) noexcept = default;
  ~WallMotionResult() = default;

  [[nodiscard]] const Vector2& displacement() const& noexcept { return displacement_; }
  [[nodiscard]] const Vector2& displacement() const&& = delete;
  [[nodiscard]] const Vector2& terminal_velocity() const& noexcept { return terminal_velocity_; }
  [[nodiscard]] const Vector2& terminal_velocity() const&& = delete;

  friend bool operator==(const WallMotionResult&, const WallMotionResult&) = default;

private:
  friend WallMotionResult resolve_player_wall_motion(const Vector2&, const Vector2&, double, double,
                                                     double, FixedDelta);

  WallMotionResult(Vector2 displacement, Vector2 terminal_velocity) noexcept;

  Vector2 displacement_;
  Vector2 terminal_velocity_;
};

// Applies stored acceleration for exactly one canonical fixed step using semi-implicit Euler.
// An out-of-contract result throws SimulationValidationError.
[[nodiscard]] Vector2 integrate_accelerated_velocity(const Vector2& velocity,
                                                     const Vector2& stored_acceleration,
                                                     FixedDelta fixed_delta);

// Scales one accelerated velocity by the phase 1 drag factor `max(0, 1 - drag_per_second * dt)`,
// in the written operation order of
// `docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick" phase 1.
// `drag_per_second` must be finite and non-negative; the clamp at zero keeps the factor total when
// `drag_per_second * dt` exceeds one, so a large configured drag stops a body rather than
// reversing it. At `drag_per_second = 0` the factor is exactly 1.0 and the result is the argument.
// Invalid input or an out-of-contract result throws SimulationValidationError.
[[nodiscard]] Vector2 apply_velocity_drag(const Vector2& accelerated_velocity,
                                          double drag_per_second, FixedDelta fixed_delta);

// Applies a wall-resolved tick-local displacement to a committed position. An out-of-contract
// result throws SimulationValidationError.
[[nodiscard]] Vector2 integrate_position(const Vector2& position, const Vector2& tick_displacement);

} // namespace blob_royale::simulation

#endif
