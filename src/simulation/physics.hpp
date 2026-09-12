#ifndef BLOB_ROYALE_SIMULATION_PHYSICS_HPP
#define BLOB_ROYALE_SIMULATION_PHYSICS_HPP

#include "fixed_delta.hpp"
#include "physics_body.hpp"
#include "vector2.hpp"

namespace blob_royale::simulation {

namespace detail {
struct MotionContactAccess;
} // namespace detail

// canonical: deterministic_physics -- pure fixed-step integration, contact, and wall equations.
class PlayerPairContact;
class PlayerPairCollisionResult;
class WallMotionResult;

// Detects an equal-radius disc contact at committed positions. The radius must be finite and
// positive; invalid input or an invalid calculation throws SimulationValidationError.
[[nodiscard]] PlayerPairContact detect_player_pair_contact(const PhysicsBody& first_body,
                                                           const PhysicsBody& second_body,
                                                           double player_radius);

// The same detection against an explicit contact distance instead of one common radius. This is
// the one implementation: `detect_player_pair_contact` computes `2 * player_radius` and delegates
// here, so the contact predicate, the epsilon comparisons, and the coincident-centre normal
// fallback have exactly one definition and cannot drift between the accepted and general paths.
// The contact distance must be finite and greater than zero; an invalid calculation throws
// SimulationValidationError.
[[nodiscard]] PlayerPairContact detect_pair_contact(const PhysicsBody& first_body,
                                                    const PhysicsBody& second_body,
                                                    double contact_distance);

// canonical: pair_contact_distance -- the separation at which two discs touch.
//
// `r_a + r_b`, where a body that declares no radius contributes `configured_radius`. Two baseline
// bodies therefore produce `configured + configured`, which is bit-identical to the accepted
// `2 * player_radius`: both are exact in binary64, so nothing that measures with the configured
// radius today measures differently through this.
//
// The fallback is `effective_radius`'s, and the reason it is a fallback rather than a rejection is
// stated there: an undeclared radius really does reach the narrow phase, and the value already
// means "defers to the configuration". Rejecting it here would reject every world built from bare
// seeds, which is every accepted fixture's construction path.
//
// `configured_radius` and both effective radii must be finite, positive, and within the accepted
// physical component limit; anything else throws SimulationValidationError, so a body carrying a
// nonsense radius fails the tick instead of silently never colliding.
[[nodiscard]] double pair_contact_distance(const PhysicsBody& first_body,
                                           const PhysicsBody& second_body,
                                           double configured_radius);

// Resolves one equal-unit-mass frictionless pair in first/second order. Non-contacting and
// stationary-or-separating pairs retain their velocities. Invalid calculations throw
// SimulationValidationError.
[[nodiscard]] PlayerPairCollisionResult
resolve_player_pair_collision(const PhysicsBody& first_body, const PhysicsBody& second_body,
                              double player_radius);

// The identical accepted impulse over an already-certified contact, with no position detection.
// The contact must describe these bodies in first/second orientation at the hit. Continuous
// motion supplies that contact from its canonical root, not a second rounded overlap predicate.
// The contact/separating guard, written arithmetic, and failure diagnostics are unchanged.
[[nodiscard]] PlayerPairCollisionResult
resolve_player_pair_collision(const PhysicsBody& first_body, const PhysicsBody& second_body,
                              const PlayerPairContact& contact);

// canonical: pair_restitution -- how two bodies' restitutions combine into one pair restitution.
//
// **The rule is the minimum, and the choice is load-bearing.** Three rules are usual -- minimum,
// product, and average -- and minimum is taken here for three reasons.
//
//   * It is the only one under which a perfectly inelastic body dampens every contact it takes
//     part in. `min(0, e)` is `0` for every partner, so a designer who sets a body's bounciness to
//     zero gets a body that does not bounce off anything; under `average` that same body would
//     still leave a perfectly elastic partner at `e = 0.5`, which contradicts what the value says.
//   * It is idempotent, so two bodies declaring the same bounciness produce exactly that
//     bounciness. `product` does not: two bodies at `0.9` would combine to `0.81`, a pair less
//     bouncy than either body, which is a surprise a designer has to learn rather than read.
//   * It is closed on `[0, 1]` and returns exactly `1.0` for a pair of baseline bodies, so the
//     general equation reduces to the perfectly elastic accepted one with no special case.
//
// Both arguments are `PhysicsBody` restitutions, which validation has already put in `[0, 1]`, so
// neither can be NaN and the comparison is total.
[[nodiscard]] double combined_restitution(double first_restitution,
                                          double second_restitution) noexcept;

// Resolves one frictionless pair of arbitrary positive masses, arbitrary restitution, and
// arbitrary radii in first/second order, using the general impulse
//
//   `j = -(1 + e) * (v_rel . n) / (1/m_a + 1/m_b)`,  `v_a' = v_a - (j/m_a) n`,
//   `v_b' = v_b + (j/m_b) n`
//
// where `e` is `combined_restitution` of the two bodies. The stationary-or-separating rejection and
// the coincident-centre normal fallback are the accepted ones, and the detection is the same one
// implementation; what differs from `resolve_player_pair_collision` is the impulse and the contact
// distance, which is `pair_contact_distance` -- the sum of the two bodies' own radii -- rather than
// twice one common radius. `configured_radius` is therefore the fallback for a body that declares
// no size, not the size the pair is measured with: a hazard drawn at its own radius must collide at
// the edge a player can see, or the feature reads as broken however right the arithmetic is.
//
// **Preconditions this signature cannot carry.** Both bodies must be **dynamic**. A dynamic body's
// mass is strictly positive by `PhysicsBody` validation, and this function divides by both masses;
// a *static* body is permitted to carry zero mass, because nothing reads a wall's mass, so handing
// this function a wall is a division by zero. The one built-in caller cannot: `variable_impulse`'s
// predicates are `body_is_variable_dynamic` and `body_is_dynamic`, so both subjects are dynamic. A
// mode reusing this equation under its own row name must declare predicates that do the same.
//
// **This is not the accepted baseline and must never be used as one.** With unit masses,
// restitution 1, and the configured radius it agrees with `resolve_player_pair_collision` to well
// within `epsilon_velocity`, but it is *not* bit-identical: it forms `(v_b - v_a) . n` where the
// baseline forms `(v_b . n) - (v_a . n)`, and it adds a `(1 + e)` product and three divisions the
// baseline does not perform. That is precisely why the `variable_impulse` row is predicated on a
// body differing from the baseline -- a pair of ordinary blobs never reaches this function, and
// every accepted fixture horizon keeps its exact arithmetic
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Player-pair policy").
//
// Invalid input or an out-of-contract result throws SimulationValidationError.
[[nodiscard]] PlayerPairCollisionResult
resolve_general_pair_collision(const PhysicsBody& first_body, const PhysicsBody& second_body,
                               double configured_radius);

// The identical general impulse over a contact already certified for these two dynamic bodies.
// No radius or overlap calculation is repeated; all other preconditions above still apply.
[[nodiscard]] PlayerPairCollisionResult
resolve_general_pair_collision(const PhysicsBody& first_body, const PhysicsBody& second_body,
                               const PlayerPairContact& contact);

// The existing reflect_static row's velocity equation, in its original binary64 operation order.
// `normal` is the row-oriented unit contact normal. This equation does not classify contact or
// motion direction; the caller owns admission exactly as the existing row does.
[[nodiscard]] Vector2 reflect_static_contact_velocity(const Vector2& velocity,
                                                      const Vector2& normal);

// Resolves the complete fixed-step motion against an axis-aligned world, x before y. Geometry
// must be finite, positive, and contain the committed position; invalid input or output throws
// SimulationValidationError.
[[nodiscard]] WallMotionResult
resolve_player_wall_motion(const Vector2& position, const Vector2& velocity, double world_width,
                           double world_height, double player_radius, FixedDelta fixed_delta);

// Resolves the complete fixed-step motion of a body for which the arena has no walls: the
// displacement is the proposed `velocity * dt` unfolded and the terminal velocity is the velocity
// unchanged. This is the phase 4 result of a `BoundsBehavior::kCross` body, and it is the same
// `WallMotionResult` shape a folding body produces so that phase 5 integrates both identically.
//
// It takes no geometry, because a body with no walls has no geometry to resolve against -- which
// is also why a crossing body needs no arena at all. The one bound that still holds it is the
// component limit every `Vector2` obeys: `integrate_position` in phase 5 rejects a committed centre
// past `kMaximumPhysicalComponentMagnitude`, so a crossing body travels out of the arena but never
// out of the representable world. An out-of-contract result throws SimulationValidationError.
[[nodiscard]] WallMotionResult resolve_unbounded_motion(const Vector2& velocity,
                                                        FixedDelta fixed_delta);

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

  // Re-orients an existing certificate without classifying geometry or velocity again. Distance,
  // membership, and relative normal speed are orientation-invariant; only its normal is negated.
  [[nodiscard]] PlayerPairContact reversed() const;

  friend bool operator==(const PlayerPairContact&, const PlayerPairContact&) = default;

private:
  friend PlayerPairContact detect_player_pair_contact(const PhysicsBody&, const PhysicsBody&,
                                                      double);
  friend PlayerPairContact detect_pair_contact(const PhysicsBody&, const PhysicsBody&, double);
  // Defined only by the canonical continuous-motion implementation. This is not a public contact
  // factory: that owner certifies hit membership and validates its normal/distance/speed first.
  friend struct detail::MotionContactAccess;

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
  friend PlayerPairCollisionResult resolve_general_pair_collision(const PhysicsBody&,
                                                                  const PhysicsBody&, double);
  friend PlayerPairCollisionResult
  resolve_player_pair_collision(const PhysicsBody&, const PhysicsBody&, const PlayerPairContact&);
  friend PlayerPairCollisionResult
  resolve_general_pair_collision(const PhysicsBody&, const PhysicsBody&, const PlayerPairContact&);

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
  friend WallMotionResult resolve_unbounded_motion(const Vector2&, FixedDelta);

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
