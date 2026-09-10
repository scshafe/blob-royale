#include "race/course_publisher_system.hpp"

#include "race/race_test_fixture.hpp"

#include "mode_states/race_mode_state.hpp"
#include "mode_states/royale_placements_mode_state.hpp"
#include "race/race_mode_state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>
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
  const auto system = gameplay::CoursePublisherSystem::create(course, configuration);
  const testing::TickHarness harness{simulation::TickSequence::create(10)};
  simulation::GameWorld world = testing::race_test_world({});
  const simulation::RaceStanding recorded{simulation::EntityId::create(1),
                                          simulation::ControllerId::create(7), 1,
                                          simulation::TickSequence::create(5)};
  gameplay::race_mode_state_in(world).standings = {recorded};
  for (const simulation::MatchPhase phase :
       {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
        simulation::MatchPhase::kRunning, simulation::MatchPhase::kEnded}) {
    world.mutable_match().phase = phase;
    auto& block = gameplay::race_mode_state_in(world);
    block.track.clear();
    block.checkpoint_radius = 0.0;
    system->apply(world, harness.context());
    CHECK(block.track_half_width == configuration.track_half_width());
    CHECK(block.checkpoint_radius == configuration.checkpoint_radius());
    CHECK(block.track ==
          std::vector<simulation::Vector2>{course.track().begin(), course.track().end()});
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
  const auto system = gameplay::CoursePublisherSystem::create(
      gameplay::RaceCourse::create(map, configuration), configuration);
  const testing::TickHarness harness{simulation::TickSequence::create(1)};
  simulation::GameWorld world = testing::race_test_world({}, simulation::MatchPhase::kLobby);
  CHECK(gameplay::race_standings_of(world).empty());
  world.mutable_match().mode_state = simulation::RoyalePlacementsModeState{};
  CHECK(gameplay::race_standings_of(world).empty());
  system->apply(world, harness.context());
  CHECK(simulation::mode_match_state_schema_id_of(world.match().mode_state) ==
        std::string_view{"race"});
  CHECK(gameplay::race_mode_state_in(world).track.size() == 2);
  CHECK(gameplay::race_standings_of(world).empty());
}
