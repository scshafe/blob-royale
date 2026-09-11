#include "game_mode_configuration.hpp"
#include "game_mode_registry.hpp"

#include "gameplay_test_fixture.hpp"

#include "game_mode.hpp"
#include "gameplay_validation_error.hpp"
#include "match_phase.hpp"
#include "royale/royale_mode.hpp"
#include "sandbox/sandbox_mode.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

TEST_CASE("GameModeRegistry resolves a registered name to a working mode",
          "[unit][gameplay][game_mode_registry]") {
  std::unique_ptr<const simulation::GameMode> mode = gameplay::GameModeRegistry::create("sandbox");

  REQUIRE(mode != nullptr);
  CHECK(mode->name() == std::string_view{"sandbox"});

  // "Working" is not "constructible": the mode has to survive the engine reading all seven
  // declarations and then running a tick against them.
  simulation::GameSimulation game =
      testing::gameplay_simulation(std::move(mode), testing::gameplay_map(4));
  game.step(testing::kGameplayFixedDelta, simulation::InputBatch::empty());

  const simulation::WorldSnapshot snapshot = game.snapshot();
  CHECK(game.mode_name() == std::string_view{"sandbox"});
  CHECK(snapshot.match().mode_name() == std::string_view{"sandbox"});
  CHECK(snapshot.match().phase() == simulation::MatchPhase::kCountdown);
}

TEST_CASE("GameModeRegistry rejects an unknown mode name with a named validation code",
          "[unit][gameplay][game_mode_registry][validation]") {
  try {
    static_cast<void>(gameplay::GameModeRegistry::create("battle_chess"));
    FAIL("an unregistered mode name was accepted");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() == gameplay::GameplayValidationCode::kGameModeNameUnknown);
    CHECK(error.code() == std::string_view{"GAMEPLAY.GAME_MODE_NAME_UNKNOWN"});
    CHECK(error.context() == "game_mode_registry.mode");
    // The rejection lists what is playable, so a mis-typed `[match] mode=` names its own fix.
    CHECK(error.detail().find("sandbox") != std::string::npos);
  }
}

TEST_CASE("GameModeRegistry rejects an empty mode name rather than defaulting",
          "[unit][gameplay][game_mode_registry][validation]") {
  CHECK_THROWS_AS(static_cast<void>(gameplay::GameModeRegistry::create("")),
                  gameplay::GameplayValidationError);
  CHECK_FALSE(gameplay::GameModeRegistry::contains(""));
}

TEST_CASE("GameModeRegistry publishes every registered mode in declared order",
          "[unit][gameplay][game_mode_registry]") {
  REQUIRE(gameplay::GameModeRegistry::registrations().size() ==
          gameplay::kGameModeRegistrations.size());
  CHECK(gameplay::GameModeRegistry::registrations()[0].name == gameplay::SandboxMode::kModeName);
  CHECK(gameplay::GameModeRegistry::registrations()[1].name == gameplay::RoyaleMode::kModeName);
  CHECK(gameplay::GameModeRegistry::contains("sandbox"));
  CHECK(gameplay::GameModeRegistry::contains("royale"));
  // Declared order, which is the order a rejection detail and a `--help` list them in. It was
  // `"sandbox"` alone until plan Step 21 registered the second game; the seam's whole claim is that
  // adding one is a row here, so this is the assertion that measures it.
  CHECK(gameplay::GameModeRegistry::registered_names() ==
        "sandbox, royale, king_of_the_hill, race");
}

TEST_CASE("Every registered mode name is distinct at compile time",
          "[unit][gameplay][game_mode_registry]") {
  // The runtime twin of the static_assert beside the table: a second row claiming one name would
  // resolve to whichever was written first, so the registry rejects it before it can.
  STATIC_REQUIRE(gameplay::game_mode_names_are_distinct());
}

TEST_CASE("Every registered factory produces a mode that answers to its registered name",
          "[unit][gameplay][game_mode_registry]") {
  for (const gameplay::GameModeRegistry::Registration& registration :
       gameplay::GameModeRegistry::registrations()) {
    const std::unique_ptr<const simulation::GameMode> mode =
        registration.factory(gameplay::GameModeConfiguration::defaults());
    REQUIRE(mode != nullptr);
    CHECK(mode->name() == registration.name);
  }
}

TEST_CASE("GameModeConfiguration owns shared movement defaults independently of every mode section",
          "[unit][gameplay][game_mode_registry][movement]") {
  const auto original = gameplay::GameModeConfiguration::defaults();
  CHECK(original.movement == simulation::MovementTuning::defaults());
  auto changed = original;
  changed.movement = testing::gameplay_movement_tuning();
  CHECK(changed != original);
  CHECK(changed.royale == original.royale);
  CHECK(changed.king_of_the_hill == original.king_of_the_hill);
  CHECK(changed.race == original.race);
  CHECK(changed.hazards == original.hazards);
}

TEST_CASE("Only modes with seats advertise the shared movement tuning command",
          "[unit][gameplay][game_mode_registry][movement]") {
  for (const auto& registration : gameplay::GameModeRegistry::registrations()) {
    CAPTURE(registration.name);
    const auto mode = registration.factory(gameplay::GameModeConfiguration::defaults());
    CHECK(mode->accepted_command_kinds().contains(simulation::CommandKind::kSetMovementTuning) ==
          (registration.name != gameplay::SandboxMode::kModeName));
  }
}
