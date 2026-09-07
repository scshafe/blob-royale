#include "controller_registry.hpp"

#include "chaser_controller.hpp"
#include "controller.hpp"
#include "controller_directory.hpp"
#include "controller_id.hpp"
#include "controllers_validation_error.hpp"
#include "scripted_replay_controller.hpp"
#include "simulation_limits.hpp"
#include "wanderer_controller.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace controllers = blob_royale::controllers;
namespace runtime = blob_royale::runtime;
namespace simulation = blob_royale::simulation;

namespace {

constexpr std::uint64_t kController = simulation::kMinimumControllerId;
constexpr std::uint64_t kSeed = 20260907;

} // namespace

TEST_CASE("ControllerRegistry resolves every registered kind under the given identity and seed",
          "[unit][controllers][controller_registry]") {
  const simulation::ControllerId identity = simulation::ControllerId::create(kController);

  for (const controllers::ControllerRegistry::Registration& registration :
       controllers::ControllerRegistry::registrations()) {
    const std::unique_ptr<controllers::Controller> bot =
        controllers::ControllerRegistry::create(registration.name, identity, kSeed);
    REQUIRE(bot != nullptr);
    CHECK(bot->kind() == registration.name);
    CHECK(bot->controller() == identity);
    CHECK(controllers::ControllerRegistry::contains(registration.name));
  }
}

TEST_CASE("ControllerRegistry lists wanderer and chaser in declared order",
          "[unit][controllers][controller_registry]") {
  const std::span<const controllers::ControllerRegistry::Registration> rows =
      controllers::ControllerRegistry::registrations();

  REQUIRE(rows.size() == 2);
  CHECK(rows[0].name == controllers::WandererController::kControllerKind);
  CHECK(rows[1].name == controllers::ChaserController::kControllerKind);
  CHECK(controllers::ControllerRegistry::registered_names() == "wanderer, chaser");
}

TEST_CASE("ControllerRegistry rejects an unknown kind and names the kinds it does resolve",
          "[unit][controllers][controller_registry]") {
  CHECK_FALSE(controllers::ControllerRegistry::contains("sniper"));

  try {
    static_cast<void>(controllers::ControllerRegistry::create(
        "sniper", simulation::ControllerId::create(kController), kSeed));
    FAIL("an unregistered kind must not resolve");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() ==
          controllers::ControllersValidationCode::kControllerKindUnknown);
    CHECK(error.code() == "CONTROLLERS.CONTROLLER_KIND_UNKNOWN");
    CHECK(error.detail().find("wanderer, chaser") != std::string::npos);
  }
}

TEST_CASE("ControllerRegistry deliberately does not register scripted_replay",
          "[unit][controllers][controller_registry]") {
  // A registered kind is one `[match] bots=` may name, and a scripted controller is meaningless
  // without the recorded log no configuration line carries. Registering it would make
  // `bots=scripted_replay:1` produce a bot that silently decides nothing.
  CHECK_FALSE(controllers::ControllerRegistry::contains(
      controllers::ScriptedReplayController::kControllerKind));
  CHECK_THROWS_AS(controllers::ControllerRegistry::create(
                      controllers::ScriptedReplayController::kControllerKind,
                      simulation::ControllerId::create(kController), kSeed),
                  controllers::ControllersValidationError);
}

TEST_CASE("Every registered controller kind is a name the ControllerDirectory accepts",
          "[unit][controllers][controller_registry]") {
  // The registered name is also the `controller_kind` a session registers and the wire publishes,
  // so a row whose name the directory refused would produce a bot that could never open a session.
  for (const controllers::ControllerRegistry::Registration& registration :
       controllers::ControllerRegistry::registrations()) {
    CHECK(runtime::ControllerDirectory::is_valid_controller_kind(registration.name));
  }
  CHECK(runtime::ControllerDirectory::is_valid_controller_kind(
      controllers::ScriptedReplayController::kControllerKind));
}
