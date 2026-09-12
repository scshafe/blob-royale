#include "controller_registry.hpp"
#include "fixtures/tactical_profile_fixture.hpp"

#include "chaser_controller.hpp"
#include "controller.hpp"
#include "controller_directory.hpp"
#include "controller_id.hpp"
#include "controllers_validation_error.hpp"
#include "hill_seeker_controller.hpp"
#include "racer_controller.hpp"
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
namespace profile_fixture = blob_royale::testing::tactical_profile_fixture;

namespace {

constexpr std::uint64_t kController = simulation::kMinimumControllerId;
constexpr std::uint64_t kSeed = 20260907;

} // namespace

TEST_CASE("ControllerRegistry resolves every registered kind under the given identity and seed",
          "[unit][controllers][controller_registry]") {
  const simulation::ControllerId identity = simulation::ControllerId::create(kController);
  const auto profile = profile_fixture::profile();

  for (const controllers::ControllerRegistry::Registration& registration :
       controllers::ControllerRegistry::registrations()) {
    const auto context = registration.requires_profile
                             ? controllers::CreationContext{&profile, profile_fixture::kIdentity}
                             : controllers::CreationContext{};
    const std::unique_ptr<controllers::Controller> bot =
        controllers::ControllerRegistry::create(registration.name, identity, kSeed, context);
    REQUIRE(bot != nullptr);
    CHECK(bot->kind() == registration.name);
    CHECK(bot->controller() == identity);
    CHECK(controllers::ControllerRegistry::contains(registration.name));
  }
}

TEST_CASE(
    "ControllerRegistry preserves four plain rows and appends the profile-required tactical row",
    "[unit][controllers][controller_registry]") {
  const std::span<const controllers::ControllerRegistry::Registration> rows =
      controllers::ControllerRegistry::registrations();

  REQUIRE(rows.size() == 5);
  CHECK(rows[0].name == controllers::WandererController::kControllerKind);
  CHECK(rows[1].name == controllers::ChaserController::kControllerKind);
  CHECK(rows[2].name == controllers::HillSeekerController::kControllerKind);
  CHECK(rows[3].name == controllers::RacerController::kControllerKind);
  CHECK(rows[4].name == controllers::TacticalController::kControllerKind);
  for (std::size_t index = 0; index < 4; ++index) {
    CHECK_FALSE(rows[index].requires_profile);
    CHECK(controllers::ControllerRegistry::find(rows[index].name) == &rows[index]);
  }
  CHECK(rows[4].requires_profile);
  CHECK(controllers::ControllerRegistry::find(rows[4].name) == &rows[4]);
  CHECK(controllers::ControllerRegistry::registered_names() ==
        "wanderer, chaser, hill_seeker, racer, tactical");
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
    CHECK(error.detail().find("wanderer, chaser, hill_seeker, racer") != std::string::npos);
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

TEST_CASE("ControllerRegistry validates exact creation context after unknown-kind resolution",
          "[unit][controllers][controller_registry]") {
  const auto identity = simulation::ControllerId::create(kController);
  const auto profile = profile_fixture::profile();
  const controllers::CreationContext profile_only{&profile, std::nullopt};
  const controllers::CreationContext identity_only{nullptr, profile_fixture::kIdentity};
  const controllers::CreationContext both{&profile, profile_fixture::kIdentity};
  for (const auto& row : controllers::ControllerRegistry::registrations()) {
    if (row.requires_profile) {
      continue;
    }
    for (const auto& context : {profile_only, identity_only, both}) {
      try {
        static_cast<void>(
            controllers::ControllerRegistry::create(row.name, identity, kSeed, context));
        FAIL("plain factories reject either profile context field");
      } catch (const controllers::ControllersValidationError& error) {
        CHECK(error.validation_code() ==
              controllers::ControllersValidationCode::kControllerCreationContextInvalid);
      }
    }
  }
  for (const auto& context : {controllers::CreationContext{}, profile_only, identity_only}) {
    try {
      static_cast<void>(controllers::ControllerRegistry::create(
          controllers::TacticalController::kControllerKind, identity, kSeed, context));
      FAIL("tactical factory requires profile and seed identity");
    } catch (const controllers::ControllersValidationError& error) {
      CHECK(error.validation_code() ==
            controllers::ControllersValidationCode::kControllerCreationContextInvalid);
    }
  }
  CHECK(controllers::ControllerRegistry::find("unknown") == nullptr);
  try {
    static_cast<void>(
        controllers::ControllerRegistry::create("unknown", identity, kSeed, profile_only));
    FAIL("unknown kind must fail before invalid context");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() ==
          controllers::ControllersValidationCode::kControllerKindUnknown);
  }
  const auto created = controllers::ControllerRegistry::create(
      controllers::TacticalController::kControllerKind, identity, kSeed, both);
  const auto* tactical = dynamic_cast<const controllers::TacticalController*>(created.get());
  REQUIRE(tactical != nullptr);
  CHECK(tactical->profile() == profile);
  CHECK(tactical->seed_identity() == profile_fixture::kIdentity);
}
