#include "king_of_the_hill/hill_objective.hpp"

#include "gameplay_test_fixture.hpp"

#include "components/controllable_component.hpp"
#include "components/score_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "match_outcome.hpp"
#include "match_state.hpp"
#include "physics_body.hpp"
#include "seat_roster.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] simulation::EntityId entity(const std::uint64_t id) {
  return simulation::EntityId::create(id);
}

// `participant_count` participants numbered from 1, each alive, with the scores given (a missing
// score reads as zero). Running began on tick 100.
[[nodiscard]] simulation::GameWorld field(const std::vector<std::int64_t>& scores) {
  std::vector<simulation::GameWorld::EntitySeed> seeds;
  for (std::size_t index = 0; index < scores.size(); ++index) {
    seeds.push_back(simulation::GameWorld::EntitySeed::create(
        entity(index + 1),
        simulation::PhysicsBody::create(
            simulation::Vector2::create(60.0 * static_cast<double>(index + 1), 60.0),
            simulation::Vector2::create(0.0, 0.0), simulation::Vector2::create(0.0, 0.0)),
        simulation::ControllerId::create(index + 1)));
  }
  simulation::GameWorld world = simulation::GameWorld::create(std::move(seeds));
  for (std::size_t index = 0; index < scores.size(); ++index) {
    if (scores[index] != 0) {
      world.mutable_store<simulation::Score>().insert_or_assign(entity(index + 1),
                                                                simulation::Score{scores[index]});
    }
  }
  world.mutable_match().running_started_tick = simulation::TickSequence::create(100);
  return world;
}

// points_to_win 5, time limit 1,000 ticks (2.5 s).
[[nodiscard]] gameplay::HillObjective objective() {
  gameplay::KingOfTheHillConfiguration::Section section =
      gameplay::KingOfTheHillConfiguration::default_section();
  section.points_to_win = 5;
  section.time_limit_seconds = 2.5;
  return gameplay::HillObjective{gameplay::KingOfTheHillConfiguration::create(section)};
}

[[nodiscard]] simulation::MatchOutcome outcome_at(const simulation::GameWorld& world,
                                                  const std::uint64_t tick_sequence) {
  const testing::TickHarness harness{simulation::TickSequence::create(tick_sequence)};
  return objective().outcome(world, harness.context());
}

} // namespace

TEST_CASE("the hill can start exactly when every seat is filled and a start was requested",
          "[unit][gameplay][king_of_the_hill][objective]") {
  simulation::GameWorld world = field({0, 0});
  world.mutable_match().seats = testing::started_lobby(2);
  CHECK(objective().can_start(world));
  world.mutable_match().seats.clear_start_request();
  CHECK_FALSE(objective().can_start(world));
}

TEST_CASE("the first to the threshold wins, and two reaching it on one tick is a draw",
          "[unit][gameplay][king_of_the_hill][objective]") {
  CHECK(outcome_at(field({4, 3}), 200) == simulation::MatchOutcome::undecided());
  CHECK(outcome_at(field({5, 3}), 200) == simulation::MatchOutcome::won_by_entity(entity(1)));
  CHECK(outcome_at(field({3, 7}), 200) == simulation::MatchOutcome::won_by_entity(entity(2)));
  CHECK(outcome_at(field({5, 5}), 200) == simulation::MatchOutcome::drawn());
  // A third player short of the threshold does not turn a shared threshold into a win.
  CHECK(outcome_at(field({6, 6, 2}), 200) == simulation::MatchOutcome::drawn());
}

TEST_CASE("the clock ranks the leader, and a level scoreboard at the clock is a draw",
          "[unit][gameplay][king_of_the_hill][objective]") {
  // Running began on tick 100 and the limit is 1,000 ticks, so tick 1,100 is the first decided
  // tick.
  CHECK(outcome_at(field({2, 1}), 1'099) == simulation::MatchOutcome::undecided());
  CHECK(outcome_at(field({2, 1}), 1'100) == simulation::MatchOutcome::won_by_entity(entity(1)));
  CHECK(outcome_at(field({1, 2}), 1'100) == simulation::MatchOutcome::won_by_entity(entity(2)));
  CHECK(outcome_at(field({0, 0}), 1'100) == simulation::MatchOutcome::drawn());
  CHECK(outcome_at(field({3, 3, 1}), 5'000) == simulation::MatchOutcome::drawn());
}

TEST_CASE("a field of one is decided before the scoreboard is read, and an empty field is a draw",
          "[unit][gameplay][king_of_the_hill][objective]") {
  CHECK(outcome_at(field({0}), 200) == simulation::MatchOutcome::won_by_entity(entity(1)));
  CHECK(outcome_at(field({}), 200) == simulation::MatchOutcome::drawn());
}

TEST_CASE("the field is counted in participants, so a respawning player still holds the match open",
          "[unit][gameplay][king_of_the_hill][objective]") {
  simulation::GameWorld world = field({4, 1});
  // Player 2 is out of play: the controller stays, the body is gone.
  world.mutable_store<simulation::PhysicsBody>().erase(entity(2));
  const testing::TickHarness harness{simulation::TickSequence::create(200)};
  CHECK(objective().outcome(world, harness.context()) == simulation::MatchOutcome::undecided());
}

TEST_CASE("the objective's durations are the configured tick counts",
          "[unit][gameplay][king_of_the_hill][objective]") {
  CHECK(objective().durations().countdown_ticks == 2'000);
  CHECK(objective().durations().restart_delay_ticks == 3'200);
}
