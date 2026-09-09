#include "royale/royale_mode.hpp"

#include "shared/lethal_hazard_contact_rule.hpp"

#include "gameplay_test_fixture.hpp"

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "contact_rule_table.hpp"
#include "game_mode_registry.hpp"
#include "gameplay_validation_error.hpp"
#include "map_definition.hpp"
#include "mode_match_state_registry.hpp"
#include "mode_states/royale_placements_mode_state.hpp"
#include "royale/royale_configuration.hpp"
#include "system_pipeline.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] gameplay::RoyaleMode default_mode() {
  return gameplay::RoyaleMode{gameplay::RoyaleConfiguration::defaults()};
}

} // namespace

TEST_CASE("RoyaleMode declares the shrinking-zone game as seven answers",
          "[unit][gameplay][royale]") {
  const gameplay::RoyaleMode mode = default_mode();

  CHECK(mode.name() == std::string_view{"royale"});
  // One royale row above the built-in ones, and the built-in ones unmodified below it. Royale still
  // changes no collision *equation* -- `lethal_hazard` computes no physics and returns both bodies
  // verbatim -- so every accepted pair and wall fixture stays valid without regeneration, which the
  // suffix check below states as a property rather than as a retyped list.
  const simulation::ContactRuleTable rules = mode.contact_rules();
  const simulation::ContactRuleTable built_in = simulation::ContactRuleTable::built_in();
  REQUIRE(rules.size() == built_in.size() + 1);
  CHECK(rules.rows()[0].name() == gameplay::kLethalHazardContactRuleName);
  for (std::size_t index = 0; index < built_in.size(); ++index) {
    CHECK(rules.rows()[index + 1] == built_in.rows()[index]);
  }
  // Seven: the three every mode needs, and the four that operate the pre-match lobby. A mode
  // declares the lobby kinds rather than the engine offering them to everybody, which is why
  // `sandbox` -- whose objective never starts -- accepts none of them.
  CHECK(mode.accepted_command_kinds() ==
        simulation::CommandKindMask::create(
            {simulation::CommandKind::kSpawn, simulation::CommandKind::kDespawn,
             simulation::CommandKind::kThrust, simulation::CommandKind::kSetSeatCount,
             simulation::CommandKind::kClearSeat, simulation::CommandKind::kSeatNpc,
             simulation::CommandKind::kStartMatch, simulation::CommandKind::kLeave}));
}

TEST_CASE("RoyaleMode declares seven systems in the order its rules depend on",
          "[unit][gameplay][royale]") {
  const simulation::SystemPipeline systems = default_mode().systems();
  REQUIRE(systems.size() == 7);

  REQUIRE(systems.systems_at(simulation::SystemStage::kPreKernel).size() == 1);
  CHECK(systems.systems_at(simulation::SystemStage::kPreKernel)[0].system->name() ==
        std::string_view{"thrust_steering"});

  // `zone_elimination` reads the radius this tick's `zone_shrink` wrote, so the declared order at
  // this stage is the rule and not a preference.
  REQUIRE(systems.systems_at(simulation::SystemStage::kPostKernel).size() == 2);
  CHECK(systems.systems_at(simulation::SystemStage::kPostKernel)[0].system->name() ==
        std::string_view{"zone_shrink"});
  CHECK(systems.systems_at(simulation::SystemStage::kPostKernel)[1].system->name() ==
        std::string_view{"zone_elimination"});

  // Remove, then add, then publish: `placement_recorder` destroys this tick's eliminated entities,
  // then `lifetime_expiry` emits the despawns for whatever ran out, then `hazard_spawn` draws from
  // the entity id reservation only after every other creating system has taken what it needs, and
  // `elimination_grace_publisher` runs last so it is the final writer of the mode-state block.
  REQUIRE(systems.systems_at(simulation::SystemStage::kLifecycle).size() == 4);
  CHECK(systems.systems_at(simulation::SystemStage::kLifecycle)[1].system->name() ==
        std::string_view{"lifetime_expiry"});
  CHECK(systems.systems_at(simulation::SystemStage::kLifecycle)[2].system->name() ==
        std::string_view{"hazard_spawn"});
  CHECK(systems.systems_at(simulation::SystemStage::kLifecycle)[0].system->name() ==
        std::string_view{"placement_recorder"});
  // Last, and the position is the rule rather than a preference: `placement_recorder` reads the
  // whole block out and assigns it back, so a publisher declared ahead of it would be relying on
  // another system to carry a member it does not know about.
  CHECK(systems.systems_at(simulation::SystemStage::kLifecycle)[3].system->name() ==
        std::string_view{"elimination_grace_publisher"});
}

TEST_CASE("RoyaleMode rejects a map with fewer spawn markers than its lobby has seats",
          "[unit][gameplay][royale][validation]") {
  // A map with fewer could not seat a full lobby, so a match that satisfied `can_start` would still
  // leave joiners pending: it is a startup rejection naming the map rather than a silent stall.
  try {
    default_mode().validate_map(testing::gameplay_map(1, "royale_one_point_map"));
    FAIL("a map with one spawn marker was accepted for a four-seat lobby");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() ==
          gameplay::GameplayValidationCode::kRoyaleMapWithoutEnoughSpawnPoints);
    CHECK(error.code() == std::string_view{"GAMEPLAY.ROYALE_MAP_WITHOUT_ENOUGH_SPAWN_POINTS"});
    CHECK(error.detail().find("royale_one_point_map") != std::string::npos);
  }

  CHECK_NOTHROW(default_mode().validate_map(testing::gameplay_map(4, "royale_four_point_map")));
}

TEST_CASE("RoyaleMode rejects a map whose arena is already inside the zone minimum",
          "[unit][gameplay][royale][validation]") {
  // `R_min` is checked here rather than at parse time because `R_full` is a property of the map's
  // bounds, not of the `[royale]` section. An arena that starts at its floor would never contract.
  const simulation::MapDefinition tiny = simulation::MapDefinition::create(
      "royale_tiny_map", simulation::ArenaBounds::create(60.0, 40.0), {},
      // Four markers, because the spawn-marker check runs first and this test is about the second
      // rejection: a map short of the default lobby's seats would be refused for the wrong reason.
      {simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(20.0, 10.0)),
       simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(40.0, 10.0)),
       simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(20.0, 30.0)),
       simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(40.0, 30.0))},
      simulation::MapMetadata::none());

  try {
    default_mode().validate_map(tiny);
    FAIL("a map smaller than the zone minimum was accepted");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() ==
          gameplay::GameplayValidationCode::kRoyaleMapArenaWithinZoneMinimum);
    CHECK(error.code() == std::string_view{"GAMEPLAY.ROYALE_MAP_ARENA_WITHIN_ZONE_MINIMUM"});
    CHECK(error.detail().find("royale_tiny_map") != std::string::npos);
  }
}

TEST_CASE("royale resolves through the one game mode registry",
          "[unit][gameplay][royale][registry]") {
  CHECK(gameplay::GameModeRegistry::contains("royale"));
  CHECK(gameplay::GameModeRegistry::create("royale")->name() == std::string_view{"royale"});
  CHECK(gameplay::GameModeRegistry::registrations().size() == 2);
  CHECK(gameplay::GameModeRegistry::registered_names() == "sandbox, royale");
}

TEST_CASE("royale's match state is one registered arm of the mode-state seam",
          "[unit][gameplay][royale][mode_state]") {
  // The arm is assigned by royale's own `kLifecycle` system the first tick it observes another, so
  // the engine neither reads it, writes it, nor branches on which arm is held.
  testing::SteppedGame driver{testing::gameplay_simulation(
      gameplay::RoyaleMode::create(), testing::gameplay_map(4, "royale_mode_state_map"))};

  const simulation::WorldSnapshot snapshot = driver.step();
  CHECK(simulation::mode_match_state_schema_id_of(snapshot.match().mode_state()) ==
        std::string_view{"royale_placements"});
  const auto* held =
      std::get_if<simulation::RoyalePlacementsModeState>(&snapshot.match().mode_state());
  REQUIRE(held != nullptr);
  CHECK(held->placements.empty());
  CHECK(held->previous_phase == simulation::MatchPhase::kLobby);
  // Stamped on the very first committed tick, which is the earliest tick any client can receive:
  // `SnapshotPublication` reports not-ready until one tick has completed. There is no snapshot on
  // which a client sees a default zero that a later frame corrects.
  CHECK(held->elimination_grace_ticks ==
        gameplay::RoyaleConfiguration::defaults().elimination_grace_ticks());
}

TEST_CASE("the grace royale publishes is the grace royale enforces",
          "[unit][gameplay][royale][mode_state]") {
  // `zone_elimination` and `elimination_grace_publisher` are built from one `RoyaleConfiguration`
  // at one call site, which is what keeps the number on the wire from drifting away from the number
  // the rule applies. A configured value rather than the default, so a publisher that ignored its
  // construction and hard-coded the proposed 1,200 would fail here.
  gameplay::RoyaleConfiguration::Section section = gameplay::RoyaleConfiguration::default_section();
  section.elimination_grace_seconds = 0.5;
  const gameplay::RoyaleConfiguration configuration =
      gameplay::RoyaleConfiguration::create(section);
  REQUIRE(configuration.elimination_grace_ticks() == 200);

  testing::SteppedGame driver{testing::gameplay_simulation(
      gameplay::RoyaleMode::create(configuration), testing::gameplay_map(4, "royale_grace_map"))};

  const simulation::WorldSnapshot snapshot = driver.step();
  const auto* held =
      std::get_if<simulation::RoyalePlacementsModeState>(&snapshot.match().mode_state());
  REQUIRE(held != nullptr);
  CHECK(held->elimination_grace_ticks == configuration.elimination_grace_ticks());
}

TEST_CASE("a royale mode built from its own configuration hands it to the systems it declares",
          "[unit][gameplay][royale][configuration]") {
  // The mode holds its validated section and hands it to the systems and policies it builds, which
  // is the only way configuration reaches a tick. Two royale modes with different thrust maxima are
  // two different games played by the same rules.
  gameplay::RoyaleConfiguration::Section section = gameplay::RoyaleConfiguration::default_section();
  section.thrust_max_world_units_per_second_squared = 1'000.0;
  section.lobby_seat_count = 3;
  const gameplay::RoyaleMode mode{gameplay::RoyaleConfiguration::create(section)};

  CHECK_THROWS_AS(mode.validate_map(testing::gameplay_map(2, "royale_two_point_map")),
                  gameplay::GameplayValidationError);
  CHECK_NOTHROW(mode.validate_map(testing::gameplay_map(3, "royale_three_point_map")));
  CHECK(mode.objective()->durations() == default_mode().objective()->durations());
}
