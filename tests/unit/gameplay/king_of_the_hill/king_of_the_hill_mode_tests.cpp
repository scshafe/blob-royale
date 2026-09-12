#include "king_of_the_hill/king_of_the_hill_mode.hpp"

#include "shared/guarded_pair_contact_rule.hpp"

#include "components/respawn_timer_component.hpp"
#include "fixtures/falling_mode_fixture.hpp"
#include "gameplay_test_fixture.hpp"

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "components/hill_component.hpp"
#include "components/score_component.hpp"
#include "contact_rule_table.hpp"
#include "game_mode_registry.hpp"
#include "gameplay_validation_error.hpp"
#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "map_definition.hpp"
#include "match_outcome.hpp"
#include "match_phase.hpp"
#include "mode_match_state_registry.hpp"
#include "mode_states/king_of_the_hill_mode_state.hpp"
#include "system_pipeline.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] gameplay::KingOfTheHillMode default_mode() {
  return gameplay::KingOfTheHillMode{gameplay::KingOfTheHillConfiguration::defaults()};
}

// Four spawn points on a line and one hill marker on the first of them, so a seated player is
// inside the hill from its first tick.
[[nodiscard]] simulation::MapDefinition hill_map(const std::string& name) {
  std::vector<simulation::MapDefinition::Marker> markers;
  for (std::size_t index = 0; index < 4; ++index) {
    markers.push_back(simulation::MapDefinition::Marker::spawn(
        simulation::Vector2::create(100.0 * static_cast<double>(index + 1), 320.0)));
  }
  markers.push_back(
      simulation::MapDefinition::Marker::create("hill", simulation::Vector2::create(100.0, 320.0),
                                                std::nullopt, simulation::MapMetadata::none()));
  return simulation::MapDefinition::create(name, simulation::ArenaBounds::create(960.0, 640.0), {},
                                           std::move(markers), simulation::MapMetadata::none());
}

// Zero countdown, a two-tick point interval, two points to win, and a small hill.
[[nodiscard]] gameplay::KingOfTheHillConfiguration quick_configuration() {
  gameplay::KingOfTheHillConfiguration::Section section =
      gameplay::KingOfTheHillConfiguration::default_section();
  section.countdown_seconds = 0.0;
  section.point_interval_seconds = 0.005;
  section.points_to_win = 2;
  section.hill_radius_world_units = 30.0;
  return gameplay::KingOfTheHillConfiguration::create(section);
}

} // namespace

TEST_CASE("KingOfTheHillMode declares the hill game as eight answers",
          "[unit][gameplay][king_of_the_hill]") {
  const gameplay::KingOfTheHillMode mode = default_mode();

  CHECK(mode.name() == std::string_view{"king_of_the_hill"});
  CHECK(mode.motion_triggers().size() == 1);
  const simulation::ContactRuleTable rules = mode.contact_rules();
  const simulation::ContactRuleTable built_in = simulation::ContactRuleTable::built_in();
  REQUIRE(rules.size() == built_in.size() + 1);
  // The shared `guarded_pair` row, above the engine's own three, which its symmetric presence
  // predicates make unreachable here; the suffix check states that they are nonetheless still
  // declared and still the engine's own values.
  CHECK(rules.rows()[0].name() == gameplay::kGuardedPairContactRuleName);
  for (std::size_t index = 0; index < built_in.size(); ++index) {
    CHECK(rules.rows()[index + 1] == built_in.rows()[index]);
  }
  // Royale's twelve, `shield` and `charge` included, and both advertised only because this mode
  // declares the one `ability` system that admits both of them.
  CHECK(mode.accepted_command_kinds() ==
        simulation::CommandKindMask::create(
            {simulation::CommandKind::kSpawn, simulation::CommandKind::kDespawn,
             simulation::CommandKind::kThrust, simulation::CommandKind::kShield,
             simulation::CommandKind::kCharge, simulation::CommandKind::kSetMovementTuning,
             simulation::CommandKind::kSetSeatCount, simulation::CommandKind::kClearSeat,
             simulation::CommandKind::kSeatNpc, simulation::CommandKind::kStartMatch,
             simulation::CommandKind::kLeave, simulation::CommandKind::kJoin}));
  CHECK(mode.spawn_policy() != nullptr);
  CHECK(mode.objective() != nullptr);
}

TEST_CASE("hill falling removes body-bound state and returns on N plus D plus one",
          "[unit][gameplay][king_of_the_hill][falling][respawn]") {
  namespace falling = testing::falling_mode_fixture;
  auto section = gameplay::KingOfTheHillConfiguration::default_section();
  section.respawn_delay_seconds = 0.005;
  auto game = falling::game(
      gameplay::KingOfTheHillMode::create(gameplay::KingOfTheHillConfiguration::create(section)));
  const auto fallen = falling::step(game);
  CHECK(falling::component<simulation::PhysicsBody>(fallen, 1) == nullptr);
  CHECK(falling::component<simulation::Controllable>(fallen, 1) != nullptr);
  CHECK(falling::component<simulation::ContactEffectAdmission>(fallen, 1) == nullptr);
  REQUIRE(falling::component<simulation::RespawnTimer>(fallen, 1) != nullptr);
  CHECK(falling::component<simulation::RespawnTimer>(fallen, 1)->ticks_remaining == 2);
  for (unsigned elapsed = 0; elapsed < 2; ++elapsed) {
    const auto waiting = falling::step(game);
    CHECK(falling::component<simulation::PhysicsBody>(waiting, 1) == nullptr);
  }
  const auto returned = falling::step(game);
  CHECK(returned.tick_sequence().value() == 4);
  const auto* body = falling::component<simulation::PhysicsBody>(returned, 1);
  REQUIRE(body != nullptr);
  CHECK(body->position() == falling::point(100, 320));
  CHECK(body->velocity() == falling::point(0, 0));
  CHECK(body->ground_attachment() == simulation::GroundAttachment::kGroundBound);
  const auto resting = falling::step(game);
  REQUIRE(falling::component<simulation::PhysicsBody>(resting, 1) != nullptr);
  CHECK(falling::component<simulation::PhysicsBody>(resting, 1)->velocity() ==
        falling::point(0, 0));
}

TEST_CASE("hill support loss stays inactive before the running phase",
          "[unit][gameplay][king_of_the_hill][falling]") {
  namespace falling = testing::falling_mode_fixture;
  for (const auto phase : {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown}) {
    auto game = falling::game(gameplay::KingOfTheHillMode::create(), phase);
    const auto stepped = falling::step(game);
    CHECK(falling::component<simulation::PhysicsBody>(stepped, 1) != nullptr);
    CHECK(falling::component<simulation::RespawnTimer>(stepped, 1) == nullptr);
  }
}

TEST_CASE("KingOfTheHillMode declares ten systems in the order its rules depend on",
          "[unit][gameplay][king_of_the_hill]") {
  const simulation::SystemPipeline systems = default_mode().systems();
  REQUIRE(systems.size() == 10);
  // `ability` is last at this stage in every mode that declares it, mirroring `status` at
  // kPostKernel: pulse admission reads the canonical input lock, so every kPreKernel system that
  // can change what that lock answers has already run.
  REQUIRE(systems.systems_at(simulation::SystemStage::kPreKernel).size() == 2);
  CHECK(systems.systems_at(simulation::SystemStage::kPreKernel)[0].system->name() ==
        std::string_view{"thrust_steering"});
  CHECK(systems.systems_at(simulation::SystemStage::kPreKernel)[1].system->name() ==
        std::string_view{"ability"});
  // Scoring reads the circle this tick's movement wrote.
  REQUIRE(systems.systems_at(simulation::SystemStage::kPostKernel).size() == 3);
  CHECK(systems.systems_at(simulation::SystemStage::kPostKernel)[0].system->name() ==
        std::string_view{"hill_movement"});
  CHECK(systems.systems_at(simulation::SystemStage::kPostKernel)[1].system->name() ==
        std::string_view{"hill_scoring"});
  CHECK(systems.systems_at(simulation::SystemStage::kPostKernel)[2].system->name() == "status");
  // Remove, reset, expire, add, publish.
  REQUIRE(systems.systems_at(simulation::SystemStage::kLifecycle).size() == 5);
  CHECK(systems.systems_at(simulation::SystemStage::kLifecycle)[0].system->name() ==
        std::string_view{"respawn"});
  CHECK(systems.systems_at(simulation::SystemStage::kLifecycle)[1].system->name() ==
        std::string_view{"match_reset"});
  CHECK(systems.systems_at(simulation::SystemStage::kLifecycle)[2].system->name() ==
        std::string_view{"lifetime_expiry"});
  CHECK(systems.systems_at(simulation::SystemStage::kLifecycle)[3].system->name() ==
        std::string_view{"hazard_spawn"});
  CHECK(systems.systems_at(simulation::SystemStage::kLifecycle)[4].system->name() ==
        std::string_view{"hill_rules_publisher"});
}

TEST_CASE("KingOfTheHillMode rejects a map with no hill marker or no spawn marker",
          "[unit][gameplay][king_of_the_hill][validation]") {
  CHECK_NOTHROW(default_mode().validate_map(hill_map("hill_valid_map")));

  try {
    default_mode().validate_map(testing::gameplay_map(4, "hill_no_hill_map"));
    FAIL("a map with no hill marker was accepted");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() ==
          gameplay::GameplayValidationCode::kKingOfTheHillMapWithoutHill);
    CHECK(error.detail().find("hill_no_hill_map") != std::string::npos);
  }

  const simulation::MapDefinition no_spawn = simulation::MapDefinition::create(
      "hill_no_spawn_map", simulation::ArenaBounds::create(960.0, 640.0), {},
      {simulation::MapDefinition::Marker::create("hill", simulation::Vector2::create(480.0, 320.0),
                                                 std::nullopt, simulation::MapMetadata::none())},
      simulation::MapMetadata::none());
  try {
    default_mode().validate_map(no_spawn);
    FAIL("a map with no spawn marker was accepted");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() ==
          gameplay::GameplayValidationCode::kKingOfTheHillMapWithoutSpawnPoint);
  }
}

TEST_CASE("king of the hill resolves through the one game mode registry",
          "[unit][gameplay][king_of_the_hill][registry]") {
  CHECK(gameplay::GameModeRegistry::contains("king_of_the_hill"));
  CHECK(gameplay::GameModeRegistry::create("king_of_the_hill")->name() ==
        std::string_view{"king_of_the_hill"});
  CHECK(gameplay::GameModeRegistry::registrations().size() == 4);
}

TEST_CASE("the hill entity is created on the first tick and the block is stamped with it",
          "[unit][gameplay][king_of_the_hill][mode_state]") {
  testing::SteppedGame driver{
      testing::gameplay_simulation(gameplay::KingOfTheHillMode::create(), hill_map("hill_first"))};
  const simulation::EntityId hill_entity = driver.next_entity_id();

  const simulation::WorldSnapshot first = driver.step();
  REQUIRE(first.components<simulation::Hill>().size() == 1);
  CHECK(first.components<simulation::Hill>()[0].entity == hill_entity);
  CHECK(first.components<simulation::Hill>()[0].value.center ==
        simulation::Vector2::create(100.0, 320.0));
  CHECK(first.components<simulation::Hill>()[0].value.radius == 90.0);
  CHECK(first.entities().size() == 1);
  CHECK(simulation::mode_match_state_schema_id_of(first.match().mode_state()) ==
        std::string_view{"king_of_the_hill"});
  const auto* held = std::get_if<simulation::KingOfTheHillModeState>(&first.match().mode_state());
  REQUIRE(held != nullptr);
  CHECK(held->points_to_win == 30);
  CHECK(held->point_interval_ticks == 400);
  CHECK(held->time_limit_ticks == 96'000);

  const simulation::WorldSnapshot later = driver.advance(8);
  CHECK(later.components<simulation::Hill>().size() == 1);
  CHECK(later.components<simulation::Hill>()[0].entity == hill_entity);
}

TEST_CASE("a seated player holding the hill scores to the threshold and wins",
          "[unit][gameplay][king_of_the_hill][match]") {
  // Two seats, two players; player 1 is seated on the hill marker, player 2 well outside the
  // 30 wu hill. With a two-tick interval and two points to win, player 1 wins on the fourth
  // running tick.
  testing::SteppedGame driver{
      testing::gameplay_simulation(gameplay::KingOfTheHillMode::create(quick_configuration()),
                                   hill_map("hill_match"), 0, testing::started_lobby(2))};

  // Tick 1: both spawn and are seated at points 1 and 2 (100 and 200 on x); lobby -> countdown.
  simulation::WorldSnapshot snapshot =
      driver.step({testing::spawn_command(1), testing::spawn_command(2)});
  CHECK(snapshot.match().phase() == simulation::MatchPhase::kCountdown);
  // Tick 2: countdown -> running with a zero countdown; scoring has not run yet.
  snapshot = driver.step();
  CHECK(snapshot.match().phase() == simulation::MatchPhase::kRunning);
  // Ticks 3 and 4: one point; ticks 5 and 6: the second, which decides the match on tick 6.
  snapshot = driver.advance(3);
  CHECK(snapshot.match().phase() == simulation::MatchPhase::kRunning);
  snapshot = driver.step();
  CHECK(snapshot.match().phase() == simulation::MatchPhase::kEnded);
  REQUIRE(snapshot.match().outcome().winning_entity().has_value());
  const simulation::EntityId winner = *snapshot.match().outcome().winning_entity();
  const simulation::Score* score = nullptr;
  for (const auto& entry : snapshot.components<simulation::Score>()) {
    if (entry.entity == winner) {
      score = &entry.value;
    }
  }
  REQUIRE(score != nullptr);
  CHECK(score->points == 2);
}
