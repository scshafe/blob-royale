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
