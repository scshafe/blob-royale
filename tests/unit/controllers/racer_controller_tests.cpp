#include "racer_controller.hpp"

#include "command_registry.hpp"
#include "commands/thrust_command.hpp"
#include "components/respawn_timer_component.hpp"
#include "controllers_validation_error.hpp"
#include "fixtures/racer_observation_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace controllers = blob_royale::controllers;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

// Classic pyramid lens: deterministic snapshot-to-command decisions. Intent comes from
// ADR 0007 "Bots"; body/progress lifecycles from "Progress and finishing" and "Returning".
// Geometry and expected headings are independent literals, not the controller's projection.
[[nodiscard]] std::unique_ptr<controllers::Controller>
racer(const std::uint64_t seed = 0,
      const controllers::RacerController::Personality personality = {}) {
  return controllers::RacerController::create(simulation::ControllerId::create(1), seed,
                                              personality);
}

[[nodiscard]] const controllers::RacerController&
typed(const std::unique_ptr<controllers::Controller>& bot) {
  const auto* const value = dynamic_cast<const controllers::RacerController*>(bot.get());
  REQUIRE(value != nullptr);
  return *value;
}

[[nodiscard]] simulation::Vector2 sole_thrust(const std::vector<simulation::Command>& commands,
                                              const std::uint64_t entity = 1) {
  REQUIRE(commands.size() == 1);
  REQUIRE(std::holds_alternative<simulation::ThrustCommand>(commands.front()));
  const simulation::ThrustCommand& thrust = std::get<simulation::ThrustCommand>(commands.front());
  CHECK(simulation::addressed_identity_of(commands.front()).ordering_key() == entity);
  return thrust.direction;
}

void check_direction(const simulation::Vector2 actual, const double x, const double y) {
  CHECK(actual.x() == Catch::Approx(x).margin(simulation::kScalarTolerance));
  CHECK(actual.y() == Catch::Approx(y).margin(simulation::kScalarTolerance));
}

} // namespace

TEST_CASE("RacerController requests a body only when its entity is absent",
          "[unit][controllers][racer]") {
  simulation::GameWorld world =
      testing::racer_observation_world(simulation::Vector2::create(200.0, 320.0));
  world.destroy_entity(simulation::EntityId::create(1));
  const controllers::Observation observation =
      testing::straight_racer_observation(std::move(world));
  const std::unique_ptr<controllers::Controller> bot = racer();

  const std::vector<simulation::Command> commands = bot->decide(observation);

  REQUIRE(commands.size() == 1);
  CHECK(simulation::command_kind_of(commands.front()) == simulation::CommandKind::kSpawn);
  CHECK(simulation::addressed_identity_of(commands.front()).ordering_key() == 1);
  CHECK(bot->decide(observation).empty());
}

TEST_CASE("RacerController waits through respawn without requesting a duplicate entity",
          "[unit][controllers][racer]") {
  simulation::GameWorld world =
      testing::racer_observation_world(simulation::Vector2::create(200.0, 320.0), 1);
  world.mutable_store<simulation::PhysicsBody>().erase(simulation::EntityId::create(1));
  SECTION("the timer is still counting") {
    world.mutable_store<simulation::RespawnTimer>().insert_or_assign(
        simulation::EntityId::create(1), simulation::RespawnTimer{100});
  }
  SECTION("the timer expired but the checkpoint is still occupied") {}
  const std::unique_ptr<controllers::Controller> bot = racer();

  CHECK(bot->decide(testing::straight_racer_observation(std::move(world))).empty());
  CHECK(bot->entity() == simulation::EntityId::create(1));
  CHECK_FALSE(bot->last_spawn_request_tick().has_value());
}

TEST_CASE("RacerController waits in the lobby before RaceProgress exists",
          "[unit][controllers][racer]") {
  simulation::GameWorld world =
      testing::racer_observation_world(simulation::Vector2::create(200.0, 320.0));
  world.mutable_match().phase = simulation::MatchPhase::kLobby;
  world.mutable_store<simulation::RaceProgress>().erase(simulation::EntityId::create(1));

  CHECK(racer()->decide(testing::straight_racer_observation(std::move(world))).empty());
}

TEST_CASE("RacerController decides nothing when the snapshot publishes another mode",
          "[unit][controllers][racer]") {
  simulation::GameWorld world =
      testing::racer_observation_world(simulation::Vector2::create(200.0, 320.0));
  world.mutable_match().mode_state = simulation::NoModeState{};

  CHECK(racer()->decide(testing::straight_racer_observation(std::move(world))).empty());
}

TEST_CASE("RacerController drives toward its next gate while centred",
          "[unit][controllers][racer]") {
  const controllers::Observation observation = testing::straight_racer_observation(
      testing::racer_observation_world(simulation::Vector2::create(200.0, 320.0)));

  check_direction(sole_thrust(racer()->decide(observation)), 1.0, 0.0);
}

TEST_CASE("RacerController follows progress instead of returning to a nearer earlier gate",
          "[unit][controllers][racer]") {
  // Gate zero is 100 wu behind, gate one is 200 wu ahead: nearest-gate selection points left.
  const controllers::Observation observation = testing::straight_racer_observation(
      testing::racer_observation_world(simulation::Vector2::create(400.0, 320.0), 1));

  check_direction(sole_thrust(racer()->decide(observation)), 1.0, 0.0);
}

TEST_CASE("RacerController reads its own body and progress after another racer in store order",
          "[unit][controllers][racer]") {
  simulation::GameWorld world = testing::race_test_world(
      {simulation::Vector2::create(700.0, 320.0), simulation::Vector2::create(200.0, 320.0)});
  world.mutable_match().mode_state = testing::straight_racer_course();
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(simulation::EntityId::create(1),
                                                                   simulation::RaceProgress{2});
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(simulation::EntityId::create(2),
                                                                   simulation::RaceProgress{0});
  const controllers::Observation observation =
      testing::straight_racer_observation(std::move(world), 2);
  const std::unique_ptr<controllers::Controller> bot =
      controllers::RacerController::create(simulation::ControllerId::create(2), 0);

  // The first body is past the finish and its progress says finished. Either wrong join would
  // produce leftward or zero thrust; the second racer must seek its own first gate to the right.
  check_direction(sole_thrust(bot->decide(observation), 2), 1.0, 0.0);
}

TEST_CASE("RacerController seeks the gate at the inclusive caution boundary",
          "[unit][controllers][racer]") {
  // Half-width 80 * default caution 0.75 = 60. The gate offset is (100,-60).
  const controllers::Observation observation = testing::straight_racer_observation(
      testing::racer_observation_world(simulation::Vector2::create(200.0, 380.0)));

  check_direction(sole_thrust(racer()->decide(observation)), 5.0 / std::sqrt(34.0),
                  -3.0 / std::sqrt(34.0));
}

TEST_CASE("RacerController turns toward the centreline strictly past its caution fraction",
          "[unit][controllers][racer]") {
  const controllers::Observation observation = testing::straight_racer_observation(
      testing::racer_observation_world(simulation::Vector2::create(200.0, 381.0)));

  check_direction(sole_thrust(racer()->decide(observation)), 0.0, -1.0);
}

TEST_CASE("RacerController personality changes the recovery threshold",
          "[unit][controllers][racer]") {
  const controllers::Observation observation = testing::straight_racer_observation(
      testing::racer_observation_world(simulation::Vector2::create(200.0, 361.0)));
  const std::unique_ptr<controllers::Controller> bot =
      racer(0, controllers::RacerController::Personality{.caution_fraction = 0.5});

  check_direction(sole_thrust(bot->decide(observation)), 0.0, -1.0);
}

TEST_CASE("RacerController recovers to the nearest leg of a bend", "[unit][controllers][racer]") {
  const controllers::Observation observation = testing::racer_observation(
      testing::racer_observation_world(simulation::Vector2::create(550.0, 440.0), 1,
                                       testing::bent_racer_course()),
      testing::bent_racer_terrain());
  const std::unique_ptr<controllers::Controller> bot =
      racer(0, controllers::RacerController::Personality{.caution_fraction = 0.5});

  check_direction(sole_thrust(bot->decide(observation)), -1.0, 0.0);
}

TEST_CASE("RacerController chooses the first declared segment at an equal-distance bend",
          "[unit][controllers][racer]") {
  const controllers::Observation observation = testing::racer_observation(
      testing::racer_observation_world(simulation::Vector2::create(450.0, 370.0), 1,
                                       testing::bent_racer_course()),
      testing::bent_racer_terrain());
  const std::unique_ptr<controllers::Controller> bot =
      racer(0, controllers::RacerController::Personality{.caution_fraction = 0.5});

  check_direction(sole_thrust(bot->decide(observation)), 0.0, -1.0);
}

TEST_CASE("RacerController clamps recovery targets at both centreline endpoints",
          "[unit][controllers][racer]") {
  SECTION("before the first node") {
    const controllers::Observation observation = testing::straight_racer_observation(
        testing::racer_observation_world(simulation::Vector2::create(60.0, 370.0)));
    check_direction(sole_thrust(racer()->decide(observation)), 4.0 / std::sqrt(41.0),
                    -5.0 / std::sqrt(41.0));
  }
  SECTION("past the final node") {
    const controllers::Observation observation = testing::straight_racer_observation(
        testing::racer_observation_world(simulation::Vector2::create(840.0, 370.0), 1));
    check_direction(sole_thrust(racer()->decide(observation)), -4.0 / std::sqrt(41.0),
                    -5.0 / std::sqrt(41.0));
  }
}

TEST_CASE("RacerController resolves the selected named corridor independently of declaration order",
          "[unit][controllers][racer][terrain]") {
  for (const bool selected_first : {false, true}) {
    DYNAMIC_SECTION("selected_first=" << selected_first) {
      const auto observation = testing::racer_observation(
          testing::racer_observation_world(simulation::Vector2::create(200.0, 370.0), 0,
                                           testing::alternate_racer_course()),
          testing::alternate_racer_terrain(selected_first));
      check_direction(sole_thrust(racer()->decide(observation)), 2.0 / std::sqrt(5.0),
                      -1.0 / std::sqrt(5.0));
    }
  }
}

TEST_CASE("RacerController rejects missing road bindings without selecting another corridor",
          "[unit][controllers][racer][terrain]") {
  const auto world = testing::racer_observation_world(simulation::Vector2::create(200.0, 370.0), 0,
                                                      testing::alternate_racer_course());
  auto terrain = testing::straight_racer_terrain();
  SECTION("a different road exists") {}
  SECTION("solid ground has no corridors") {
    terrain = simulation::TerrainDefinition::solid(terrain.bounds());
  }
  const auto observation = testing::racer_observation(world, std::move(terrain));
  try {
    static_cast<void>(racer()->decide(observation));
    FAIL("a missing explicit binding must not produce a plausible steering command");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() == controllers::ControllersValidationCode::kRacerCourseInvalid);
    CHECK(error.code() == "CONTROLLERS.RACER_COURSE_INVALID");
    CHECK(error.context() == "racer_controller.observation.road");
  }
}

TEST_CASE("RacerController writes zero thrust at the next gate centre",
          "[unit][controllers][racer]") {
  const controllers::Observation observation = testing::straight_racer_observation(
      testing::racer_observation_world(simulation::Vector2::create(300.0, 320.0)));

  check_direction(sole_thrust(racer()->decide(observation)), 0.0, 0.0);
}

TEST_CASE("RacerController writes zero thrust after finishing even near the road edge",
          "[unit][controllers][racer]") {
  const controllers::Observation observation = testing::straight_racer_observation(
      testing::racer_observation_world(simulation::Vector2::create(600.0, 381.0), 2));

  check_direction(sole_thrust(racer()->decide(observation)), 0.0, 0.0);
}

TEST_CASE("RacerController retains its seed and repeats deterministic recovery without jitter",
          "[unit][controllers][racer]") {
  const controllers::Observation observation = testing::straight_racer_observation(
      testing::racer_observation_world(simulation::Vector2::create(200.0, 381.0)));
  const std::unique_ptr<controllers::Controller> first = racer(7);
  const std::unique_ptr<controllers::Controller> same_seed = racer(7);
  const std::unique_ptr<controllers::Controller> other_seed = racer(19);
  const std::vector<simulation::Command> expected = first->decide(observation);

  CHECK(typed(first).seed() == 7);
  CHECK(typed(other_seed).seed() == 19);
  CHECK(same_seed->decide(observation) == expected);
  CHECK(other_seed->decide(observation) == expected);
  CHECK(first->decide(observation) == expected);
}

TEST_CASE("RacerController refuses non-finite caution fractions with the named error",
          "[unit][controllers][racer]") {
  for (const double fraction :
       {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()}) {
    try {
      static_cast<void>(
          racer(0, controllers::RacerController::Personality{.caution_fraction = fraction}));
      FAIL("a racer must reject a non-finite caution fraction");
    } catch (const controllers::ControllersValidationError& error) {
      CHECK(error.validation_code() ==
            controllers::ControllersValidationCode::kRacerCautionFractionNotFinite);
      CHECK(error.code() == "CONTROLLERS.RACER_CAUTION_FRACTION_NOT_FINITE");
      CHECK(error.context() == "racer_controller.personality.caution_fraction");
    }
  }
}

TEST_CASE("RacerController accepts positive caution fractions up to one and rejects the rest",
          "[unit][controllers][racer]") {
  for (const double fraction : {0.0, -0.25, 1.01}) {
    try {
      static_cast<void>(
          racer(0, controllers::RacerController::Personality{.caution_fraction = fraction}));
      FAIL("a racer must reject a caution fraction outside (0,1]");
    } catch (const controllers::ControllersValidationError& error) {
      CHECK(error.validation_code() ==
            controllers::ControllersValidationCode::kRacerCautionFractionOutOfRange);
      CHECK(error.code() == "CONTROLLERS.RACER_CAUTION_FRACTION_OUT_OF_RANGE");
      CHECK(error.context() == "racer_controller.personality.caution_fraction");
    }
  }
  CHECK_NOTHROW(racer(0, controllers::RacerController::Personality{.caution_fraction = 1.0}));
  CHECK_NOTHROW(racer(0, controllers::RacerController::Personality{.caution_fraction = 0.01}));
}
