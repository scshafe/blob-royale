#include "scripted_replay_controller.hpp"

#include "command_registry.hpp"
#include "commands/spawn_command.hpp"
#include "commands/thrust_command.hpp"
#include "controller.hpp"
#include "controller_id.hpp"
#include "controllers_limits.hpp"
#include "controllers_test_fixture.hpp"
#include "controllers_validation_error.hpp"
#include "entity_id.hpp"
#include "observation.hpp"
#include "simulation_limits.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace controllers = blob_royale::controllers;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

constexpr std::uint64_t kSeatedController = simulation::kMinimumControllerId;
constexpr std::uint64_t kUnseatedController = 900;

[[nodiscard]] simulation::Command thrust_for(const std::uint64_t entity, const double x,
                                             const double y) {
  return simulation::Command{
      simulation::ThrustCommand{.entity = simulation::EntityId::create(entity),
                                .direction = simulation::Vector2::create(x, y)}};
}

// The recorded log every test below drives: join, wait, steer right, steer up.
[[nodiscard]] std::vector<controllers::ScriptedReplayController::Step> recorded_log() {
  return {controllers::ScriptedReplayController::Step{simulation::Command{
              simulation::SpawnCommand{simulation::ControllerId::create(kSeatedController)}}},
          controllers::ScriptedReplayController::Step{},
          controllers::ScriptedReplayController::Step{thrust_for(1, 1.0, 0.0)},
          controllers::ScriptedReplayController::Step{thrust_for(1, 0.0, 1.0)}};
}

} // namespace

TEST_CASE("ScriptedReplayController drives its recorded log one step per pass, in order",
          "[unit][controllers][scripted_replay]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const std::unique_ptr<controllers::Controller> replay =
      controllers::ScriptedReplayController::create(
          simulation::ControllerId::create(kSeatedController), recorded_log());
  const controllers::Observation observation = fixture.observation_for(kSeatedController);

  const std::vector<simulation::Command> join = replay->decide(observation);
  const std::vector<simulation::Command> wait = replay->decide(observation);
  const std::vector<simulation::Command> right = replay->decide(observation);
  const std::vector<simulation::Command> up = replay->decide(observation);

  REQUIRE(join.size() == 1);
  CHECK(simulation::command_kind_of(join.front()) == simulation::CommandKind::kSpawn);
  CHECK(wait.empty());
  REQUIRE(right.size() == 1);
  CHECK(right.front() == thrust_for(1, 1.0, 0.0));
  REQUIRE(up.size() == 1);
  CHECK(up.front() == thrust_for(1, 0.0, 1.0));
}

TEST_CASE("ScriptedReplayController decides nothing once its log is exhausted",
          "[unit][controllers][scripted_replay]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const std::unique_ptr<controllers::Controller> replay =
      controllers::ScriptedReplayController::create(
          simulation::ControllerId::create(kSeatedController), recorded_log());
  const auto* const typed =
      dynamic_cast<const controllers::ScriptedReplayController*>(replay.get());
  REQUIRE(typed != nullptr);
  const controllers::Observation observation = fixture.observation_for(kSeatedController);

  REQUIRE(typed->step_count() == 4);
  for (int pass = 0; pass < 4; ++pass) {
    static_cast<void>(replay->decide(observation));
  }

  CHECK(typed->is_exhausted());
  CHECK(typed->completed_step_count() == 4);
  // A finished controller is finished, not failed: it keeps deciding nothing forever.
  CHECK(replay->decide(observation).empty());
  CHECK(replay->decide(observation).empty());
  CHECK(typed->completed_step_count() == 4);
}

TEST_CASE("ScriptedReplayController drives the same log whatever it observes",
          "[unit][controllers][scripted_replay]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const std::unique_ptr<controllers::Controller> seated =
      controllers::ScriptedReplayController::create(
          simulation::ControllerId::create(kSeatedController), recorded_log());
  const std::unique_ptr<controllers::Controller> unseated =
      controllers::ScriptedReplayController::create(
          simulation::ControllerId::create(kUnseatedController), recorded_log());

  const controllers::Observation with_body = fixture.observation_for(kSeatedController);
  const controllers::Observation without_body = fixture.observation_for(kUnseatedController);
  REQUIRE(with_body.has_live_entity());
  REQUIRE_FALSE(without_body.has_live_entity());

  // The observation is not read at all, which is the property that makes this the controller a
  // fixture can state its expectations against.
  for (int pass = 0; pass < 4; ++pass) {
    CHECK(seated->decide(with_body) == unseated->decide(without_body));
  }
}

TEST_CASE("ScriptedReplayController refuses a log above the accepted maximum",
          "[unit][controllers][scripted_replay]") {
  std::vector<controllers::ScriptedReplayController::Step> oversized(
      controllers::kMaximumScriptedReplayStepCount + 1);

  try {
    static_cast<void>(controllers::ScriptedReplayController::create(
        simulation::ControllerId::create(kSeatedController), std::move(oversized)));
    FAIL("a scripted replay must refuse a log above the accepted maximum");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() ==
          controllers::ControllersValidationCode::kScriptedReplayLogLimitExceeded);
    CHECK(error.code() == "CONTROLLERS.SCRIPTED_REPLAY_LOG_LIMIT_EXCEEDED");
  }
}
