#include "shared/thrust_steering_system.hpp"

#include "gameplay_test_fixture.hpp"

#include "gameplay_validation_error.hpp"
#include "input_batch.hpp"
#include "physics_body.hpp"
#include "sandbox/sandbox_mode.hpp"
#include "simulation_limits.hpp"
#include "simulation_tolerance.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string_view>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

constexpr double kThrustMaximum = 400.0;

// Two seated entities in a sandbox simulation, so "moves the intended entity and only that one" is
// a question the world can answer. Entity 1 sits at (100, 320) and entity 2 at (200, 320).
[[nodiscard]] simulation::GameSimulation seated_pair() {
  simulation::GameSimulation game = testing::gameplay_simulation(
      gameplay::SandboxMode::create(kThrustMaximum), testing::gameplay_map(4));
  game.step(testing::kGameplayFixedDelta,
            testing::gameplay_batch(game, {testing::spawn_command(7), testing::spawn_command(8)}));
  return game;
}

} // namespace

TEST_CASE("steered_acceleration applies the magnitude clamp exactly once, as written",
          "[unit][gameplay][thrust_steering]") {
  // `(1, 1)`: m = sqrt(2), s = 1 / sqrt(2), each component is (1 * s) * thrust_max. Written as the
  // ADR writes it rather than as `thrust_max / sqrt(2)`, because those are different binary64
  // values and the operation order is the contract.
  const double diagonal_magnitude = std::sqrt((1.0 * 1.0) + (1.0 * 1.0));
  const double diagonal_scale = 1.0 / diagonal_magnitude;
  const simulation::Vector2 diagonal =
      gameplay::steered_acceleration(simulation::Vector2::create(1.0, 1.0), kThrustMaximum);
  CHECK(diagonal.x() == (1.0 * diagonal_scale) * kThrustMaximum);
  CHECK(diagonal.y() == (1.0 * diagonal_scale) * kThrustMaximum);

  // Clamping once means the result has magnitude thrust_max; clamping twice would scale by
  // 1/sqrt(2) again and land at thrust_max / 2. The magnitude is compared with ADR 0003's
  // absolute-plus-relative tolerance rather than for bit equality, because
  // `sqrt(((1/sqrt(2)) * 400)^2 * 2)` rounds to 399.99999999999994 and the exact-magnitude claim of
  // `docs/architecture/0005-royale-mode.md` § "Steering" is about the arithmetic, not about a
  // round trip through a second square root. The exact claim is the component equality above.
  const double diagonal_magnitude_result =
      std::sqrt((diagonal.x() * diagonal.x()) + (diagonal.y() * diagonal.y()));
  CHECK(simulation::approximately_equal(diagonal_magnitude_result, kThrustMaximum,
                                        simulation::kAccelerationTolerance));
  // The clamp is applied once and not twice, stated as the number a second clamp would produce.
  CHECK(diagonal.x() != ((1.0 * diagonal_scale) * diagonal_scale) * kThrustMaximum);
  CHECK(gameplay::steered_acceleration(diagonal, 1.0) != diagonal);

  // A direction inside the unit disc keeps its magnitude: s is exactly 1.0, and multiplying by 1.0
  // is the identity on every finite binary64 value, so this is (0.5 * 1.0) * 400 and not a
  // renormalization to full thrust.
  const simulation::Vector2 partial =
      gameplay::steered_acceleration(simulation::Vector2::create(0.5, 0.0), kThrustMaximum);
  CHECK(partial.x() == (0.5 * 1.0) * kThrustMaximum);
  CHECK(partial.y() == 0.0);

  // The unit axis is the boundary case m == 1, which takes the `s = 1` arm.
  const simulation::Vector2 axis =
      gameplay::steered_acceleration(simulation::Vector2::create(0.0, -1.0), kThrustMaximum);
  CHECK(axis.x() == 0.0);
  CHECK(axis.y() == -kThrustMaximum);

  // `(0, 0)` is the coast command and stores zero acceleration rather than dividing by zero.
  const simulation::Vector2 coast =
      gameplay::steered_acceleration(simulation::Vector2::create(0.0, 0.0), kThrustMaximum);
  CHECK(coast == simulation::Vector2::create(0.0, 0.0));
}

TEST_CASE("A thrust command accelerates the intended entity and only that one",
          "[unit][gameplay][thrust_steering]") {
  simulation::GameSimulation game = seated_pair();
  const simulation::Vector2 entity_two_position =
      testing::published_body(game.snapshot(), 2)->position();

  game.step(testing::kGameplayFixedDelta,
            testing::gameplay_batch(game, {testing::thrust_command(1, 1.0, 0.0)}));

  const simulation::WorldSnapshot snapshot = game.snapshot();
  const simulation::PhysicsBody steered = *testing::published_body(snapshot, 1);
  const simulation::PhysicsBody untouched = *testing::published_body(snapshot, 2);

  CHECK(steered.acceleration() == simulation::Vector2::create(kThrustMaximum, 0.0));
  CHECK(steered.velocity().x() == kThrustMaximum * testing::kGameplayFixedDelta.seconds());
  CHECK(steered.position().x() > 100.0);

  // The entity that was not addressed keeps the at-rest body the seating gave it: no acceleration,
  // no velocity, and the position it was seated at.
  CHECK(untouched.acceleration() == simulation::Vector2::create(0.0, 0.0));
  CHECK(untouched.velocity() == simulation::Vector2::create(0.0, 0.0));
  CHECK(untouched.position() == entity_two_position);
}

TEST_CASE("Stored acceleration persists until the next thrust for that entity",
          "[unit][gameplay][thrust_steering]") {
  simulation::GameSimulation game = seated_pair();

  game.step(testing::kGameplayFixedDelta,
            testing::gameplay_batch(game, {testing::thrust_command(1, 1.0, 0.0)}));
  game.step(testing::kGameplayFixedDelta, simulation::InputBatch::empty());

  // ADR 0003 stores acceleration and never decays it; a tick with no thrust re-integrates the same
  // acceleration rather than zeroing it.
  CHECK(testing::published_body(game.snapshot(), 1)->acceleration() ==
        simulation::Vector2::create(kThrustMaximum, 0.0));
  CHECK(testing::published_body(game.snapshot(), 1)->velocity().x() ==
        2.0 * (kThrustMaximum * testing::kGameplayFixedDelta.seconds()));

  // The coast command is how a controller stops accelerating, and it is a thrust like any other.
  game.step(testing::kGameplayFixedDelta,
            testing::gameplay_batch(game, {testing::thrust_command(1, 0.0, 0.0)}));
  CHECK(testing::published_body(game.snapshot(), 1)->acceleration() ==
        simulation::Vector2::create(0.0, 0.0));
}

TEST_CASE("A thrust naming an entity that owns no body is skipped rather than failing the tick",
          "[unit][gameplay][thrust_steering]") {
  simulation::GameSimulation game = testing::gameplay_simulation(
      gameplay::SandboxMode::create(kThrustMaximum), testing::gameplay_map(1));

  // Two joiners and one point: entity 2 is created carrying only its controller link and is
  // deferred, so this tick's thrust for it has nothing to write.
  game.step(testing::kGameplayFixedDelta,
            testing::gameplay_batch(game, {testing::spawn_command(7), testing::spawn_command(8)}));
  REQUIRE_FALSE(testing::published_body(game.snapshot(), 2).has_value());

  CHECK_NOTHROW(game.step(testing::kGameplayFixedDelta,
                          testing::gameplay_batch(game, {testing::thrust_command(2, 1.0, 0.0)})));
  CHECK(testing::published_player_count(game) == 1);
}

TEST_CASE("A thrust for an entity a mode did not seat cannot address a foreign body",
          "[unit][gameplay][thrust_steering]") {
  simulation::GameSimulation game = seated_pair();

  // A command naming an id no entity holds is ignored by the tick rather than failing it, because
  // the command source is a network session and one client must not be able to stop the match.
  CHECK_NOTHROW(game.step(testing::kGameplayFixedDelta,
                          testing::gameplay_batch(game, {testing::thrust_command(404, 1.0, 0.0)})));
  CHECK(testing::published_body(game.snapshot(), 1)->acceleration() ==
        simulation::Vector2::create(0.0, 0.0));
  CHECK(testing::published_body(game.snapshot(), 2)->acceleration() ==
        simulation::Vector2::create(0.0, 0.0));
}

TEST_CASE("ThrustSteeringSystem rejects a maximum it could not steer with",
          "[unit][gameplay][thrust_steering][validation]") {
  try {
    static_cast<void>(
        gameplay::ThrustSteeringSystem::create(std::numeric_limits<double>::quiet_NaN()));
    FAIL("a non-finite thrust maximum was accepted");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() == gameplay::GameplayValidationCode::kThrustMaximumNotFinite);
    CHECK(error.code() == std::string_view{"GAMEPLAY.THRUST_MAXIMUM_NOT_FINITE"});
  }

  try {
    static_cast<void>(gameplay::ThrustSteeringSystem::create(-0.5));
    FAIL("a negative thrust maximum was accepted");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() == gameplay::GameplayValidationCode::kThrustMaximumOutOfRange);
    CHECK(error.code() == std::string_view{"GAMEPLAY.THRUST_MAXIMUM_OUT_OF_RANGE"});
  }

  CHECK(gameplay::ThrustSteeringSystem::create(0.0)->name() == std::string_view{"thrust_steering"});
}
