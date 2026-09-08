#ifndef BLOB_ROYALE_SIMULATION_PHYSICS_BODY_HPP
#define BLOB_ROYALE_SIMULATION_PHYSICS_BODY_HPP

#include "component_kind_name.hpp"
#include "vector2.hpp"

#include <cstdint>
#include <string_view>

namespace blob_royale::simulation {

// canonical: bounds_behavior -- whether the arena's walls exist for this body.
//
// `kFold` is the accepted behaviour of
// `docs/architecture/0003-deterministic-simulation-contract.md` § "Wall policy": phase 4 folds the
// body's complete proposed motion into the disc-centre interval, phase 10 rejects a committed
// centre outside that interval, and the broad phase rejects one too. `kCross` is the smallest value
// that expresses "this body travels *through* the arena rather than bouncing inside it", which is
// what a hazard crossing the screen needs: phase 4 applies the proposed motion unfolded, phase 10
// accepts wherever the centre lands, and the broad phase clamps the body's coverage to the edge
// cells it is nearest instead of rejecting it.
//
// `kFold` is the default and every construction that existed before this value carries it, so
// nothing that already ran changes. This is an addition to the contract's wall policy for a body
// that opts out of it, not an amendment of the policy itself: a folding body still folds by exactly
// the accepted equation.
enum class BoundsBehavior : std::uint8_t {
  kFold = 0,
  kCross = 1,
};

// canonical: physics_body_component -- the one physical body value in the game.
//
// This is the component the physics kernel reads and writes. There is no second body type, so
// exactly one thing in the tree answers to "the body".
//
// `collision_layer` and `collision_mask` are bitmasks: a candidate pair is admitted to the contact
// phase only when `(a.collision_mask & b.collision_layer)` and `(b.collision_mask &
// a.collision_layer)` are both nonzero, which is a pure integer predicate that adds no ordering.
//
// `mass_` and `restitution_` are the per-body physics **the general impulse rule reads**, which is
// phase 3. Both default to the accepted baseline -- unit mass and perfectly elastic -- so a body
// that names neither is exactly the body ADR 0003 § "Player-pair policy" is written for, and
// `body_has_baseline_physics` below is the predicate that says so. `restitution_` is the fraction
// of normal closing speed a contact returns: `1.0` is perfectly elastic and `0.0` leaves the pair
// with a common normal velocity. Neither is written by any phase, which is what makes both legal
// for a `ContactRule` predicate to read from the committed world.
//
// `drag_scale_` is the per-body physics **phase 1 reads**, and the distinction from the two above
// is load-bearing rather than pedantic. Phase 1 scales an accelerated velocity by
// `max(0, 1 - drag_per_second * drag_scale * dt)`, so this value says how much of the configured
// `[simulation] drag_per_second` this body feels: `1.0` is all of it, which is what every body
// carried before this value existed, and `0.0` is a body that coasts. It exists because the drag
// factor is geometric and therefore *bounds total travel*: a body launched at `v` and never
// thrusting again covers exactly `v / drag_per_second` world units before it stops, so at the
// deployed `drag_per_second = 2.0` a 260 wu/s object sent across a 960 wu arena has a range of
// 130 wu and stalls into a drifting obstacle. A hazard declares `0.0` and crosses.
//
// **It is deliberately not part of `body_has_baseline_physics`.** That predicate gates which
// *collision* equation a pair takes, and no collision equation reads drag; see the note there.
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
  // Perfectly elastic, which is the restitution the accepted pair equation already assumes.
  static constexpr double kDefaultRestitution = 1.0;
  static constexpr double kMinimumRestitution = 0.0;
  static constexpr double kMaximumRestitution = 1.0;
  // The whole of the configured drag, which is what phase 1 applied to every dynamic body before
  // this value existed. Multiplication by `1.0` is exact in binary64 for every finite value, so
  // `drag_per_second * kDefaultDragScale` **is** `drag_per_second`, bit for bit: a body that names
  // no scale is dragged by the identical arithmetic it always was.
  static constexpr double kDefaultDragScale = 1.0;
  // A body that feels no drag at all. Negative is a rejection rather than a clamp, because it
  // would make the phase 1 factor exceed one and add energy to the body on every tick -- an
  // anti-drag no phase bounds, which is the same reason a restitution above one is rejected.
  static constexpr double kMinimumDragScale = 0.0;
  static constexpr CollisionLayer kDefaultCollisionLayer = 1;
  static constexpr CollisionLayer kDefaultCollisionMask = 1;
  static constexpr BoundsBehavior kDefaultBoundsBehavior = BoundsBehavior::kFold;

  // The motion-only body: one baseline dynamic disc on the single default collision layer.
  [[nodiscard]] static PhysicsBody create(Vector2 position, Vector2 velocity, Vector2 acceleration);

  // The complete body, including the fields no accepted phase reads yet. Restitution, drag scale,
  // and bounds behaviour are deliberately absent from this signature: all three default here, so
  // every call written before they existed keeps its exact meaning, and a body that wants one says
  // so with the named wither rather than by threading three more positional arguments through
  // every call site.
  //
  // Throws SimulationValidationError for a mass that is not finite, not within the accepted
  // physical component limit, or not greater than zero on a dynamic body.
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
  [[nodiscard]] double restitution() const noexcept { return restitution_; }
  // The fraction of the configured `drag_per_second` phase 1 applies to this body.
  [[nodiscard]] double drag_scale() const noexcept { return drag_scale_; }
  [[nodiscard]] CollisionLayer collision_layer() const noexcept { return collision_layer_; }
  [[nodiscard]] CollisionLayer collision_mask() const noexcept { return collision_mask_; }
  [[nodiscard]] bool is_static() const noexcept { return is_static_; }
  [[nodiscard]] BoundsBehavior bounds_behavior() const noexcept { return bounds_behavior_; }
  // The one question phase 4, the commit-time bounds validation, and the broad phase each ask.
  [[nodiscard]] bool crosses_bounds() const noexcept {
    return bounds_behavior_ == BoundsBehavior::kCross;
  }

  [[nodiscard]] PhysicsBody with_position(Vector2 position) const;
  [[nodiscard]] PhysicsBody with_velocity(Vector2 velocity) const;
  [[nodiscard]] PhysicsBody with_acceleration(Vector2 acceleration) const;
  // The wither a growing blob needs. No accepted phase reads `radius()`, so this changes only the
  // value; it is here so the growth change is a system plus a kernel amendment rather than a system
  // plus a missing operation on the one body type.
  [[nodiscard]] PhysicsBody with_radius(double radius) const;
  // Throws SimulationValidationError for a mass that is not finite, not within the accepted
  // physical component limit, or not greater than zero on a dynamic body. A static body may carry
  // zero, which nothing divides by; see the note in the implementation.
  [[nodiscard]] PhysicsBody with_mass(double mass) const;
  // Throws SimulationValidationError for a restitution that is not finite or lies outside the
  // closed interval [0, 1].
  [[nodiscard]] PhysicsBody with_restitution(double restitution) const;
  // Throws SimulationValidationError for a drag scale that is not finite or is negative. There is
  // deliberately no upper bound; see the note in the implementation.
  [[nodiscard]] PhysicsBody with_drag_scale(double drag_scale) const;
  [[nodiscard]] PhysicsBody with_bounds_behavior(BoundsBehavior bounds_behavior) const;

  friend bool operator==(const PhysicsBody&, const PhysicsBody&) = default;

private:
  // The one validating factory. Every public `create` and every wither routes through it, so a
  // body that exists is a body whose mass, restitution, and drag scale are in range however it was
  // built. The mass rule depends on `is_static`, which is why it lives here rather than in a scalar
  // helper.
  [[nodiscard]] static PhysicsBody
  validated(Vector2 position, Vector2 velocity, Vector2 acceleration, double radius, double mass,
            double restitution, double drag_scale, CollisionLayer collision_layer,
            CollisionLayer collision_mask, bool is_static, BoundsBehavior bounds_behavior);

  PhysicsBody(Vector2 position, Vector2 velocity, Vector2 acceleration, double radius, double mass,
              double restitution, double drag_scale, CollisionLayer collision_layer,
              CollisionLayer collision_mask, bool is_static,
              BoundsBehavior bounds_behavior) noexcept;

  Vector2 position_;
  Vector2 velocity_;
  Vector2 acceleration_;
  double radius_;
  double mass_;
  double restitution_;
  double drag_scale_;
  CollisionLayer collision_layer_;
  CollisionLayer collision_mask_;
  bool is_static_;
  BoundsBehavior bounds_behavior_;
};

// canonical: baseline_physics_predicate -- whether a body is the one the accepted **collision**
// equation describes.
//
// The accepted narrow phase of `docs/architecture/0003-deterministic-simulation-contract.md`
// § "Player-pair policy" is defined for equal-radius, equal-**unit-mass**, perfectly elastic discs.
// This is the predicate that says a body is one of those, and it is what keeps the general impulse
// row unreachable for ordinary blobs.
//
// **"Baseline" here means the defaults the narrow phase depends on, not every default the body
// carries**, and the distinction is a rule a fourth per-body property will face too. The only
// question this predicate answers is which collision equation phase 3 hands a pair to, so it reads
// exactly what a collision equation reads and nothing else: `resolve_player_pair_collision` and
// `resolve_general_pair_collision` between them consume mass and restitution, and neither consumes
// drag. `drag_scale` is therefore excluded on purpose. Including it would route a pair to the
// general impulse because one of the two bodies *coasts*, and the general impulse is deliberately
// **not** bit-identical to the baseline exchange -- so a unit-mass, perfectly elastic hazard that
// happens to declare `drag_scale = 0.0` would silently change the arithmetic of every contact it
// took part in, for a reason with nothing to do with contact. The rule for the next property is
// the same one: add it here only if a contact rule reads it.
//
// **Exact equality on purpose.** This is an identity test against a declared default, not a
// physical comparison, and ADR 0003 § "Floating-point contract" reserves exact equality for
// identity while giving tolerances only to physical quantities. A body one ULP away from
// `kDefaultMass` is a body the equal-unit-mass exchange is not written for, so it must take the
// general equation: the failure direction is always toward the more general rule and never toward
// applying the baseline to a body it does not describe.
[[nodiscard]] inline bool body_has_baseline_physics(const PhysicsBody& body) noexcept {
  return body.mass() == PhysicsBody::kDefaultMass &&
         body.restitution() == PhysicsBody::kDefaultRestitution;
}

// canonical: effective_radius -- the radius a phase measures this body with.
//
// `kUndeclaredRadius` is documented above as "a body that declares no size and defers to the
// configuration", and this is that sentence made executable. It matters because **an undeclared
// radius genuinely reaches the narrow phase**: `GameWorld::create(std::vector<EntitySeed>)` copies
// a seed's body verbatim, and only `GameWorld::create(configuration, map, seed, ...)`,
// `SpawnSystem`, and `ScenarioLoader` fill the field in at seating time. Every fixture and test
// world built from bare seeds therefore carries `0.0` into phase 3, so a rule that read
// `body.radius()` raw would turn a documented deferral into a silent contact distance of zero.
//
// Exact equality against the placeholder, for the same reason `body_has_baseline_physics` uses it:
// this is an identity test against a declared sentinel, not a physical comparison.
[[nodiscard]] inline double effective_radius(const PhysicsBody& body,
                                             const double configured_radius) noexcept {
  return body.radius() == PhysicsBody::kUndeclaredRadius ? configured_radius : body.radius();
}

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
