#include "controller.hpp"

#include "command_registry.hpp"
#include "commands/despawn_command.hpp"
#include "controller_id.hpp"
#include "controllers_limits.hpp"
#include "controllers_test_fixture.hpp"
#include "controllers_validation_error.hpp"
#include "entity_id.hpp"
#include "observation.hpp"
#include "simulation_limits.hpp"
#include "wanderer_controller.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace controllers = blob_royale::controllers;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

constexpr std::uint64_t kSeatedController = simulation::kMinimumControllerId;
constexpr std::uint64_t kUnseatedController = 900;

// A real registered bot, because the base's shared behavior is only worth asserting through
// something a roster would actually run.
[[nodiscard]] std::unique_ptr<controllers::Controller> bot(const std::uint64_t controller) {
  return controllers::WandererController::create(simulation::ControllerId::create(controller), 9);
}

} // namespace

TEST_CASE("Controller refuses an observation built for a different controller",
          "[unit][controllers][controller]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const std::unique_ptr<controllers::Controller> mine = bot(kSeatedController);

  try {
    static_cast<void>(mine->decide(fixture.observation_for(kUnseatedController)));
    FAIL("a controller must not decide from another controller's observation");
  } catch (const controllers::ControllersValidationError& error) {
    // A capability check, not a formality: an observation carries one controller's resolved body,
    // so deciding from a foreign one would act on an identity this controller was never issued.
    CHECK(error.validation_code() ==
          controllers::ControllersValidationCode::kObservationControllerMismatch);
    CHECK(error.code() == "CONTROLLERS.OBSERVATION_CONTROLLER_MISMATCH");
  }
}

TEST_CASE("Controller tracks the body it was driving at its most recent decision",
          "[unit][controllers][controller]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const std::unique_ptr<controllers::Controller> seated = bot(kSeatedController);
  const std::unique_ptr<controllers::Controller> pending = bot(kUnseatedController);

  // Before the first decision a controller has never seen a world, so it drives nothing.
  CHECK(seated->entity() == std::nullopt);

  static_cast<void>(seated->decide(fixture.observation_for(kSeatedController)));
  static_cast<void>(pending->decide(fixture.observation_for(kUnseatedController)));

  CHECK(seated->entity() == fixture.entity_of(kSeatedController));
  CHECK(pending->entity() == std::nullopt);
}

TEST_CASE("Controller asks for a body once and holds the request for the retry interval",
          "[unit][controllers][controller]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const std::unique_ptr<controllers::Controller> pending = bot(kUnseatedController);

  const std::vector<simulation::Command> first =
      pending->decide(fixture.observation_at_committed(kUnseatedController));
  REQUIRE(first.size() == 1);
  CHECK(simulation::command_kind_of(first.front()) == simulation::CommandKind::kSpawn);
  REQUIRE(pending->last_spawn_request_tick().has_value());
  const std::uint64_t requested_at = pending->last_spawn_request_tick()->value();

  // Every observation inside the interval decides nothing. Asking again here is exactly what would
  // give one controller two bodies, because the first request is still in flight.
  for (std::uint64_t tick = 1; tick < controllers::kSpawnRequestRetryTicks; ++tick) {
    static_cast<void>(fixture.commit());
    CHECK(pending->decide(fixture.observation_at_committed(kUnseatedController)).empty());
  }

  // And a request that was never answered is eventually repeated, so a bot whose spawn the mailbox
  // refused is not stranded silently and permanently.
  static_cast<void>(fixture.commit());
  const std::vector<simulation::Command> repeated =
      pending->decide(fixture.observation_at_committed(kUnseatedController));
  REQUIRE(repeated.size() == 1);
  CHECK(simulation::command_kind_of(repeated.front()) == simulation::CommandKind::kSpawn);
  CHECK(pending->last_spawn_request_tick()->value() ==
        requested_at + controllers::kSpawnRequestRetryTicks);
}

TEST_CASE("Controller asks again immediately once a body it held has been destroyed",
          "[unit][controllers][controller]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const std::unique_ptr<controllers::Controller> joining = bot(kUnseatedController);

  // Ask, be seated, then be eliminated well inside the retry interval.
  REQUIRE(joining->decide(fixture.observation_at_committed(kUnseatedController)).size() == 1);
  static_cast<void>(fixture.commit({testing::spawn_command(kUnseatedController)}));
  static_cast<void>(joining->decide(fixture.observation_at_committed(kUnseatedController)));
  REQUIRE(joining->entity().has_value());
  const simulation::EntityId seated_body = *joining->entity();
  CHECK(joining->last_spawn_request_tick() == std::nullopt);

  static_cast<void>(
      fixture.commit({simulation::Command{simulation::DespawnCommand{.entity = seated_body}}}));
  const std::vector<simulation::Command> after_loss =
      joining->decide(fixture.observation_at_committed(kUnseatedController));

  // The interval is a bound on *unanswered* requests, so an elimination is followed by an immediate
  // rejoin rather than by a hundred milliseconds of absence.
  REQUIRE(after_loss.size() == 1);
  CHECK(simulation::command_kind_of(after_loss.front()) == simulation::CommandKind::kSpawn);
}
