#include "chaser_controller.hpp"

#include "command_registry.hpp"
#include "commands/thrust_command.hpp"
#include "controller.hpp"
#include "controller_id.hpp"
#include "controllers_test_fixture.hpp"
#include "controllers_validation_error.hpp"
#include "entity_id.hpp"
#include "observation.hpp"
#include "simulation_limits.hpp"
#include "vector2.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace controllers = blob_royale::controllers;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

constexpr std::uint64_t kLeftController = simulation::kMinimumControllerId;
constexpr std::uint64_t kRightController = simulation::kMinimumControllerId + 1;
constexpr std::uint64_t kChaserController = simulation::kMinimumControllerId + 2;
constexpr std::uint64_t kUnseatedController = 900;

// Three markers: the left candidate, the right candidate, and the chaser between them. Sandbox
// seats ascending `ControllerId` at ascending marker index, so `kChaserController` takes the third.
[[nodiscard]] simulation::MapDefinition chase_map(const double left_x, const double right_x,
                                                  const double chaser_x) {
  return testing::controllers_map({simulation::Vector2::create(left_x, 320.0),
                                   simulation::Vector2::create(right_x, 320.0),
                                   simulation::Vector2::create(chaser_x, 320.0)},
                                  "chase_map");
}

[[nodiscard]] std::unique_ptr<controllers::Controller>
chaser(const controllers::ChaserController::Personality personality =
           controllers::ChaserController::Personality{}) {
  return controllers::ChaserController::create(simulation::ControllerId::create(kChaserController),
                                               0, personality);
}

[[nodiscard]] simulation::Vector2 sole_thrust(const std::vector<simulation::Command>& commands) {
  REQUIRE(commands.size() == 1);
  return std::get<simulation::ThrustCommand>(commands.front()).direction;
}

} // namespace

TEST_CASE("ChaserController asks for a body when it drives none", "[unit][controllers][chaser]") {
  const testing::ControllersFixture fixture(chase_map(100.0, 500.0, 300.0), 3);
  const std::unique_ptr<controllers::Controller> bot = controllers::ChaserController::create(
      simulation::ControllerId::create(kUnseatedController), 0);

  const std::vector<simulation::Command> decided =
      bot->decide(fixture.observation_for(kUnseatedController));

  REQUIRE(decided.size() == 1);
  CHECK(simulation::command_kind_of(decided.front()) == simulation::CommandKind::kSpawn);
  CHECK(simulation::addressed_identity_of(decided.front()).ordering_key() == kUnseatedController);
}

TEST_CASE("ChaserController thrusts toward the nearest other controllable entity",
          "[unit][controllers][chaser]") {
  // Left candidate 200 world units away, right candidate 100 away.
  const testing::ControllersFixture fixture(chase_map(100.0, 400.0, 300.0), 3);
  const std::unique_ptr<controllers::Controller> bot = chaser();

  const simulation::Vector2 direction =
      sole_thrust(bot->decide(fixture.observation_for(kChaserController)));

  const auto* const typed = dynamic_cast<const controllers::ChaserController*>(bot.get());
  REQUIRE(typed != nullptr);
  CHECK(typed->target() == fixture.entity_of(kRightController));
  CHECK(direction.x() == Catch::Approx(1.0).margin(simulation::kScalarTolerance));
  CHECK(direction.y() == Catch::Approx(0.0).margin(simulation::kScalarTolerance));
}

TEST_CASE("ChaserController breaks a distance tie toward the lowest EntityId",
          "[unit][controllers][chaser]") {
  // Both candidates exactly 200 world units away, which is what a symmetric spawn arrangement
  // produces on the first tick of a match.
  const testing::ControllersFixture fixture(chase_map(100.0, 500.0, 300.0), 3);
  const std::unique_ptr<controllers::Controller> bot = chaser();
  const simulation::EntityId left = *fixture.entity_of(kLeftController);
  const simulation::EntityId right = *fixture.entity_of(kRightController);
  REQUIRE(left < right);

  const simulation::Vector2 direction =
      sole_thrust(bot->decide(fixture.observation_for(kChaserController)));

  const auto* const typed = dynamic_cast<const controllers::ChaserController*>(bot.get());
  REQUIRE(typed != nullptr);
  CHECK(typed->target() == left);
  // Toward the left candidate, which is the lowest EntityId and the oldest body.
  CHECK(direction.x() == Catch::Approx(-1.0).margin(simulation::kScalarTolerance));
}

TEST_CASE("ChaserController decides nothing when it is alone in the world",
          "[unit][controllers][chaser]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const std::unique_ptr<controllers::Controller> bot = controllers::ChaserController::create(
      simulation::ControllerId::create(simulation::kMinimumControllerId), 0);

  const std::vector<simulation::Command> decided =
      bot->decide(fixture.observation_for(simulation::kMinimumControllerId));

  // No target means no direction, and no direction means no command: the stored acceleration is
  // left exactly as it was.
  CHECK(decided.empty());
  const auto* const typed = dynamic_cast<const controllers::ChaserController*>(bot.get());
  REQUIRE(typed != nullptr);
  CHECK(typed->target() == std::nullopt);
}

TEST_CASE("ChaserController tunes its aggression weight without a second type",
          "[unit][controllers][chaser]") {
  const testing::ControllersFixture fixture(chase_map(100.0, 400.0, 300.0), 3);

  // One class, two personalities, two behaviors.
  const std::unique_ptr<controllers::Controller> relentless =
      chaser(controllers::ChaserController::Personality{.aggression_weight = 1.0});
  const std::unique_ptr<controllers::Controller> timid =
      chaser(controllers::ChaserController::Personality{.aggression_weight = 0.25});

  const simulation::Vector2 relentless_direction =
      sole_thrust(relentless->decide(fixture.observation_for(kChaserController)));
  const simulation::Vector2 timid_direction =
      sole_thrust(timid->decide(fixture.observation_for(kChaserController)));

  CHECK(relentless_direction.x() == Catch::Approx(1.0).margin(simulation::kScalarTolerance));
  CHECK(timid_direction.x() == Catch::Approx(0.25).margin(simulation::kScalarTolerance));
  // Same target, same heading, a quarter of the commitment.
  CHECK(dynamic_cast<const controllers::ChaserController*>(timid.get())->target() ==
        dynamic_cast<const controllers::ChaserController*>(relentless.get())->target());
}

TEST_CASE("ChaserController never produces a thrust component the sink would refuse",
          "[unit][controllers][chaser]") {
  const testing::ControllersFixture fixture(chase_map(100.0, 400.0, 300.0), 3);
  const std::unique_ptr<controllers::Controller> bot = chaser();

  const simulation::Vector2 direction =
      sole_thrust(bot->decide(fixture.observation_for(kChaserController)));

  CHECK(std::abs(direction.x()) <= simulation::kMaximumThrustDirectionComponentMagnitude);
  CHECK(std::abs(direction.y()) <= simulation::kMaximumThrustDirectionComponentMagnitude);
}

TEST_CASE("ChaserController refuses an aggression weight outside the accepted range",
          "[unit][controllers][chaser]") {
  const simulation::ControllerId identity = simulation::ControllerId::create(kChaserController);

  try {
    static_cast<void>(controllers::ChaserController::create(
        identity, 0, controllers::ChaserController::Personality{.aggression_weight = 1.5}));
    FAIL("a chaser must refuse an aggression weight above the accepted maximum");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() ==
          controllers::ControllersValidationCode::kChaserAggressionWeightOutOfRange);
    CHECK(error.code() == "CONTROLLERS.CHASER_AGGRESSION_WEIGHT_OUT_OF_RANGE");
  }

  try {
    static_cast<void>(controllers::ChaserController::create(
        identity, 0,
        controllers::ChaserController::Personality{.aggression_weight =
                                                       std::numeric_limits<double>::quiet_NaN()}));
    FAIL("a chaser must refuse a non-finite aggression weight");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() ==
          controllers::ControllersValidationCode::kChaserAggressionWeightNotFinite);
  }
}
