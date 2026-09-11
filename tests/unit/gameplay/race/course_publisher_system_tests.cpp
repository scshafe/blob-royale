#include "race/course_publisher_system.hpp"

#include "race/race_test_fixture.hpp"

#include "mode_states/race_mode_state.hpp"
#include "mode_states/royale_placements_mode_state.hpp"
#include "race/race_mode.hpp"
#include "race/race_mode_state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

TEST_CASE(
    "the race publisher stamps declared course and clocks while preserving observed standings",
    "[unit][gameplay][race][mode_state]") {
  const auto configuration = testing::race_test_configuration();
  const auto map = testing::race_test_map();
  const auto course = gameplay::RaceCourse::create(map, configuration);
  const auto* road = map.terrain().find_corridor(configuration.road());
  REQUIRE(road != nullptr);
  const auto system = gameplay::CoursePublisherSystem::create(course, configuration);
  const testing::TickHarness harness{simulation::TickSequence::create(10)};
  simulation::GameWorld world = testing::race_test_world({});
  const simulation::RaceStanding recorded{simulation::EntityId::create(1),
                                          simulation::ControllerId::create(7), 1,
                                          simulation::TickSequence::create(5)};
  gameplay::race_mode_state_in(world, course).standings = {recorded};
  for (const simulation::MatchPhase phase :
       {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
        simulation::MatchPhase::kRunning, simulation::MatchPhase::kEnded}) {
    world.mutable_match().phase = phase;
    auto& block = gameplay::race_mode_state_in(world, course);
    block.road = simulation::RaceRoadName::create("another_road");
    block.checkpoint_radius = 0.0;
    system->apply(world, harness.context());
    CHECK(block.road.value() == road->name());
    CHECK(block.checkpoint_radius == configuration.checkpoint_radius());
    CHECK(block.checkpoints == std::vector<simulation::Vector2>{course.checkpoints().begin(),
                                                                course.checkpoints().end()});
    CHECK(block.time_limit_ticks == configuration.time_limit_ticks());
    CHECK(block.finish_window_ticks == configuration.finish_window_ticks());
    CHECK(block.standings == std::vector<simulation::RaceStanding>{recorded});
  }
}

TEST_CASE("an uninitialized race has no standings and its first publisher installs the race block",
          "[unit][gameplay][race][mode_state]") {
  const auto configuration = testing::race_test_configuration();
  const auto map = testing::race_test_map();
  const auto course = gameplay::RaceCourse::create(map, configuration);
  const auto system = gameplay::CoursePublisherSystem::create(course, configuration);
  const testing::TickHarness harness{simulation::TickSequence::create(1)};
  simulation::GameWorld world = testing::race_test_world({}, simulation::MatchPhase::kLobby);
  CHECK(gameplay::race_standings_of(world).empty());
  world.mutable_match().mode_state = simulation::RoyalePlacementsModeState{};
  CHECK(gameplay::race_standings_of(world).empty());
  system->apply(world, harness.context());
  CHECK(simulation::mode_match_state_schema_id_of(world.match().mode_state) ==
        std::string_view{"race"});
  CHECK(gameplay::race_mode_state_in(world, course).road.value() == course.road_name());
  CHECK(gameplay::race_standings_of(world).empty());
}

TEST_CASE("every committed race tick publishes its terrain road identity without changing gates",
          "[unit][gameplay][race][mode_state][terrain]") {
  const auto configuration = testing::race_test_configuration();
  const auto map = testing::race_test_map();
  const auto* road = map.terrain().find_corridor(configuration.road());
  REQUIRE(road != nullptr);
  testing::SteppedGame driver{simulation::GameSimulation::create(
      testing::gameplay_configuration(), testing::race_test_world({}),
      simulation::GameSimulationSetup::of_mode(map, gameplay::RaceMode::create(configuration)))};
  for (std::uint64_t tick = 1; tick <= 5; ++tick) {
    const auto snapshot = driver.step();
    const auto* block = std::get_if<simulation::RaceModeState>(&snapshot.match().mode_state());
    REQUIRE(block != nullptr);
    CHECK(snapshot.tick_sequence() == simulation::TickSequence::create(tick));
    CHECK(block->road.value() == road->name());
    CHECK(snapshot.terrain().find_corridor(block->road.value()) != nullptr);
    CHECK(block->checkpoint_radius == configuration.checkpoint_radius());
  }
}

TEST_CASE("race publication preserves the configured identity across reordered terrain corridors",
          "[unit][gameplay][race][mode_state][terrain]") {
  const auto base = testing::race_test_map();
  auto section = gameplay::RaceConfiguration::default_section();
  section.road = "race_route";
  const auto configuration = gameplay::RaceConfiguration::create(section);
  const std::vector<simulation::Vector2> points{simulation::Vector2::create(100.0, 320.0),
                                               simulation::Vector2::create(800.0, 320.0)};
  for (const bool selected_first : {false, true}) {
    std::vector<simulation::TerrainCorridor> corridors{
        simulation::TerrainCorridor::create("unselected", 10.0, points),
        simulation::TerrainCorridor::create("race_route", testing::kRaceTestRoadHalfWidth, points)};
    if (selected_first) {
      std::swap(corridors[0], corridors[1]);
    }
    const auto map = simulation::MapDefinition::create(
        "race_named_publication",
        simulation::TerrainDefinition::create(base.terrain().bounds(),
                                               simulation::TerrainGround::kCorridors,
                                               std::move(corridors), {}),
        {}, {base.markers().begin(), base.markers().end()}, simulation::MapMetadata::none());
    const auto course = gameplay::RaceCourse::create(map, configuration);
    simulation::GameWorld world = testing::race_test_world({}, simulation::MatchPhase::kLobby);
    const auto& initialized = gameplay::race_mode_state_in(world, course);
    CHECK(initialized.road.value() == "race_route");
    CHECK(initialized.standings.empty());
    const auto publisher = gameplay::CoursePublisherSystem::create(course, configuration);
    const testing::TickHarness harness{simulation::TickSequence::create(1), map};
    publisher->apply(world, harness.context());
    CHECK(initialized.road.value() == "race_route");
    CHECK(initialized.checkpoint_radius == configuration.checkpoint_radius());
    CHECK(initialized.checkpoints == std::vector<simulation::Vector2>{course.checkpoints().begin(),
                                                                      course.checkpoints().end()});
  }
}
