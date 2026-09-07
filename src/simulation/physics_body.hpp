#ifndef BLOB_ROYALE_SIMULATION_PHYSICS_BODY_HPP
#define BLOB_ROYALE_SIMULATION_PHYSICS_BODY_HPP

#include "component_kind_name.hpp"
#include "vector2.hpp"

#include <cstdint>
#include <string_view>

namespace blob_royale::simulation {

// canonical: physics_body_component -- the one physical body value in the game.
//
// This is the component the physics kernel reads and writes. There is no second body type, so
// exactly one thing in the tree answers to "the body".
//
// `collision_layer` and `collision_mask` are bitmasks: a candidate pair is admitted to the contact
// phase only when `(a.collision_mask & b.collision_layer)` and `(b.collision_mask &
// a.collision_layer)` are both nonzero, which is a pure integer predicate that adds no ordering.
// The kernel does not consult radius, mass, the masks, or `is_static` yet -- the accepted physics
// takes the one common radius from SimulationConfig, and moving it here is a separate, versioned
// physics change.
class PhysicsBody final {
public:
  using CollisionLayer = std::uint32_t;

  static constexpr double kDefaultRadius = 0.0;
  static constexpr double kDefaultMass = 1.0;
  static constexpr CollisionLayer kDefaultCollisionLayer = 1;
  static constexpr CollisionLayer kDefaultCollisionMask = 1;

  // The motion-only body: one baseline dynamic disc on the single default collision layer.
  [[nodiscard]] static PhysicsBody create(Vector2 position, Vector2 velocity, Vector2 acceleration);

  // The complete body, including the fields no accepted phase reads yet.
  [[nodiscard]] static PhysicsBody create(Vector2 position, Vector2 velocity, Vector2 acceleration,
                                          double radius, double mass,
                                          CollisionLayer collision_layer,
                                          CollisionLayer collision_mask, bool is_static);

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
  [[nodiscard]] double radius() const noexcept { return radius_; }
  [[nodiscard]] double mass() const noexcept { return mass_; }
  [[nodiscard]] CollisionLayer collision_layer() const noexcept { return collision_layer_; }
  [[nodiscard]] CollisionLayer collision_mask() const noexcept { return collision_mask_; }
  [[nodiscard]] bool is_static() const noexcept { return is_static_; }

  [[nodiscard]] PhysicsBody with_position(Vector2 position) const;
  [[nodiscard]] PhysicsBody with_velocity(Vector2 velocity) const;
  [[nodiscard]] PhysicsBody with_acceleration(Vector2 acceleration) const;

  friend bool operator==(const PhysicsBody&, const PhysicsBody&) = default;

private:
  PhysicsBody(Vector2 position, Vector2 velocity, Vector2 acceleration, double radius, double mass,
              CollisionLayer collision_layer, CollisionLayer collision_mask,
              bool is_static) noexcept;

  Vector2 position_;
  Vector2 velocity_;
  Vector2 acceleration_;
  double radius_;
  double mass_;
  CollisionLayer collision_layer_;
  CollisionLayer collision_mask_;
  bool is_static_;
};

template <> struct ComponentKindName<PhysicsBody> {
  static constexpr std::string_view value = "physics_body";
};

} // namespace blob_royale::simulation

#endif
