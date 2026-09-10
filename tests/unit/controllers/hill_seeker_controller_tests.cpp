#include "hill_seeker_controller.hpp"

#include "../gameplay/gameplay_test_fixture.hpp"
#include "command_registry.hpp"
#include "commands/thrust_command.hpp"
#include "controller.hpp"
#include "controller_id.hpp"
#include "controllers_test_fixture.hpp"
#include "controllers_validation_error.hpp"
#include "entity_id.hpp"
#include "king_of_the_hill/hill_geometry.hpp"
#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "king_of_the_hill/king_of_the_hill_mode.hpp"
#include "map_definition.hpp"
#include "observation.hpp"
#include "simulation_limits.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace controllers = blob_royale::controllers;
namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

constexpr std::uint64_t kSeekerController = simulation::kMinimumControllerId;
constexpr std::uint64_t kUnseatedController = 900;

// One `hill` marker and one `spawn` marker on the arena's midline.
[[nodiscard]] simulation::MapDefinition hill_map(const double spawn_x, const double hill_x) {
  return simulation::MapDefinition::create(
      "hill_map", simulation::ArenaBounds::create(960.0, 640.0), {},
      {simulation::MapDefinition::Marker::create(std::string(gameplay::kHillMarkerKind),
                                                 simulation::Vector2::create(hill_x, 320.0),
                                                 std::nullopt, simulation::MapMetadata::none()),
       simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(spawn_x, 320.0))},
      simulation::MapMetadata::none());
}

// A king-of-the-hill world after its first tick: the hill exists from the first tick in every
// phase and the field is open, so one spawn command seats the seeker at the marker.
[[nodiscard]] testing::SteppedGame seated_hill_world(const double spawn_x, const double hill_x) {
  testing::SteppedGame stepped(testing::gameplay_simulation(gameplay::KingOfTheHillMode::create(),
                                                            hill_map(spawn_x, hill_x)));
  static_cast<void>(stepped.step({testing::spawn_command(kSeekerController)}));
  return stepped;
}

[[nodiscard]] controllers::Observation observe(const testing::SteppedGame& stepped) {
  return controllers::Observation::create(
      std::make_shared<const simulation::WorldSnapshot>(stepped.game().snapshot()),
      simulation::ControllerId::create(kSeekerController));
}

[[nodiscard]] std::unique_ptr<controllers::Controller>
seeker(const std::uint64_t seed = 0,
       const controllers::HillSeekerController::Personality personality =
           controllers::HillSeekerController::Personality{}) {
  return controllers::HillSeekerController::create(
      simulation::ControllerId::create(kSeekerController), seed, personality);
}

// A seeker with no jitter, so a heading is an exact function of the geometry.
[[nodiscard]] std::unique_ptr<controllers::Controller>
steady_seeker(const double approach_weight = 1.0) {
  return seeker(0, controllers::HillSeekerController::Personality{
                       .approach_weight = approach_weight, .jitter_weight = 0.0});
}

[[nodiscard]] const controllers::HillSeekerController&
typed(const std::unique_ptr<controllers::Controller>& bot) {
  const auto* const seeker = dynamic_cast<const controllers::HillSeekerController*>(bot.get());
  REQUIRE(seeker != nullptr);
  return *seeker;
}

[[nodiscard]] simulation::Vector2 sole_thrust(const std::vector<simulation::Command>& commands) {
  REQUIRE(commands.size() == 1);
  return std::get<simulation::ThrustCommand>(commands.front()).direction;
}

} // namespace

TEST_CASE("HillSeekerController asks for a body when it drives none",
          "[unit][controllers][hill_seeker]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const std::unique_ptr<controllers::Controller> bot = controllers::HillSeekerController::create(
      simulation::ControllerId::create(kUnseatedController), 0);

  const std::vector<simulation::Command> decided =
      bot->decide(fixture.observation_for(kUnseatedController));

  REQUIRE(decided.size() == 1);
  CHECK(simulation::command_kind_of(decided.front()) == simulation::CommandKind::kSpawn);
  CHECK(simulation::addressed_identity_of(decided.front()).ordering_key() == kUnseatedController);
  CHECK(typed(bot).draw_count() == 0);
}

TEST_CASE("HillSeekerController decides nothing in a world that publishes no hill",
          "[unit][controllers][hill_seeker]") {
  // Sandbox seats a body and creates no hill: nothing to seek, and nothing drawn.
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const std::unique_ptr<controllers::Controller> bot = seeker();

  const std::vector<simulation::Command> decided =
      bot->decide(fixture.observation_for(kSeekerController));

  CHECK(decided.empty());
  CHECK(typed(bot).hill() == std::nullopt);
  CHECK(typed(bot).draw_count() == 0);
}

TEST_CASE("HillSeekerController thrusts at full commitment toward a hill it is outside of",
          "[unit][controllers][hill_seeker]") {
  // Seated 400 world units left of the centre, well outside the default radius.
  const testing::SteppedGame world = seated_hill_world(100.0, 500.0);
  const std::unique_ptr<controllers::Controller> bot = steady_seeker();

  const simulation::Vector2 direction = sole_thrust(bot->decide(observe(world)));

  CHECK(direction.x() == Catch::Approx(1.0).margin(simulation::kScalarTolerance));
  CHECK(direction.y() == Catch::Approx(0.0).margin(simulation::kScalarTolerance));
  REQUIRE(typed(bot).hill().has_value());
  // The hill is the one entity of the snapshot carrying `Hill`.
  const simulation::WorldSnapshot snapshot = world.game().snapshot();
  CHECK(snapshot.components<simulation::Hill>().front().entity == *typed(bot).hill());
  // Two draws per decision, taken even though this personality's jitter is zero.
  CHECK(typed(bot).draw_count() == 2);
}

TEST_CASE("HillSeekerController pulls proportionally inside the hill and rests at its centre",
          "[unit][controllers][hill_seeker]") {
  const double radius = gameplay::KingOfTheHillConfiguration::defaults().hill_radius();
  REQUIRE(radius > 40.0);

  SECTION("forty world units right of the centre, inside the hill") {
    const testing::SteppedGame world = seated_hill_world(540.0, 500.0);
    const simulation::Vector2 direction = sole_thrust(steady_seeker()->decide(observe(world)));

    // The offset over the radius rather than over the distance: a pull that weakens toward the
    // centre instead of a unit heading that would carry the body through it.
    CHECK(direction.x() == Catch::Approx(-40.0 / radius).margin(simulation::kScalarTolerance));
    CHECK(direction.y() == Catch::Approx(0.0).margin(simulation::kScalarTolerance));
  }

  SECTION("exactly at the centre") {
    const testing::SteppedGame world = seated_hill_world(500.0, 500.0);
    const simulation::Vector2 direction = sole_thrust(steady_seeker()->decide(observe(world)));

    CHECK(direction.x() == Catch::Approx(0.0).margin(simulation::kScalarTolerance));
    CHECK(direction.y() == Catch::Approx(0.0).margin(simulation::kScalarTolerance));
  }
}

TEST_CASE("HillSeekerController tunes its approach weight without a second type",
          "[unit][controllers][hill_seeker]") {
  const testing::SteppedGame world = seated_hill_world(100.0, 500.0);

  const simulation::Vector2 committed = sole_thrust(steady_seeker(1.0)->decide(observe(world)));
  const simulation::Vector2 shy = sole_thrust(steady_seeker(0.25)->decide(observe(world)));

  CHECK(committed.x() == Catch::Approx(1.0).margin(simulation::kScalarTolerance));
  CHECK(shy.x() == Catch::Approx(0.25).margin(simulation::kScalarTolerance));
}

TEST_CASE("Two hill seekers under different seeds do not submit identical directions",
          "[unit][controllers][hill_seeker]") {
  const testing::SteppedGame world = seated_hill_world(100.0, 500.0);
  const std::unique_ptr<controllers::Controller> first = seeker(1);
  const std::unique_ptr<controllers::Controller> second = seeker(2);
  const std::unique_ptr<controllers::Controller> first_again = seeker(1);

  const simulation::Vector2 first_direction = sole_thrust(first->decide(observe(world)));
  const simulation::Vector2 second_direction = sole_thrust(second->decide(observe(world)));
  const simulation::Vector2 first_direction_again =
      sole_thrust(first_again->decide(observe(world)));

  // The same hill, the same observation, two seeds: two lines toward it, which is what keeps two
  // seekers from settling on one spot. The same seed is the same bot.
  CHECK(first_direction != second_direction);
  CHECK(first_direction == first_direction_again);
  CHECK(typed(first).seed() == 1);
  CHECK(typed(second).seed() == 2);
}

TEST_CASE("HillSeekerController never produces a thrust component the sink would refuse",
          "[unit][controllers][hill_seeker]") {
  // Full approach plus full jitter is a component of up to two before the clamp.
  const testing::SteppedGame world = seated_hill_world(100.0, 500.0);
  const std::unique_ptr<controllers::Controller> bot =
      seeker(7, controllers::HillSeekerController::Personality{.approach_weight = 1.0,
                                                               .jitter_weight = 1.0});

  for (int pass = 0; pass < 16; ++pass) {
    const simulation::Vector2 direction = sole_thrust(bot->decide(observe(world)));
    CHECK(std::abs(direction.x()) <= simulation::kMaximumThrustDirectionComponentMagnitude);
    CHECK(std::abs(direction.y()) <= simulation::kMaximumThrustDirectionComponentMagnitude);
  }
  CHECK(typed(bot).draw_count() == 32);
}

TEST_CASE("HillSeekerController refuses a weight outside the accepted range",
          "[unit][controllers][hill_seeker]") {
  const simulation::ControllerId identity = simulation::ControllerId::create(kSeekerController);

  try {
    static_cast<void>(controllers::HillSeekerController::create(
        identity, 0, controllers::HillSeekerController::Personality{.approach_weight = 1.5}));
    FAIL("a hill seeker must refuse an approach weight above the accepted maximum");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() ==
          controllers::ControllersValidationCode::kHillSeekerWeightOutOfRange);
    CHECK(error.code() == "CONTROLLERS.HILL_SEEKER_WEIGHT_OUT_OF_RANGE");
    CHECK(error.context() == "hill_seeker_controller.personality.approach_weight");
  }

  try {
    static_cast<void>(controllers::HillSeekerController::create(
        identity, 0,
        controllers::HillSeekerController::Personality{
            .jitter_weight = std::numeric_limits<double>::quiet_NaN()}));
    FAIL("a hill seeker must refuse a non-finite jitter weight");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() ==
          controllers::ControllersValidationCode::kHillSeekerWeightNotFinite);
    CHECK(error.context() == "hill_seeker_controller.personality.jitter_weight");
  }
}
