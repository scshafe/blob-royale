#include "race/standings_recorder_system.hpp"

#include "race/race_test_fixture.hpp"

#include "components/race_progress_component.hpp"
#include "gameplay_validation_error.hpp"
#include "mode_states/race_mode_state.hpp"
#include "race/race_mode_state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

void finish(simulation::GameWorld& world, const std::uint64_t entity) {
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(
      simulation::EntityId::create(entity), simulation::RaceProgress{2});
}

[[nodiscard]] simulation::RaceStanding
standing(const std::uint64_t entity, const std::uint64_t placement, const std::uint64_t tick) {
  return simulation::RaceStanding{simulation::EntityId::create(entity),
                                  simulation::ControllerId::create(entity), placement,
                                  simulation::TickSequence::create(tick)};
}

} // namespace

TEST_CASE("race finishers on one tick share a placement and are recorded only once in entity order",
          "[unit][gameplay][race][standings]") {
  const auto map = testing::race_test_map();
  const auto system = gameplay::StandingsRecorderSystem::create(
      gameplay::RaceCourse::create(map, testing::race_test_configuration()));
  simulation::GameWorld world = testing::race_test_world(
      {simulation::Vector2::create(100.0, 320.0), simulation::Vector2::create(200.0, 320.0),
       simulation::Vector2::create(300.0, 320.0)});
  finish(world, 3);
  finish(world, 1);
  const testing::TickHarness first{simulation::TickSequence::create(10)};
  system->apply(world, first.context());
  const auto& standings = gameplay::race_mode_state_in(world).standings;
  CHECK(standings == std::vector<simulation::RaceStanding>{standing(1, 1, 10), standing(3, 1, 10)});
  CHECK(world.store<simulation::PhysicsBody>().entries().size() == 3);

  finish(world, 2);
  const testing::TickHarness next{simulation::TickSequence::create(11)};
  system->apply(world, next.context());
  CHECK(standings == std::vector<simulation::RaceStanding>{standing(1, 1, 10), standing(3, 1, 10),
                                                           standing(2, 3, 11)});
  world.destroy_entity(simulation::EntityId::create(1));
  CHECK(standings.front().controller == simulation::ControllerId::create(1));
}

TEST_CASE(
    "race standings survive other phases and clear on the first running tick before recording",
    "[unit][gameplay][race][standings]") {
  const auto map = testing::race_test_map();
  const auto system = gameplay::StandingsRecorderSystem::create(
      gameplay::RaceCourse::create(map, testing::race_test_configuration()));
  const testing::TickHarness harness{simulation::TickSequence::create(20)};
  simulation::GameWorld world =
      testing::race_test_world({simulation::Vector2::create(600.0, 320.0)});
  gameplay::race_mode_state_in(world).standings = {standing(9, 1, 10)};
  finish(world, 1);
  for (const simulation::MatchPhase phase :
       {simulation::MatchPhase::kEnded, simulation::MatchPhase::kLobby,
        simulation::MatchPhase::kCountdown}) {
    world.mutable_match().phase = phase;
    system->apply(world, harness.context());
    CHECK(gameplay::race_mode_state_in(world).standings ==
          std::vector<simulation::RaceStanding>{standing(9, 1, 10)});
  }
  world.mutable_match().phase = simulation::MatchPhase::kRunning;
  world.mutable_match().previous_phase = simulation::MatchPhase::kCountdown;
  system->apply(world, harness.context());
  CHECK(gameplay::race_mode_state_in(world).standings ==
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
  finish(world, 1);
  world.mutable_store<simulation::PhysicsBody>().erase(simulation::EntityId::create(1));
  system->apply(world, harness.context());
  CHECK(gameplay::race_standings_of(world).empty());
}

TEST_CASE("race standings cannot exceed the protocol player limit",
          "[unit][gameplay][race][standings][validation]") {
  const auto map = testing::race_test_map();
  const auto system = gameplay::StandingsRecorderSystem::create(
      gameplay::RaceCourse::create(map, testing::race_test_configuration()));
  const testing::TickHarness harness{simulation::TickSequence::create(10)};
  simulation::GameWorld world =
      testing::race_test_world({simulation::Vector2::create(600.0, 320.0)});
  finish(world, 1);
  auto& standings = gameplay::race_mode_state_in(world).standings;
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
