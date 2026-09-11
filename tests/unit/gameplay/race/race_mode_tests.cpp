#include "race/race_mode.hpp"

#include "race/race_test_fixture.hpp"

#include "components/race_progress_component.hpp"
#include "components/respawn_timer_component.hpp"
#include "game_mode_registry.hpp"
#include "gameplay_validation_error.hpp"
#include "mode_states/no_mode_state.hpp"
#include "mode_states/race_mode_state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] std::vector<std::string_view>
system_names_at(const simulation::SystemPipeline& pipeline, const simulation::SystemStage stage) {
  std::vector<std::string_view> names;
  for (const auto& declaration : pipeline.systems_at(stage)) {
    names.push_back(declaration.system->name());
  }
  return names;
}

[[nodiscard]] const simulation::RaceModeState& block_of(const simulation::WorldSnapshot& snapshot) {
  const auto* block = std::get_if<simulation::RaceModeState>(&snapshot.match().mode_state());
  REQUIRE(block != nullptr);
  return *block;
}

[[nodiscard]] simulation::SystemPipeline declarations_after_mode_destruction() {
  auto mode = gameplay::RaceMode::create(testing::race_test_configuration());
  mode->validate_map(testing::race_test_map("owned_race_course"));
  auto systems = mode->systems();
  mode.reset();
  return systems;
}

} // namespace

TEST_CASE("RaceMode declares the fourth game with the shared contact rows and nine commands",
          "[unit][gameplay][race][mode]") {
  const gameplay::RaceMode mode{testing::race_test_configuration()};
  CHECK(mode.name() == std::string_view{"race"});
  const auto rules = mode.contact_rules();
  const auto built_in = simulation::ContactRuleTable::built_in();
  REQUIRE(rules.size() == built_in.size() + 1);
  CHECK(rules.rows()[0].name() == gameplay::kLethalHazardContactRuleName);
  for (std::size_t index = 0; index < built_in.size(); ++index) {
    CHECK(rules.rows()[index + 1] == built_in.rows()[index]);
  }
  CHECK(mode.accepted_command_kinds() ==
        simulation::CommandKindMask::create(
            {simulation::CommandKind::kSpawn, simulation::CommandKind::kDespawn,
             simulation::CommandKind::kThrust, simulation::CommandKind::kSetSeatCount,
             simulation::CommandKind::kClearSeat, simulation::CommandKind::kSeatNpc,
             simulation::CommandKind::kStartMatch, simulation::CommandKind::kLeave,
             simulation::CommandKind::kJoin}));
  CHECK(mode.spawn_policy() != nullptr);
  CHECK(mode.objective() != nullptr);
  CHECK(gameplay::GameModeRegistry::contains("race"));
  CHECK(gameplay::GameModeRegistry::create("race")->name() == std::string_view{"race"});
  CHECK(gameplay::GameModeRegistry::registrations().size() == 4);
}

TEST_CASE("RaceMode declares ten systems with progress before bounds and return before respawn",
          "[unit][gameplay][race][mode]") {
  const gameplay::RaceMode mode{testing::race_test_configuration()};
  mode.validate_map(testing::race_test_map());
  const auto systems = mode.systems();
  CHECK(systems.size() == 10);
  CHECK(system_names_at(systems, simulation::SystemStage::kPreKernel) ==
        std::vector<std::string_view>{"thrust_steering"});
  CHECK(system_names_at(systems, simulation::SystemStage::kPostKernel) ==
        std::vector<std::string_view>{"checkpoint_progress", "track_bounds"});
  CHECK(system_names_at(systems, simulation::SystemStage::kLifecycle) ==
        std::vector<std::string_view>{"standings_recorder", "checkpoint_respawn", "respawn",
                                      "match_reset", "lifetime_expiry", "hazard_spawn",
                                      "course_publisher"});
}

TEST_CASE("RaceMode requires a successfully bound course and failed revalidation clears it",
          "[unit][gameplay][race][mode][validation]") {
  const gameplay::RaceMode mode{testing::race_test_configuration()};
  const auto require_unbound = [&mode] {
    try {
      static_cast<void>(mode.systems());
      FAIL("an unbound course produced race systems");
    } catch (const gameplay::GameplayValidationError& error) {
      CHECK(error.validation_code() == gameplay::GameplayValidationCode::kRaceCourseUnbound);
      CHECK(error.code() == std::string_view{"GAMEPLAY.RACE_COURSE_UNBOUND"});
      CHECK(error.context() == "race_mode.systems");
    }
  };
  require_unbound();
  mode.validate_map(testing::race_test_map());
  CHECK_NOTHROW(mode.systems());
  CHECK_THROWS_AS(mode.validate_map(testing::gameplay_map(4, "race_without_track")),
                  gameplay::GameplayValidationError);
  require_unbound();
  mode.validate_map(testing::race_test_map("race_rebound"));
  CHECK_NOTHROW(mode.systems());
}

TEST_CASE("race systems own their course after both their source mode and source map are destroyed",
          "[unit][gameplay][race][mode]") {
  const auto systems = declarations_after_mode_destruction();
  // The context deliberately carries different gates: course-dependent rules read their owned
  // setup value, so publishing or progressing against the current context cannot hide a dangling
  // source map reference or reconstruction during ticks.
  const testing::TickHarness harness{
      simulation::TickSequence::create(3),
      testing::race_test_map("different_context_map", {simulation::Vector2::create(500.0, 320.0),
                                                       simulation::Vector2::create(700.0, 320.0)})};
  simulation::GameWorld world =
      testing::race_test_world({simulation::Vector2::create(300.0, 320.0)});
  for (const auto& declared : systems.systems_at(simulation::SystemStage::kPostKernel)) {
    declared.system->apply(world, harness.context());
  }
  for (const auto& declared : systems.systems_at(simulation::SystemStage::kLifecycle)) {
    declared.system->apply(world, harness.context());
  }
  REQUIRE(world.store<simulation::RaceProgress>().find(simulation::EntityId::create(1)) != nullptr);
  CHECK(world.store<simulation::RaceProgress>()
            .find(simulation::EntityId::create(1))
            ->next_checkpoint == 1);
  const auto* block = std::get_if<simulation::RaceModeState>(&world.match().mode_state);
  REQUIRE(block != nullptr);
  CHECK(block->checkpoints ==
        std::vector<simulation::Vector2>{simulation::Vector2::create(300.0, 320.0),
                                         simulation::Vector2::create(600.0, 320.0)});
}

TEST_CASE("a race publishes its course on the first lobby tick and finishes one overlapping gate "
          "per tick",
          "[unit][gameplay][race][mode][match]") {
  const auto configuration = testing::race_test_configuration();
  const auto map =
      testing::race_test_map("race_overlapping_start", {simulation::Vector2::create(100.0, 320.0),
                                                        simulation::Vector2::create(120.0, 320.0)});
  testing::SteppedGame lobby{
      testing::gameplay_simulation(gameplay::RaceMode::create(configuration), map)};
  const auto initial_lobby = lobby.game().snapshot();
  CHECK(initial_lobby.tick_sequence() == simulation::TickSequence::zero());
  CHECK(std::holds_alternative<simulation::NoModeState>(initial_lobby.match().mode_state()));
  const simulation::WorldSnapshot first = lobby.step();
  CHECK(first.match().phase() == simulation::MatchPhase::kLobby);
  CHECK(block_of(first).checkpoints.size() == 2);
  CHECK(block_of(first).time_limit_ticks == configuration.time_limit_ticks());
  CHECK(block_of(first).finish_window_ticks == configuration.finish_window_ticks());
  CHECK(block_of(first).road.value() == configuration.road());

  testing::SteppedGame race{testing::gameplay_simulation(gameplay::RaceMode::create(configuration),
                                                         map, 0, testing::started_lobby(1))};
  simulation::WorldSnapshot snapshot = race.step({testing::spawn_command(1)});
  CHECK(snapshot.match().phase() == simulation::MatchPhase::kCountdown);
  snapshot = race.step();
  CHECK(snapshot.match().phase() == simulation::MatchPhase::kRunning);
  CHECK(snapshot.components<simulation::RaceProgress>().empty());
  snapshot = race.step();
  REQUIRE(snapshot.components<simulation::RaceProgress>().size() == 1);
  CHECK(snapshot.components<simulation::RaceProgress>()[0].value.next_checkpoint == 1);
  CHECK(snapshot.match().phase() == simulation::MatchPhase::kRunning);
  snapshot = race.step();
  CHECK(snapshot.components<simulation::RaceProgress>()[0].value.next_checkpoint == 2);
  CHECK(snapshot.match().phase() == simulation::MatchPhase::kEnded);
  CHECK(snapshot.match().outcome() ==
        simulation::MatchOutcome::won_by_entity(simulation::EntityId::create(1)));
  REQUIRE(block_of(snapshot).standings.size() == 1);
  CHECK(block_of(snapshot).standings.front().finished_tick == simulation::TickSequence::create(4));
  CHECK(testing::published_body(snapshot, 1).has_value());
}

TEST_CASE("a same-tick race finish is recorded before an off-road elimination removes its body",
          "[unit][gameplay][race][mode][match]") {
  // The explicit map contract permits a gate centre on the corridor edge. This centre is inside
  // that gate but outside the corridor, exercising the specified progress -> bounds -> record
  // -> respawn order in one real kernel tick.
  const auto map = testing::race_test_map("race_finish_and_elimination",
                                          {simulation::Vector2::create(300.0, 390.0)});
  simulation::GameWorld world =
      testing::race_test_world({simulation::Vector2::create(300.0, 420.0)});
  testing::SteppedGame driver{simulation::GameSimulation::create(
      testing::gameplay_configuration(), std::move(world),
      simulation::GameSimulationSetup::of_mode(
          map, gameplay::RaceMode::create(testing::race_test_configuration())))};
  const auto initial = driver.game().snapshot();
  CHECK(initial.tick_sequence() == simulation::TickSequence::zero());
  CHECK(initial.match().phase() == simulation::MatchPhase::kRunning);
  CHECK(std::holds_alternative<simulation::NoModeState>(initial.match().mode_state()));
  const simulation::WorldSnapshot snapshot = driver.step();
  CHECK(snapshot.tick_sequence() == simulation::TickSequence::create(1));
  CHECK(block_of(snapshot).road.value() == "road");
  CHECK(snapshot.terrain().find_corridor(block_of(snapshot).road.value()) != nullptr);
  CHECK(block_of(snapshot).checkpoints ==
        std::vector<simulation::Vector2>{simulation::Vector2::create(300.0, 390.0)});
  REQUIRE(block_of(snapshot).standings.size() == 1);
  CHECK(block_of(snapshot).standings.front().entity == simulation::EntityId::create(1));
  CHECK(block_of(snapshot).standings.front().finished_tick == simulation::TickSequence::create(1));
  CHECK_FALSE(testing::published_body(snapshot, 1).has_value());
  CHECK(snapshot.components<simulation::RespawnTimer>().size() == 1);
  CHECK(snapshot.match().phase() == simulation::MatchPhase::kEnded);
  CHECK(snapshot.match().outcome() ==
        simulation::MatchOutcome::won_by_entity(simulation::EntityId::create(1)));
}
