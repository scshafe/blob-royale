#ifndef BLOB_ROYALE_TESTS_UNIT_SIMULATION_CONTACT_IMPULSE_FROZEN_REFERENCE_HPP
#define BLOB_ROYALE_TESTS_UNIT_SIMULATION_CONTACT_IMPULSE_FROZEN_REFERENCE_HPP

#include "physics.hpp"
#include "simulation_validation_error.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace blob_royale::testing::contact_impulse_reference {

// Test-only frozen oracle from commit 638da1b:
//   src/simulation/physics.cpp, both resolve_*_pair_collision impulse bodies;
//   src/simulation/contact_rule_table.cpp, reflect_static_response velocity block;
//   src/simulation/simulation_tolerance.hpp, the separating comparison.
// Only result storage/names differ. No production impulse helper is called. Legacy contact
// detection is intentionally outside this oracle: the promotion changes equations' ownership,
// not detection. Phase A compares these frozen expressions to the still-original public
// functions before their bodies become delegates; later tests remain independent of delegates.
using simulation::PhysicsBody;
using simulation::PlayerPairContact;
using simulation::SimulationValidationCode;
using simulation::SimulationValidationError;
using simulation::Vector2;

inline constexpr double kVelocityTolerance = 1e-9;
inline constexpr double kRelativeTolerance = 1e-12;

[[nodiscard]] inline double normalize_signed_zero(const double value) noexcept {
  return value == 0.0 ? 0.0 : value;
}

[[nodiscard]] inline double require_finite_result(const double value,
                                                  const std::string_view context) {
  if (!std::isfinite(value)) {
    throw SimulationValidationError{SimulationValidationCode::kPhysicalScalarNotFinite,
                                    std::string{context}, "physics result must be finite"};
  }
  return normalize_signed_zero(value);
}

[[nodiscard]] inline double comparison_tolerance(const double first, const double second,
                                                 const double absolute_tolerance) noexcept {
  return absolute_tolerance + (kRelativeTolerance * std::max(std::abs(first), std::abs(second)));
}

[[nodiscard]] inline bool approximately_equal(const double first, const double second,
                                              const double absolute_tolerance) noexcept {
  return std::abs(first - second) <= comparison_tolerance(first, second, absolute_tolerance);
}

[[nodiscard]] inline bool
greater_than_or_approximately_equal(const double first, const double second,
                                    const double absolute_tolerance) noexcept {
  return first > second || approximately_equal(first, second, absolute_tolerance);
}

[[nodiscard]] inline double combined_restitution(const double first_restitution,
                                                 const double second_restitution) noexcept {
  return std::min(first_restitution, second_restitution);
}

struct FrozenCollisionResult final {
  PlayerPairContact contact_value;
  bool applied;
  Vector2 first;
  Vector2 second;

  [[nodiscard]] const PlayerPairContact& contact() const noexcept { return contact_value; }
  [[nodiscard]] bool impulse_applied() const noexcept { return applied; }
  [[nodiscard]] const Vector2& first_velocity() const noexcept { return first; }
  [[nodiscard]] const Vector2& second_velocity() const noexcept { return second; }
};

[[nodiscard]] inline FrozenCollisionResult
resolve_player_pair_collision(const PhysicsBody& first_body, const PhysicsBody& second_body,
                              const PlayerPairContact& contact) {
  if (!contact.is_contact() || greater_than_or_approximately_equal(contact.relative_normal_speed(),
                                                                   0.0, kVelocityTolerance)) {
    return FrozenCollisionResult{contact, false, first_body.velocity(), second_body.velocity()};
  }

  const double first_normal_speed =
      require_finite_result((first_body.velocity().x() * contact.normal().x()) +
                                (first_body.velocity().y() * contact.normal().y()),
                            "physics.resolve_player_pair_collision.first_normal_speed");
  const double second_normal_speed =
      require_finite_result((second_body.velocity().x() * contact.normal().x()) +
                                (second_body.velocity().y() * contact.normal().y()),
                            "physics.resolve_player_pair_collision.second_normal_speed");

  const double first_normal_delta =
      require_finite_result(second_normal_speed - first_normal_speed,
                            "physics.resolve_player_pair_collision.first_normal_delta");
  const double second_normal_delta =
      require_finite_result(first_normal_speed - second_normal_speed,
                            "physics.resolve_player_pair_collision.second_normal_delta");

  const Vector2 first_velocity = Vector2::create(
      require_finite_result(first_body.velocity().x() + (first_normal_delta * contact.normal().x()),
                            "physics.resolve_player_pair_collision.first_velocity.x"),
      require_finite_result(first_body.velocity().y() + (first_normal_delta * contact.normal().y()),
                            "physics.resolve_player_pair_collision.first_velocity.y"));
  const Vector2 second_velocity =
      Vector2::create(require_finite_result(
                          second_body.velocity().x() + (second_normal_delta * contact.normal().x()),
                          "physics.resolve_player_pair_collision.second_velocity.x"),
                      require_finite_result(
                          second_body.velocity().y() + (second_normal_delta * contact.normal().y()),
                          "physics.resolve_player_pair_collision.second_velocity.y"));

  return FrozenCollisionResult{contact, true, first_velocity, second_velocity};
}

[[nodiscard]] inline FrozenCollisionResult
resolve_general_pair_collision(const PhysicsBody& first_body, const PhysicsBody& second_body,
                               const PlayerPairContact& contact) {
  if (!contact.is_contact() || greater_than_or_approximately_equal(contact.relative_normal_speed(),
                                                                   0.0, kVelocityTolerance)) {
    return FrozenCollisionResult{contact, false, first_body.velocity(), second_body.velocity()};
  }

  // The written operation order, which
  // `docs/architecture/0003-deterministic-simulation-contract.md` § "Floating-point contract"
  // requires be preserved and which no reassociation or contraction may alter:
  //
  //   1. inverse_first_mass   = 1 / m_a
  //   2. inverse_second_mass  = 1 / m_b
  //   3. inverse_mass_sum     = inverse_first_mass + inverse_second_mass
  //   4. restitution          = min(e_a, e_b)
  //   5. impulse_numerator    = -((1 + restitution) * (v_rel . n))
  //   6. impulse_scalar       = impulse_numerator / inverse_mass_sum
  //   7. first_normal_delta   = impulse_scalar / m_a
  //      second_normal_delta  = impulse_scalar / m_b
  //   8. v_a'.x = v_a.x - (first_normal_delta * n.x), then v_a'.y likewise
  //      v_b'.x = v_b.x + (second_normal_delta * n.x), then v_b'.y likewise
  //
  // `(1 + restitution)` is formed before it multiplies the relative normal speed, and the negation
  // is applied to that whole product; the two per-body scalings divide the one impulse scalar by
  // each mass rather than multiplying by an already-rounded reciprocal, which is the equation as
  // written. `v_rel . n` is the contact's own `relative_normal_speed`, which is `(v_b - v_a) . n`
  // with `n` directed from a to b: an approaching pair makes it negative, so the impulse scalar is
  // positive and `b` is pushed along `+n` while `a` is pushed along `-n`.
  //
  // **Both masses are strictly positive because both bodies are dynamic, not because every
  // PhysicsBody has a positive mass.** A static body is deliberately permitted to carry zero, since
  // nothing reads a wall's mass. What rules a wall out here is the caller: `variable_impulse`
  // declares `body_is_variable_dynamic` and `body_is_dynamic` as its predicates, so both subjects
  // are dynamic, and `PhysicsBody` validation rejects a non-positive mass on a dynamic body. The
  // two reciprocals are therefore defined and their sum is strictly positive; the finiteness checks
  // still stand because a validly tiny mass can overflow its own reciprocal.
  const double inverse_first_mass = require_finite_result(
      1.0 / first_body.mass(), "physics.resolve_general_pair_collision.inverse_first_mass");
  const double inverse_second_mass = require_finite_result(
      1.0 / second_body.mass(), "physics.resolve_general_pair_collision.inverse_second_mass");
  const double inverse_mass_sum =
      require_finite_result(inverse_first_mass + inverse_second_mass,
                            "physics.resolve_general_pair_collision.inverse_mass_sum");
  const double restitution =
      combined_restitution(first_body.restitution(), second_body.restitution());
  const double impulse_numerator =
      require_finite_result(-((1.0 + restitution) * contact.relative_normal_speed()),
                            "physics.resolve_general_pair_collision.impulse_numerator");
  const double impulse_scalar =
      require_finite_result(impulse_numerator / inverse_mass_sum,
                            "physics.resolve_general_pair_collision.impulse_scalar");
  const double first_normal_delta =
      require_finite_result(impulse_scalar / first_body.mass(),
                            "physics.resolve_general_pair_collision.first_normal_delta");
  const double second_normal_delta =
      require_finite_result(impulse_scalar / second_body.mass(),
                            "physics.resolve_general_pair_collision.second_normal_delta");

  const Vector2 first_velocity = Vector2::create(
      require_finite_result(first_body.velocity().x() - (first_normal_delta * contact.normal().x()),
                            "physics.resolve_general_pair_collision.first_velocity.x"),
      require_finite_result(first_body.velocity().y() - (first_normal_delta * contact.normal().y()),
                            "physics.resolve_general_pair_collision.first_velocity.y"));
  const Vector2 second_velocity =
      Vector2::create(require_finite_result(
                          second_body.velocity().x() + (second_normal_delta * contact.normal().x()),
                          "physics.resolve_general_pair_collision.second_velocity.x"),
                      require_finite_result(
                          second_body.velocity().y() + (second_normal_delta * contact.normal().y()),
                          "physics.resolve_general_pair_collision.second_velocity.y"));

  return FrozenCollisionResult{contact, true, first_velocity, second_velocity};
}

[[nodiscard]] inline Vector2 reflect_static_contact_velocity(const Vector2& velocity,
                                                             const Vector2& normal) {
  const double normal_speed = (velocity.x() * normal.x()) + (velocity.y() * normal.y());
  const Vector2 reflected_velocity =
      Vector2::create(velocity.x() - (2.0 * normal_speed * normal.x()),
                      velocity.y() - (2.0 * normal_speed * normal.y()));
  return reflected_velocity;
}

} // namespace blob_royale::testing::contact_impulse_reference

#endif
