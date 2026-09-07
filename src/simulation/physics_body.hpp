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
//
// **`radius_` is not read by any accepted phase.** Every phase takes the one common radius from
// `SimulationConfig::player_radius()`: the pair contact predicate uses `2r`, the wall fold uses
// `[r, extent - r]`, the spatial index sizes its cells from it, and the spawn occupancy test
// measures against it. So the constant that fills the field is named `kUndeclaredRadius` rather
// than `kDefaultRadius`: `0.0` is not a body one wu across, it is a body that declares no size and
// defers to the configuration (engine review finding 10).
//
// **A body that reaches a world nevertheless carries the configured radius, not the placeholder.**
// `GameWorld::create(configuration, map, seed)`, `SpawnSystem`, and `ScenarioLoader` each fill it
// in at seating time, because `physics-body-component.schema.json` requires a positive radius and a
// published placeholder made every live match unencodable. The placeholder therefore survives only
// between construction and seating -- in the motion-only `create` and `create_static` overloads a
// map loader, a test, or a benchmark uses -- and what a snapshot publishes is always the radius the
// kernel actually measured with.
//
// It stays a field rather than being deleted because making it authoritative is the growing-blob
// change, and that is a **versioned physics change on ADR 0003's amendment path**, not a cleanup:
// unequal radii replace the accepted equal-mass pair equation with the general impulse equation and
// regenerate every accepted pair and wall fixture
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Justified extension points and
// what-if stress"; `docs/architecture/0005-royale-mode.md` § "Considered Options" B, rejected for
// exactly that cost). What that change needs from this file is `with_radius`, which now exists, so
// a growth system is a `kPostKernel` system writing `body.with_radius(...)` and the remaining cost
// is entirely in the kernel and its fixtures rather than in this value.
// related: with_radius -- the value operation a growing blob needs.
// related: simulation_config.hpp -- where every accepted phase reads the radius it actually uses.
class PhysicsBody final {
public:
  using CollisionLayer = std::uint32_t;

  // Not a radius: the absence of a declared one. See the note above the class.
  static constexpr double kUndeclaredRadius = 0.0;
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

  // A wall or obstacle: it takes part in the broad phase and in contact resolution and is never
  // integrated, accelerated, or dragged. Zero velocity and zero acceleration are structural here
  // rather than incidental, because no phase would ever consume them.
  [[nodiscard]] static PhysicsBody create_static(Vector2 position);
  [[nodiscard]] static PhysicsBody create_static(Vector2 position, CollisionLayer collision_layer,
                                                 CollisionLayer collision_mask);

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
  // The wither a growing blob needs. No accepted phase reads `radius()`, so this changes only the
  // value; it is here so the growth change is a system plus a kernel amendment rather than a system
  // plus a missing operation on the one body type.
  [[nodiscard]] PhysicsBody with_radius(double radius) const;

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

// canonical: collision_admission -- the one predicate that admits a candidate pair to phase 3.
//
// A canonical pair is admitted only when `(a.collision_mask & b.collision_layer)` and
// `(b.collision_mask & a.collision_layer)` are both nonzero. It is a pure integer predicate over
// two values and adds no ordering, which is why it sits before the narrow phase rather than inside
// a contact rule (`docs/architecture/0004-gameplay-architecture.md`
// § "Entities, components, and stores"). Every baseline body carries the single default layer and
// mask, so the accepted fixtures admit exactly the pairs they always did.
[[nodiscard]] inline bool collision_masks_admit(const PhysicsBody& first,
                                                const PhysicsBody& second) noexcept {
  return (first.collision_mask() & second.collision_layer()) != 0U &&
         (second.collision_mask() & first.collision_layer()) != 0U;
}

template <> struct ComponentKindName<PhysicsBody> {
  static constexpr std::string_view value = "physics_body";
};

} // namespace blob_royale::simulation

#endif
