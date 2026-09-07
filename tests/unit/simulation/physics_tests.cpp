#include "fixed_delta.hpp"
#include "physics.hpp"
#include "physics_body.hpp"
#include "simulation_limits.hpp"
#include "simulation_tolerance.hpp"
#include "simulation_validation_error.hpp"
#include "vector2.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <limits>
#include <type_traits>

namespace simulation = blob_royale::simulation;

namespace {

constexpr double kWorldWidth = 100.0;
constexpr double kWorldHeight = 80.0;
constexpr double kPlayerRadius = 10.0;

[[nodiscard]] simulation::Vector2 vector(const double x, const double y) {
  return simulation::Vector2::create(x, y);
}

[[nodiscard]] simulation::PhysicsBody body(const double position_x, const double position_y,
                                           const double velocity_x, const double velocity_y,
                                           const double acceleration_x = 0.0,
                                           const double acceleration_y = 0.0) {
  return simulation::PhysicsBody::create(vector(position_x, position_y),
                                         vector(velocity_x, velocity_y),
                                         vector(acceleration_x, acceleration_y));
}

void check_vector(const simulation::Vector2& actual, const double expected_x,
                  const double expected_y,
                  const double absolute_tolerance = simulation::kScalarTolerance) {
  CHECK(
      actual.x() ==
      Catch::Approx(expected_x).margin(absolute_tolerance).epsilon(simulation::kRelativeTolerance));
  CHECK(
      actual.y() ==
      Catch::Approx(expected_y).margin(absolute_tolerance).epsilon(simulation::kRelativeTolerance));
}

[[nodiscard]] double kinetic_energy_without_half(const simulation::Vector2& velocity) {
  return (velocity.x() * velocity.x()) + (velocity.y() * velocity.y());
}

// One body with a declared mass and restitution. Both are named at every call site of the general
// rule's tests, because a test whose expected numbers depend on a default it does not state is a
// test that silently changes meaning when the default does.
[[nodiscard]] simulation::PhysicsBody
variable_body(const double position_x, const double position_y, const double velocity_x,
              const double velocity_y, const double mass, const double restitution) {
  return body(position_x, position_y, velocity_x, velocity_y)
      .with_mass(mass)
      .with_restitution(restitution);
}

// Pair momentum, which the general impulse conserves for every mass ratio and every restitution:
// the two bodies receive equal and opposite impulses whatever their masses.
[[nodiscard]] simulation::Vector2 pair_momentum(const simulation::PhysicsBody& first,
                                                const simulation::Vector2& first_velocity,
                                                const simulation::PhysicsBody& second,
                                                const simulation::Vector2& second_velocity) {
  return (first.mass() * first_velocity) + (second.mass() * second_velocity);
}

} // namespace

TEST_CASE("FixedDelta exposes only the exact deterministic simulation quantum",
          "[unit][simulation][physics][fixed_delta]") {
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<simulation::FixedDelta>);

  const simulation::FixedDelta fixed_delta = simulation::FixedDelta::canonical();
  CHECK(fixed_delta.duration() == std::chrono::nanoseconds{2'500'000});
  CHECK(fixed_delta.seconds() == 1.0 / 400.0);
}

TEST_CASE("stored acceleration updates velocity before fixed-step position integration",
          "[unit][simulation][physics][integration]") {
  const simulation::PhysicsBody original = body(50.0, 50.0, 4.0, -8.0, 400.0, 800.0);
  const simulation::FixedDelta fixed_delta = simulation::FixedDelta::canonical();

  const simulation::Vector2 accelerated_velocity = simulation::integrate_accelerated_velocity(
      original.velocity(), original.acceleration(), fixed_delta);
  const simulation::Vector2 displacement = accelerated_velocity * fixed_delta.seconds();
  const simulation::Vector2 integrated_position =
      simulation::integrate_position(original.position(), displacement);

  check_vector(accelerated_velocity, 5.0, -6.0);
  check_vector(integrated_position, 50.0125, 49.985);
  check_vector(original.acceleration(), 400.0, 800.0);
}

TEST_CASE("touching head-on players exchange their normal velocities",
          "[unit][simulation][physics][collision]") {
  const simulation::PlayerPairCollisionResult result = simulation::resolve_player_pair_collision(
      body(30.0, 40.0, 5.0, 0.0), body(50.0, 40.0, -3.0, 0.0), kPlayerRadius);

  REQUIRE(result.contact().is_contact());
  REQUIRE(result.impulse_applied());
  check_vector(result.first_velocity(), -3.0, 0.0);
  check_vector(result.second_velocity(), 5.0, 0.0);
}

TEST_CASE("oblique collision exchanges normal components and preserves tangential components",
          "[unit][simulation][physics][collision]") {
  const simulation::PlayerPairCollisionResult result = simulation::resolve_player_pair_collision(
      body(30.0, 40.0, 2.0, 3.0), body(50.0, 40.0, -4.0, -5.0), kPlayerRadius);

  REQUIRE(result.impulse_applied());
  check_vector(result.first_velocity(), -4.0, 3.0);
  check_vector(result.second_velocity(), 2.0, -5.0);
}

TEST_CASE("overlapping players moving apart receive no collision impulse",
          "[unit][simulation][physics][collision]") {
  const simulation::PlayerPairCollisionResult result = simulation::resolve_player_pair_collision(
      body(30.0, 40.0, -1.0, 2.0), body(49.0, 40.0, 1.0, -3.0), kPlayerRadius);

  REQUIRE(result.contact().is_contact());
  CHECK_FALSE(result.impulse_applied());
  check_vector(result.first_velocity(), -1.0, 2.0);
  check_vector(result.second_velocity(), 1.0, -3.0);
}

TEST_CASE("coincident moving centers derive their normal from relative velocity",
          "[unit][simulation][physics][collision]") {
  const simulation::PlayerPairCollisionResult result = simulation::resolve_player_pair_collision(
      body(40.0, 40.0, 2.0, 3.0), body(40.0, 40.0, 2.0, -1.0), kPlayerRadius);

  REQUIRE(result.contact().is_contact());
  REQUIRE(result.impulse_applied());
  check_vector(result.contact().normal(), 0.0, 1.0);
  check_vector(result.first_velocity(), 2.0, -1.0);
  check_vector(result.second_velocity(), 2.0, 3.0);
}

TEST_CASE("coincident stationary centers use the deterministic fallback without an impulse",
          "[unit][simulation][physics][collision]") {
  const simulation::PlayerPairCollisionResult result = simulation::resolve_player_pair_collision(
      body(40.0, 40.0, 3.0, -4.0), body(40.0, 40.0, 3.0, -4.0), kPlayerRadius);

  REQUIRE(result.contact().is_contact());
  CHECK_FALSE(result.impulse_applied());
  check_vector(result.contact().normal(), 1.0, 0.0);
  check_vector(result.first_velocity(), 3.0, -4.0);
  check_vector(result.second_velocity(), 3.0, -4.0);
}

TEST_CASE("contact detection includes the position tolerance and rejects clear separation",
          "[unit][simulation][physics][contact]") {
  const simulation::PhysicsBody first = body(0.0, 40.0, 1.0, 0.0);
  const simulation::PlayerPairContact exact_contact =
      simulation::detect_player_pair_contact(first, body(20.0, 40.0, -1.0, 0.0), kPlayerRadius);
  const simulation::PlayerPairContact tolerance_contact = simulation::detect_player_pair_contact(
      first, body(20.0 + simulation::kPositionTolerance, 40.0, -1.0, 0.0), kPlayerRadius);
  const simulation::PlayerPairContact separated = simulation::detect_player_pair_contact(
      first, body(20.0 + (10.0 * simulation::kPositionTolerance), 40.0, -1.0, 0.0), kPlayerRadius);

  CHECK(exact_contact.is_contact());
  CHECK(tolerance_contact.is_contact());
  CHECK_FALSE(separated.is_contact());
}

TEST_CASE("relative speed at the velocity tolerance receives no impulse",
          "[unit][simulation][physics][contact]") {
  const double half_velocity_tolerance = simulation::kVelocityTolerance / 2.0;
  const simulation::PlayerPairCollisionResult result = simulation::resolve_player_pair_collision(
      body(30.0, 40.0, half_velocity_tolerance, 0.0),
      body(50.0, 40.0, -half_velocity_tolerance, 0.0), kPlayerRadius);

  REQUIRE(result.contact().is_contact());
  CHECK_FALSE(result.impulse_applied());
}

TEST_CASE("equal-unit-mass response conserves pair momentum and kinetic energy",
          "[unit][simulation][physics][conservation]") {
  const simulation::PhysicsBody first = body(30.0, 40.0, 2.0, 3.0);
  const simulation::PhysicsBody second = body(50.0, 40.0, -4.0, -5.0);
  const simulation::PlayerPairCollisionResult result =
      simulation::resolve_player_pair_collision(first, second, kPlayerRadius);

  const simulation::Vector2 initial_momentum = first.velocity() + second.velocity();
  const simulation::Vector2 final_momentum = result.first_velocity() + result.second_velocity();
  const double initial_energy = kinetic_energy_without_half(first.velocity()) +
                                kinetic_energy_without_half(second.velocity());
  const double final_energy = kinetic_energy_without_half(result.first_velocity()) +
                              kinetic_energy_without_half(result.second_velocity());

  check_vector(final_momentum, initial_momentum.x(), initial_momentum.y(),
               simulation::kScalarTolerance);
  CHECK(final_energy == Catch::Approx(initial_energy)
                            .margin(simulation::kScalarTolerance)
                            .epsilon(simulation::kRelativeTolerance));
}

TEST_CASE("single-wall overshoot folds the full endpoint and reverses terminal velocity",
          "[unit][simulation][physics][wall]") {
  const simulation::WallMotionResult result = simulation::resolve_player_wall_motion(
      vector(89.0, 40.0), vector(600.0, 0.0), kWorldWidth, kWorldHeight, kPlayerRadius,
      simulation::FixedDelta::canonical());
  const simulation::Vector2 endpoint =
      simulation::integrate_position(vector(89.0, 40.0), result.displacement());

  check_vector(endpoint, 89.5, 40.0);
  check_vector(result.terminal_velocity(), -600.0, 0.0);
}

TEST_CASE("multi-wall overshoot uses triangular folding without iteration limits",
          "[unit][simulation][physics][wall]") {
  const simulation::WallMotionResult result = simulation::resolve_player_wall_motion(
      vector(50.0, 40.0), vector(148'000.0, 0.0), kWorldWidth, kWorldHeight, kPlayerRadius,
      simulation::FixedDelta::canonical());
  const simulation::Vector2 endpoint =
      simulation::integrate_position(vector(50.0, 40.0), result.displacement());

  check_vector(endpoint, 80.0, 40.0);
  check_vector(result.terminal_velocity(), -148'000.0, 0.0);
}

TEST_CASE("exact lower and upper wall endpoints always point inward",
          "[unit][simulation][physics][wall]") {
  const simulation::WallMotionResult lower_result = simulation::resolve_player_wall_motion(
      vector(11.0, 40.0), vector(-400.0, 0.0), kWorldWidth, kWorldHeight, kPlayerRadius,
      simulation::FixedDelta::canonical());
  const simulation::WallMotionResult upper_result = simulation::resolve_player_wall_motion(
      vector(89.0, 40.0), vector(400.0, 0.0), kWorldWidth, kWorldHeight, kPlayerRadius,
      simulation::FixedDelta::canonical());

  check_vector(simulation::integrate_position(vector(11.0, 40.0), lower_result.displacement()),
               10.0, 40.0);
  check_vector(lower_result.terminal_velocity(), 400.0, 0.0);
  check_vector(simulation::integrate_position(vector(89.0, 40.0), upper_result.displacement()),
               90.0, 40.0);
  check_vector(upper_result.terminal_velocity(), -400.0, 0.0);
}

TEST_CASE("wall tolerance snapping also assigns an inward terminal direction",
          "[unit][simulation][physics][wall]") {
  const double near_wall_displacement = 1.0 - (simulation::kPositionTolerance / 2.0);
  const double near_wall_speed =
      near_wall_displacement / simulation::FixedDelta::canonical().seconds();
  const simulation::WallMotionResult lower_result = simulation::resolve_player_wall_motion(
      vector(11.0, 40.0), vector(-near_wall_speed, 0.0), kWorldWidth, kWorldHeight, kPlayerRadius,
      simulation::FixedDelta::canonical());
  const simulation::WallMotionResult upper_result = simulation::resolve_player_wall_motion(
      vector(89.0, 40.0), vector(near_wall_speed, 0.0), kWorldWidth, kWorldHeight, kPlayerRadius,
      simulation::FixedDelta::canonical());

  check_vector(simulation::integrate_position(vector(11.0, 40.0), lower_result.displacement()),
               10.0, 40.0);
  CHECK(lower_result.terminal_velocity().x() > 0.0);
  check_vector(simulation::integrate_position(vector(89.0, 40.0), upper_result.displacement()),
               90.0, 40.0);
  CHECK(upper_result.terminal_velocity().x() < 0.0);
}

TEST_CASE("relative tolerance governs large-scale contact and wall snapping",
          "[unit][simulation][physics][tolerance]") {
  constexpr double radius = 100'000'000.0;
  constexpr double contact_excess = 0.0001;
  const simulation::PhysicsBody first = body(0.0, 0.0, 1.0, 0.0);
  const simulation::PhysicsBody second = body((2.0 * radius) + contact_excess, 0.0, -1.0, 0.0);

  const simulation::PlayerPairContact contact =
      simulation::detect_player_pair_contact(first, second, radius);
  CHECK(contact.is_contact());

  constexpr double world_extent = 1'000'000'000.0;
  constexpr double wall_radius = 10.0;
  constexpr double position = 500'000'000.0;
  constexpr double upper_wall = world_extent - wall_radius;
  constexpr double wall_offset = 0.0005;
  const double velocity =
      (upper_wall - wall_offset - position) / simulation::FixedDelta::canonical().seconds();
  const simulation::WallMotionResult wall_motion = simulation::resolve_player_wall_motion(
      simulation::Vector2::create(position, position), simulation::Vector2::create(velocity, 0.0),
      world_extent, world_extent, wall_radius, simulation::FixedDelta::canonical());

  CHECK(simulation::integrate_position(simulation::Vector2::create(position, position),
                                       wall_motion.displacement())
            .x() == upper_wall);
  CHECK(wall_motion.terminal_velocity().x() < 0.0);
}

TEST_CASE("the accepted approximate comparison uses exact absolute and relative margins",
          "[unit][simulation][physics][tolerance]") {
  constexpr double reference = 1'000'000'000.0;
  CHECK(simulation::approximately_equal(reference, reference + 0.0005,
                                        simulation::kPositionTolerance));
  CHECK_FALSE(simulation::approximately_equal(reference, reference + 0.002,
                                              simulation::kPositionTolerance));
}

TEST_CASE("exact wall endpoints take precedence when both walls are within tolerance",
          "[unit][simulation][physics][wall][tiny_world]") {
  constexpr double radius = 1e-10;
  constexpr double world_extent = 3e-10;
  constexpr double upper_wall = world_extent - radius;
  const simulation::Vector2 position = simulation::Vector2::create(upper_wall, upper_wall);
  const simulation::WallMotionResult motion = simulation::resolve_player_wall_motion(
      position, simulation::Vector2::create(0.0, 0.0), world_extent, world_extent, radius,
      simulation::FixedDelta::canonical());

  CHECK(simulation::integrate_position(position, motion.displacement()) == position);
  CHECK(motion.terminal_velocity() == simulation::Vector2::create(0.0, 0.0));
}

TEST_CASE("a player starting on a wall and moving inward is not spuriously reflected",
          "[unit][simulation][physics][wall]") {
  const simulation::WallMotionResult result = simulation::resolve_player_wall_motion(
      vector(10.0, 40.0), vector(400.0, 0.0), kWorldWidth, kWorldHeight, kPlayerRadius,
      simulation::FixedDelta::canonical());
  const simulation::Vector2 endpoint =
      simulation::integrate_position(vector(10.0, 40.0), result.displacement());

  check_vector(endpoint, 11.0, 40.0);
  check_vector(result.terminal_velocity(), 400.0, 0.0);
}

TEST_CASE("simultaneous corner contact resolves both axes under x-before-y precedence",
          "[unit][simulation][physics][wall]") {
  const simulation::WallMotionResult result = simulation::resolve_player_wall_motion(
      vector(89.0, 69.0), vector(400.0, 400.0), kWorldWidth, kWorldHeight, kPlayerRadius,
      simulation::FixedDelta::canonical());
  const simulation::Vector2 endpoint =
      simulation::integrate_position(vector(89.0, 69.0), result.displacement());

  check_vector(endpoint, 90.0, 70.0);
  check_vector(result.terminal_velocity(), -400.0, -400.0);
}

TEST_CASE("finite high-speed wall motion remains in bounds and preserves axis speed",
          "[unit][simulation][physics][wall]") {
  const simulation::WallMotionResult result = simulation::resolve_player_wall_motion(
      vector(50.0, 40.0), vector(simulation::kMaximumPhysicalComponentMagnitude, 0.0), kWorldWidth,
      kWorldHeight, kPlayerRadius, simulation::FixedDelta::canonical());
  const simulation::Vector2 endpoint =
      simulation::integrate_position(vector(50.0, 40.0), result.displacement());

  CHECK(endpoint.x() >= kPlayerRadius);
  CHECK(endpoint.x() <= kWorldWidth - kPlayerRadius);
  CHECK(std::abs(result.terminal_velocity().x()) ==
        Catch::Approx(simulation::kMaximumPhysicalComponentMagnitude));
}

TEST_CASE("high-speed player crossing remains a discrete non-contact",
          "[unit][simulation][physics][collision]") {
  const simulation::PlayerPairCollisionResult result = simulation::resolve_player_pair_collision(
      body(20.0, 40.0, 100'000.0, 0.0), body(80.0, 40.0, -100'000.0, 0.0), 1.0);

  CHECK_FALSE(result.contact().is_contact());
  CHECK_FALSE(result.impulse_applied());
  check_vector(result.first_velocity(), 100'000.0, 0.0);
  check_vector(result.second_velocity(), -100'000.0, 0.0);
}

TEST_CASE("physics functions reject non-finite geometry and out-of-contract results",
          "[unit][simulation][physics][validation]") {
  const simulation::PhysicsBody first = body(30.0, 40.0, 1.0, 0.0);
  const simulation::PhysicsBody second = body(50.0, 40.0, -1.0, 0.0);

  CHECK_THROWS_AS(simulation::detect_player_pair_contact(first, second,
                                                         std::numeric_limits<double>::quiet_NaN()),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::resolve_player_pair_collision(first, second, 0.0),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::resolve_player_wall_motion(
                      first.position(), first.velocity(), std::numeric_limits<double>::infinity(),
                      kWorldHeight, kPlayerRadius, simulation::FixedDelta::canonical()),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::resolve_player_wall_motion(vector(5.0, 40.0), first.velocity(),
                                                         kWorldWidth, kWorldHeight, kPlayerRadius,
                                                         simulation::FixedDelta::canonical()),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::integrate_accelerated_velocity(
                      vector(simulation::kMaximumPhysicalComponentMagnitude, 0.0),
                      vector(simulation::kMaximumPhysicalComponentMagnitude, 0.0),
                      simulation::FixedDelta::canonical()),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(
      simulation::integrate_position(vector(simulation::kMaximumPhysicalComponentMagnitude, 0.0),
                                     vector(1.0, 0.0)),
      simulation::SimulationValidationError);
}

TEST_CASE("the pair restitution of two bodies is the smaller of the two",
          "[unit][simulation][physics][restitution]") {
  // Minimum, not product and not average. The property that decides it is the first check: a
  // perfectly inelastic body dampens the pair whatever its partner declares.
  CHECK(simulation::combined_restitution(0.0, 1.0) == 0.0);
  CHECK(simulation::combined_restitution(1.0, 0.0) == 0.0);
  // Idempotent, which `product` is not: two bodies at 0.9 stay at 0.9 rather than dropping to 0.81.
  CHECK(simulation::combined_restitution(0.9, 0.9) == 0.9);
  CHECK(simulation::combined_restitution(0.25, 0.75) == 0.25);
  // Exactly 1.0 for a baseline pair, which is what lets the general equation reduce to the accepted
  // one with no special case.
  CHECK(simulation::combined_restitution(simulation::PhysicsBody::kDefaultRestitution,
                                         simulation::PhysicsBody::kDefaultRestitution) == 1.0);
}

TEST_CASE("the general impulse with unit masses and restitution one agrees with the baseline",
          "[unit][simulation][physics][general_impulse][collision]") {
  // **Agreement, not bit identity.** The general equation forms `(v_b - v_a) . n` where the
  // baseline forms `(v_b . n) - (v_a . n)`, and it performs a `(1 + e)` product and three divisions
  // the baseline does not, so the two results differ in the last places even though they are the
  // same number to well inside `epsilon_velocity`. That is exactly why the `variable_impulse` row
  // is predicated on a body differing from the baseline: a pair of ordinary blobs must never reach
  // this function, or every accepted fixture horizon would move.
  const simulation::PhysicsBody first = body(30.0, 40.0, 2.0, 3.0);
  const simulation::PhysicsBody second = body(50.0, 40.0, -4.0, -5.0);
  REQUIRE(first.mass() == simulation::PhysicsBody::kDefaultMass);
  REQUIRE(first.restitution() == simulation::PhysicsBody::kDefaultRestitution);

  const simulation::PlayerPairCollisionResult accepted =
      simulation::resolve_player_pair_collision(first, second, kPlayerRadius);
  const simulation::PlayerPairCollisionResult general =
      simulation::resolve_general_pair_collision(first, second, kPlayerRadius);

  REQUIRE(accepted.impulse_applied());
  REQUIRE(general.impulse_applied());
  check_vector(general.first_velocity(), accepted.first_velocity().x(),
               accepted.first_velocity().y(), simulation::kVelocityTolerance);
  check_vector(general.second_velocity(), accepted.second_velocity().x(),
               accepted.second_velocity().y(), simulation::kVelocityTolerance);
  check_vector(pair_momentum(first, general.first_velocity(), second, general.second_velocity()),
               pair_momentum(first, first.velocity(), second, second.velocity()).x(),
               pair_momentum(first, first.velocity(), second, second.velocity()).y(),
               simulation::kScalarTolerance);
}

TEST_CASE("the general impulse barely deflects a heavy body struck by a light one",
          "[unit][simulation][physics][general_impulse][collision]") {
  // A 50:1 mass ratio, head-on and symmetric in speed. The heavy body's velocity change is
  // `1/50` of the light body's, because the two receive equal and opposite impulses, so it keeps
  // travelling essentially as it was while the light body is thrown back.
  const simulation::PhysicsBody heavy = variable_body(30.0, 40.0, 5.0, 0.0, 50.0, 1.0);
  const simulation::PhysicsBody light = variable_body(50.0, 40.0, -5.0, 0.0, 1.0, 1.0);

  const simulation::PlayerPairCollisionResult result =
      simulation::resolve_general_pair_collision(heavy, light, kPlayerRadius);

  REQUIRE(result.impulse_applied());
  // j = -(1 + 1) * (-10) / (1/50 + 1) = 20 / 1.02, so the heavy body sheds 20/51 and the light one
  // gains 1000/51.
  check_vector(result.first_velocity(), 5.0 - (20.0 / 51.0), 0.0, simulation::kVelocityTolerance);
  check_vector(result.second_velocity(), -5.0 + (1000.0 / 51.0), 0.0,
               simulation::kVelocityTolerance);
  // "Barely deflects" stated as a property rather than only as a number: the heavy body keeps its
  // direction and almost all of its speed, and it is deflected fifty times less than the light one.
  CHECK(result.first_velocity().x() > 4.5);
  const double heavy_change = std::abs(result.first_velocity().x() - heavy.velocity().x());
  const double light_change = std::abs(result.second_velocity().x() - light.velocity().x());
  CHECK(light_change == Catch::Approx(50.0 * heavy_change)
                            .margin(simulation::kVelocityTolerance)
                            .epsilon(simulation::kRelativeTolerance));
  check_vector(pair_momentum(heavy, result.first_velocity(), light, result.second_velocity()),
               pair_momentum(heavy, heavy.velocity(), light, light.velocity()).x(),
               pair_momentum(heavy, heavy.velocity(), light, light.velocity()).y(),
               simulation::kScalarTolerance);
}

TEST_CASE("restitution zero leaves the pair with one common normal velocity",
          "[unit][simulation][physics][general_impulse][collision]") {
  // A perfectly inelastic contact removes all normal closing speed: both bodies leave with the
  // pair's centre-of-mass normal velocity, which for these masses and speeds is `(3*4 + 1*(-4))/4`.
  const simulation::PhysicsBody first = variable_body(30.0, 40.0, 4.0, 1.0, 3.0, 0.0);
  const simulation::PhysicsBody second = variable_body(50.0, 40.0, -4.0, -2.0, 1.0, 1.0);

  const simulation::PlayerPairCollisionResult result =
      simulation::resolve_general_pair_collision(first, second, kPlayerRadius);

  REQUIRE(result.impulse_applied());
  // The pair restitution is `min(0, 1) = 0`, so one inelastic body dampens the pair even though the
  // other is perfectly elastic. That is the combination rule under test as much as the impulse is.
  const simulation::Vector2& normal = result.contact().normal();
  REQUIRE(normal == vector(1.0, 0.0));
  const double first_normal_speed = result.first_velocity().dot(normal);
  const double second_normal_speed = result.second_velocity().dot(normal);
  CHECK(first_normal_speed == Catch::Approx(2.0)
                                  .margin(simulation::kVelocityTolerance)
                                  .epsilon(simulation::kRelativeTolerance));
  CHECK(second_normal_speed == Catch::Approx(first_normal_speed)
                                   .margin(simulation::kVelocityTolerance)
                                   .epsilon(simulation::kRelativeTolerance));
  // Frictionless: the tangential components stay attached to their own bodies.
  CHECK(result.first_velocity().y() == 1.0);
  CHECK(result.second_velocity().y() == -2.0);
  check_vector(pair_momentum(first, result.first_velocity(), second, result.second_velocity()),
               pair_momentum(first, first.velocity(), second, second.velocity()).x(),
               pair_momentum(first, first.velocity(), second, second.velocity()).y(),
               simulation::kScalarTolerance);
}

TEST_CASE("the general impulse keeps the accepted detection, rejection, and normal fallback",
          "[unit][simulation][physics][general_impulse][contact]") {
  // Only the impulse is general. Everything that decides *whether* there is an impulse is the
  // accepted narrow phase, so these three cases must answer exactly as the baseline does.
  const simulation::PlayerPairCollisionResult separated =
      simulation::resolve_general_pair_collision(
          variable_body(0.0, 40.0, 1.0, 0.0, 4.0, 0.5),
          variable_body(20.0 + (10.0 * simulation::kPositionTolerance), 40.0, -1.0, 0.0, 4.0, 0.5),
          kPlayerRadius);
  CHECK_FALSE(separated.contact().is_contact());
  CHECK_FALSE(separated.impulse_applied());

  const simulation::PlayerPairCollisionResult separating =
      simulation::resolve_general_pair_collision(variable_body(30.0, 40.0, -1.0, 2.0, 9.0, 0.25),
                                                 variable_body(49.0, 40.0, 1.0, -3.0, 2.0, 0.75),
                                                 kPlayerRadius);
  REQUIRE(separating.contact().is_contact());
  CHECK_FALSE(separating.impulse_applied());
  check_vector(separating.first_velocity(), -1.0, 2.0);
  check_vector(separating.second_velocity(), 1.0, -3.0);

  const simulation::PlayerPairCollisionResult coincident_stationary =
      simulation::resolve_general_pair_collision(variable_body(40.0, 40.0, 3.0, -4.0, 6.0, 0.0),
                                                 variable_body(40.0, 40.0, 3.0, -4.0, 2.0, 0.0),
                                                 kPlayerRadius);
  REQUIRE(coincident_stationary.contact().is_contact());
  CHECK_FALSE(coincident_stationary.impulse_applied());
  check_vector(coincident_stationary.contact().normal(), 1.0, 0.0);
}

TEST_CASE("an unbounded motion crosses the wall the folding motion reflects off",
          "[unit][simulation][physics][unbounded]") {
  // The same position and velocity through both resolutions. The folding one turns around inside
  // the disc-centre interval; the unbounded one keeps going and keeps its velocity, which is the
  // whole difference a crossing body needs.
  const simulation::Vector2 position = vector(89.0, 40.0);
  const simulation::Vector2 velocity = vector(600.0, 0.0);

  const simulation::WallMotionResult folded =
      simulation::resolve_player_wall_motion(position, velocity, kWorldWidth, kWorldHeight,
                                             kPlayerRadius, simulation::FixedDelta::canonical());
  const simulation::WallMotionResult unbounded =
      simulation::resolve_unbounded_motion(velocity, simulation::FixedDelta::canonical());

  check_vector(simulation::integrate_position(position, folded.displacement()), 89.5, 40.0);
  check_vector(folded.terminal_velocity(), -600.0, 0.0);
  check_vector(simulation::integrate_position(position, unbounded.displacement()), 90.5, 40.0);
  check_vector(unbounded.terminal_velocity(), 600.0, 0.0);
}

TEST_CASE("an unbounded motion is the proposed displacement on both axes and never reflects",
          "[unit][simulation][physics][unbounded]") {
  // Multi-wall overshoot is where folding is most visible, so it is where "no fold at all" is
  // clearest: the endpoint is simply `position + velocity * dt`, far outside the arena.
  const simulation::Vector2 position = vector(50.0, 40.0);
  const simulation::Vector2 velocity = vector(148'000.0, -800.0);

  const simulation::WallMotionResult result =
      simulation::resolve_unbounded_motion(velocity, simulation::FixedDelta::canonical());

  check_vector(result.displacement(), 370.0, -2.0);
  check_vector(simulation::integrate_position(position, result.displacement()), 420.0, 38.0);
  check_vector(result.terminal_velocity(), 148'000.0, -800.0);
}

TEST_CASE("an unbounded body leaves the arena but never the representable world",
          "[unit][simulation][physics][unbounded][validation]") {
  // Removing the walls removes the arena bound and nothing else. The component limit every Vector2
  // obeys still holds the committed centre, and it is enforced where it always was -- at phase 5's
  // integration -- so a crossing body cannot travel to an unrepresentable position.
  const simulation::Vector2 position = vector(simulation::kMaximumPhysicalComponentMagnitude, 0.0);
  const simulation::Vector2 velocity = vector(simulation::kMaximumPhysicalComponentMagnitude, 0.0);
  const simulation::WallMotionResult motion =
      simulation::resolve_unbounded_motion(velocity, simulation::FixedDelta::canonical());

  CHECK(motion.displacement().x() > 0.0);
  CHECK_THROWS_AS(simulation::integrate_position(position, motion.displacement()),
                  simulation::SimulationValidationError);
}

TEST_CASE("the pair contact distance is the sum of the two bodies' own radii",
          "[unit][simulation][physics][contact][radius]") {
  // `r_a + r_b`, not `2 * player_radius`. A boulder of twenty-six and a blob of twelve touch at
  // thirty-eight, which is the edge a player can see rather than the edge one common radius names.
  const simulation::PhysicsBody boulder = body(0.0, 0.0, 0.0, 0.0).with_radius(26.0);
  const simulation::PhysicsBody blob = body(0.0, 0.0, 0.0, 0.0).with_radius(12.0);

  CHECK(simulation::pair_contact_distance(boulder, blob, kPlayerRadius) == 38.0);
  CHECK(simulation::pair_contact_distance(blob, boulder, kPlayerRadius) == 38.0);
}

TEST_CASE("a body that declares no radius defers to the configured one",
          "[unit][simulation][physics][contact][radius]") {
  // `kUndeclaredRadius` reaches the narrow phase for real: a world built from bare seeds never
  // fills the field in. The value already means "defers to the configuration", so this is that
  // sentence executed -- and reading the placeholder raw would have made the contact distance zero
  // and every such pair a silent non-collision.
  const simulation::PhysicsBody undeclared = body(0.0, 0.0, 0.0, 0.0);
  REQUIRE(undeclared.radius() == simulation::PhysicsBody::kUndeclaredRadius);
  const simulation::PhysicsBody declared = undeclared.with_radius(26.0);

  CHECK(simulation::effective_radius(undeclared, kPlayerRadius) == kPlayerRadius);
  CHECK(simulation::effective_radius(declared, kPlayerRadius) == 26.0);
  // Two baseline bodies produce exactly the accepted `2 * player_radius`: both `r + r` and `2 * r`
  // are exact in binary64, so nothing that measures with the configured radius measures differently
  // through this.
  CHECK(simulation::pair_contact_distance(undeclared, undeclared, kPlayerRadius) ==
        2.0 * kPlayerRadius);
  CHECK(simulation::pair_contact_distance(undeclared, declared, kPlayerRadius) ==
        kPlayerRadius + 26.0);

  // A nonsense declared radius fails the tick rather than silently never colliding.
  CHECK_THROWS_AS(
      simulation::pair_contact_distance(undeclared.with_radius(-1.0), declared, kPlayerRadius),
      simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::pair_contact_distance(undeclared, declared, 0.0),
                  simulation::SimulationValidationError);
}

TEST_CASE("the general impulse detects a large body's contact at its own radius",
          "[unit][simulation][physics][general_impulse][radius]") {
  // The separation is thirty, which is beyond the configured `2 * 10` and inside the boulder's own
  // `26 + 12`. Measuring at the configuration would call this a non-contact; measuring at the two
  // bodies' radii resolves it, which is the difference between a hazard that reads right and one a
  // player sinks into.
  const simulation::PhysicsBody boulder =
      body(0.0, 40.0, 5.0, 0.0).with_radius(26.0).with_mass(40.0);
  const simulation::PhysicsBody blob = body(30.0, 40.0, -1.0, 0.0).with_radius(12.0);

  const simulation::PlayerPairCollisionResult general =
      simulation::resolve_general_pair_collision(boulder, blob, kPlayerRadius);
  const simulation::PlayerPairCollisionResult at_configured_radius =
      simulation::resolve_player_pair_collision(boulder, blob, kPlayerRadius);

  CHECK(general.contact().is_contact());
  CHECK(general.impulse_applied());
  CHECK_FALSE(at_configured_radius.contact().is_contact());
  // The heavy boulder keeps almost all of its speed and the blob is thrown, both along +x.
  CHECK(general.first_velocity().x() > 4.5);
  CHECK(general.second_velocity().x() > 5.0);
  check_vector(pair_momentum(boulder, general.first_velocity(), blob, general.second_velocity()),
               pair_momentum(boulder, boulder.velocity(), blob, blob.velocity()).x(),
               pair_momentum(boulder, boulder.velocity(), blob, blob.velocity()).y(),
               simulation::kScalarTolerance);
}

TEST_CASE("the shared detection is the same one both entry points use",
          "[unit][simulation][physics][contact]") {
  // `detect_player_pair_contact` computes `2 * player_radius` and delegates, so the contact
  // predicate, the epsilon comparisons, and the coincident-centre fallback have one definition. The
  // equality here is exact, which is what "one implementation" has to mean.
  const simulation::PhysicsBody first = body(40.0, 40.0, 2.0, 3.0);
  const simulation::PhysicsBody second = body(40.0, 40.0, 2.0, -1.0);

  CHECK(simulation::detect_player_pair_contact(first, second, kPlayerRadius) ==
        simulation::detect_pair_contact(first, second, 2.0 * kPlayerRadius));
  CHECK_THROWS_AS(simulation::detect_pair_contact(first, second, 0.0),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(
      simulation::detect_pair_contact(first, second, std::numeric_limits<double>::quiet_NaN()),
      simulation::SimulationValidationError);
}
