#include "race/standings_recorder_system.hpp"

#include "race/race_test_fixture.hpp"

#include "components/race_progress_component.hpp"
#include "events/race_checkpoint_event.hpp"
#include "gameplay_validation_error.hpp"
#include "mode_states/race_mode_state.hpp"
#include "race/race_mode_state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

void finish(simulation::GameWorld& world, const std::uint64_t entity,
            const simulation::MotionTime offset = simulation::MotionTime::start()) {
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(
      simulation::EntityId::create(entity), simulation::RaceProgress{2});
  world.emit(simulation::RaceCheckpointEvent{simulation::EntityId::create(entity), 2, offset});
}

[[nodiscard]] simulation::RaceStanding
standing(const std::uint64_t entity, const std::uint64_t placement, const std::uint64_t tick,
         const simulation::MotionTime offset = simulation::MotionTime::start()) {
  return simulation::RaceStanding{simulation::EntityId::create(entity),
                                  simulation::ControllerId::create(entity), placement,
                                  simulation::TickSequence::create(tick), offset};
}

} // namespace

TEST_CASE(
    "race finishers at one certified time share placement and are recorded once in entity order",
    "[unit][gameplay][race][standings]") {
  const auto map = testing::race_test_map();
  const auto course = gameplay::RaceCourse::create(map, testing::race_test_configuration());
  const auto system = gameplay::StandingsRecorderSystem::create(course);
  const std::vector<simulation::Vector2> positions{simulation::Vector2::create(100.0, 320.0),
                                                   simulation::Vector2::create(200.0, 320.0),
                                                   simulation::Vector2::create(300.0, 320.0)};
  simulation::GameWorld world = testing::race_test_world(positions);
  finish(world, 3);
  finish(world, 1);
  const testing::TickHarness first{simulation::TickSequence::create(10)};
  system->apply(world, first.context());
  const auto& standings = gameplay::race_mode_state_in(world, course).standings;
  CHECK(standings == std::vector<simulation::RaceStanding>{standing(1, 1, 10), standing(3, 1, 10)});
  CHECK(world.store<simulation::PhysicsBody>().entries().size() == 3);
  system->apply(world, first.context());
  CHECK(standings == std::vector<simulation::RaceStanding>{standing(1, 1, 10), standing(3, 1, 10)});

  // A new tick retains observed standings/progress but not the previous tick's event list.
  // Construct that boundary explicitly; only GameSimulation may close a world's tick.
  simulation::GameWorld next_world = testing::race_test_world(positions);
  gameplay::race_mode_state_in(next_world, course).standings = standings;
  next_world.mutable_store<simulation::RaceProgress>() = world.store<simulation::RaceProgress>();
  finish(next_world, 2);
  const testing::TickHarness next{simulation::TickSequence::create(11)};
  system->apply(next_world, next.context());
  const auto& next_standings = gameplay::race_mode_state_in(next_world, course).standings;
  CHECK(next_standings == std::vector<simulation::RaceStanding>{
                              standing(1, 1, 10), standing(3, 1, 10), standing(2, 3, 11)});
  next_world.destroy_entity(simulation::EntityId::create(1));
  CHECK(next_standings.front().controller == simulation::ControllerId::create(1));
}

TEST_CASE(
    "race standings survive other phases and clear on the first running tick before recording",
    "[unit][gameplay][race][standings]") {
  const auto map = testing::race_test_map();
  const auto course = gameplay::RaceCourse::create(map, testing::race_test_configuration());
  const auto system = gameplay::StandingsRecorderSystem::create(course);
  const testing::TickHarness harness{simulation::TickSequence::create(20)};
  simulation::GameWorld world =
      testing::race_test_world({simulation::Vector2::create(600.0, 320.0)});
  gameplay::race_mode_state_in(world, course).standings = {standing(9, 1, 10)};
  finish(world, 1);
  for (const simulation::MatchPhase phase :
       {simulation::MatchPhase::kEnded, simulation::MatchPhase::kLobby,
        simulation::MatchPhase::kCountdown}) {
    world.mutable_match().phase = phase;
    system->apply(world, harness.context());
    CHECK(gameplay::race_mode_state_in(world, course).standings ==
          std::vector<simulation::RaceStanding>{standing(9, 1, 10)});
  }
  world.mutable_match().phase = simulation::MatchPhase::kRunning;
  world.mutable_match().previous_phase = simulation::MatchPhase::kCountdown;
  system->apply(world, harness.context());
  CHECK(gameplay::race_mode_state_in(world, course).standings ==
        std::vector<simulation::RaceStanding>{standing(1, 1, 20)});
}

TEST_CASE("a bodyless racer holding finished progress is not a newly observed finisher",
          "[unit][gameplay][race][standings]") {
  const auto map = testing::race_test_map();
  const auto system = gameplay::StandingsRecorderSystem::create(
      gameplay::RaceCourse::create(map, testing::race_test_configuration()));
  const testing::TickHarness harness{simulation::TickSequence::create(10)};
  simulation::GameWorld world =
      testing::race_test_world({simulation::Vector2::create(600.0, 320.0)});
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(simulation::EntityId::create(1),
                                                                   simulation::RaceProgress{2});
  world.mutable_store<simulation::PhysicsBody>().erase(simulation::EntityId::create(1));
  system->apply(world, harness.context());
  CHECK(gameplay::race_standings_of(world).empty());
}

TEST_CASE("distinct same-tick certified times rank before identity with no epsilon ties",
          "[unit][gameplay][race][standings]") {
  const auto course =
      gameplay::RaceCourse::create(testing::race_test_map(), testing::race_test_configuration());
  const auto system = gameplay::StandingsRecorderSystem::create(course);
  auto world = testing::race_test_world({simulation::Vector2::create(100.0, 320.0),
                                         simulation::Vector2::create(200.0, 320.0),
                                         simulation::Vector2::create(300.0, 320.0)});
  const auto early = simulation::MotionTime::create(0.25);
  const auto adjacent = simulation::MotionTime::create(std::nextafter(early.value(), 1.0));
  finish(world, 1, adjacent);
  finish(world, 3, early);
  finish(world, 2, early);
  const testing::TickHarness harness{simulation::TickSequence::create(10)};
  system->apply(world, harness.context());
  CHECK(gameplay::race_mode_state_in(world, course).standings ==
        std::vector<simulation::RaceStanding>{standing(2, 1, 10, early), standing(3, 1, 10, early),
                                              standing(1, 3, 10, adjacent)});
  system->apply(world, harness.context());
  CHECK(gameplay::race_standings_of(world).size() == 3);
}

TEST_CASE("race standings cannot exceed the protocol player limit",
          "[unit][gameplay][race][standings][validation]") {
  const auto map = testing::race_test_map();
  const auto course = gameplay::RaceCourse::create(map, testing::race_test_configuration());
  const auto system = gameplay::StandingsRecorderSystem::create(course);
  const testing::TickHarness harness{simulation::TickSequence::create(10)};
  simulation::GameWorld world =
      testing::race_test_world({simulation::Vector2::create(600.0, 320.0)});
  finish(world, 1);
  auto& standings = gameplay::race_mode_state_in(world, course).standings;
  for (std::uint64_t index = 0; index < simulation::kMaximumPlayerCount; ++index) {
    standings.push_back(standing(index + 2, index + 1, 9));
  }
  try {
    system->apply(world, harness.context());
    FAIL("race standings exceeded their accepted bound");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() == gameplay::GameplayValidationCode::kRaceStandingLimitExceeded);
    CHECK(error.code() == std::string_view{"GAMEPLAY.RACE_STANDING_LIMIT_EXCEEDED"});
  }
}

TEST_CASE("a certified finish without its controller identity fails visibly",
          "[unit][gameplay][race][standings][validation]") {
  const auto course =
      gameplay::RaceCourse::create(testing::race_test_map(), testing::race_test_configuration());
  const auto system = gameplay::StandingsRecorderSystem::create(course);
  auto world = testing::race_test_world({simulation::Vector2::create(600.0, 320.0)});
  finish(world, 1);
  world.mutable_store<simulation::Controllable>().erase(simulation::EntityId::create(1));
  const testing::TickHarness harness{simulation::TickSequence::create(10)};
  try {
    system->apply(world, harness.context());
    FAIL("finish without controller identity was accepted");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() == gameplay::GameplayValidationCode::kRaceFinishEventInvalid);
  }
}
