#include "wanderer_controller.hpp"

#include "command_registry.hpp"
#include "commands/thrust_command.hpp"
#include "controller.hpp"
#include "controller_id.hpp"
#include "controllers_limits.hpp"
#include "controllers_test_fixture.hpp"
#include "controllers_validation_error.hpp"
#include "observation.hpp"
#include "simulation_limits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace controllers = blob_royale::controllers;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

constexpr std::uint64_t kSeatedController = simulation::kMinimumControllerId;
constexpr std::uint64_t kUnseatedController = 900;
constexpr std::size_t kPassCount = 24;

// The thrust directions one wanderer produces over `kPassCount` passes against one fixed world.
[[nodiscard]] std::vector<simulation::Vector2>
headings_over(const testing::ControllersFixture& fixture, const std::uint64_t seed,
              const controllers::WandererController::Personality personality) {
  const std::unique_ptr<controllers::Controller> wanderer = controllers::WandererController::create(
      simulation::ControllerId::create(kSeatedController), seed, personality);
  const controllers::Observation observation = fixture.observation_for(kSeatedController);
  std::vector<simulation::Vector2> headings;
  for (std::size_t pass = 0; pass < kPassCount; ++pass) {
    for (const simulation::Command& command : wanderer->decide(observation)) {
      headings.push_back(std::get<simulation::ThrustCommand>(command).direction);
    }
  }
  return headings;
}

} // namespace

TEST_CASE("WandererController asks for a body when it drives none and draws nothing while waiting",
          "[unit][controllers][wanderer]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const std::unique_ptr<controllers::Controller> wanderer = controllers::WandererController::create(
      simulation::ControllerId::create(kUnseatedController), 7);
  const auto* const typed = dynamic_cast<const controllers::WandererController*>(wanderer.get());
  REQUIRE(typed != nullptr);

  const std::vector<simulation::Command> decided =
      wanderer->decide(fixture.observation_for(kUnseatedController));

  REQUIRE(decided.size() == 1);
  CHECK(simulation::command_kind_of(decided.front()) == simulation::CommandKind::kSpawn);
  CHECK(simulation::addressed_identity_of(decided.front()).ordering_key() == kUnseatedController);
  // The generator is untouched while the controller waits, so the stream stays a function of the
  // decisions it took rather than of how many passes its seating happened to need.
  CHECK(typed->draw_count() == 0);
  CHECK(wanderer->entity() == std::nullopt);
}

TEST_CASE("WandererController reproduces the same headings from the same seed",
          "[unit][controllers][wanderer]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const controllers::WandererController::Personality every_pass{.reaction_delay_frames = 0};

  const std::vector<simulation::Vector2> first = headings_over(fixture, 20260907, every_pass);
  const std::vector<simulation::Vector2> second = headings_over(fixture, 20260907, every_pass);

  REQUIRE(first.size() == kPassCount);
  CHECK(first == second);
}

TEST_CASE("WandererController diverges from a different seed", "[unit][controllers][wanderer]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const controllers::WandererController::Personality every_pass{.reaction_delay_frames = 0};

  const std::vector<simulation::Vector2> first = headings_over(fixture, 20260907, every_pass);
  const std::vector<simulation::Vector2> second = headings_over(fixture, 20260908, every_pass);

  REQUIRE(first.size() == second.size());
  CHECK(first != second);
}

TEST_CASE("WandererController tunes its reaction delay without a second type",
          "[unit][controllers][wanderer]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);

  // One class, two personalities, two behaviors. This is the whole claim ADR 0004 makes about
  // personalities being configuration values.
  const std::vector<simulation::Vector2> twitchy = headings_over(
      fixture, 5, controllers::WandererController::Personality{.reaction_delay_frames = 0});
  const std::vector<simulation::Vector2> deliberate = headings_over(
      fixture, 5, controllers::WandererController::Personality{.reaction_delay_frames = 3});

  CHECK(twitchy.size() == kPassCount);
  // A heading held for three further passes decides once every four: 24 passes produce six thrusts.
  CHECK(deliberate.size() == kPassCount / 4);
  // The same seed still drives both, so the first heading is shared and only the cadence differs.
  REQUIRE(!deliberate.empty());
  CHECK(twitchy.front() == deliberate.front());
}

TEST_CASE("WandererController holds a heading rather than resending it",
          "[unit][controllers][wanderer]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const std::unique_ptr<controllers::Controller> wanderer = controllers::WandererController::create(
      simulation::ControllerId::create(kSeatedController), 3,
      controllers::WandererController::Personality{.reaction_delay_frames = 2});
  const controllers::Observation observation = fixture.observation_for(kSeatedController);

  const std::vector<simulation::Command> first = wanderer->decide(observation);
  const std::vector<simulation::Command> second = wanderer->decide(observation);
  const std::vector<simulation::Command> third = wanderer->decide(observation);
  const std::vector<simulation::Command> fourth = wanderer->decide(observation);

  // A thrust persists as stored acceleration, so the held passes decide nothing at all rather than
  // resending a command that would change no committed value.
  CHECK(first.size() == 1);
  CHECK(second.empty());
  CHECK(third.empty());
  CHECK(fourth.size() == 1);
  CHECK(wanderer->entity() == fixture.entity_of(kSeatedController));
}

TEST_CASE("WandererController refuses a reaction delay above the accepted maximum",
          "[unit][controllers][wanderer]") {
  try {
    static_cast<void>(controllers::WandererController::create(
        simulation::ControllerId::create(kSeatedController), 1,
        controllers::WandererController::Personality{
            .reaction_delay_frames = controllers::kMaximumWandererReactionDelayFrames + 1}));
    FAIL("a wanderer must refuse a reaction delay above the accepted maximum");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() ==
          controllers::ControllersValidationCode::kWandererReactionDelayOutOfRange);
    CHECK(error.code() == "CONTROLLERS.WANDERER_REACTION_DELAY_OUT_OF_RANGE");
  }
}
