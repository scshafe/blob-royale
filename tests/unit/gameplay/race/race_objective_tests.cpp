#include "race/race_objective.hpp"

#include "race/race_test_fixture.hpp"

#include "components/race_progress_component.hpp"
#include "mode_states/race_mode_state.hpp"
#include "race/race_mode_state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

constexpr std::uint64_t kRunningStart = 100;
constexpr std::uint64_t kTimeLimit = 100;
constexpr std::uint64_t kFinishWindow = 20;
constexpr std::uint64_t kFirstFinish = 190;

[[nodiscard]] simulation::EntityId entity(const std::uint64_t id) {
  return simulation::EntityId::create(id);
}

[[nodiscard]] gameplay::RaceObjective objective(const std::uint64_t window = kFinishWindow) {
  gameplay::RaceConfiguration::Section section = gameplay::RaceConfiguration::default_section();
  section.time_limit_seconds = static_cast<double>(kTimeLimit) / 400.0;
  section.finish_window_seconds = static_cast<double>(window) / 400.0;
  section.countdown_seconds = 0.01;
  section.restart_delay_seconds = 0.025;
  return gameplay::RaceObjective{gameplay::RaceConfiguration::create(section)};
}

// The shared race field with optional progress: nullopt preserves the absent component case.
[[nodiscard]] simulation::GameWorld
field(const std::initializer_list<std::optional<std::uint64_t>> progress) {
  std::vector<simulation::Vector2> positions;
  for (std::size_t index = 0; index < progress.size(); ++index) {
    positions.push_back(simulation::Vector2::create(100.0 * static_cast<double>(index + 1), 320.0));
  }
  simulation::GameWorld world = testing::race_test_world(positions);
  std::uint64_t id = 1;
  for (const std::optional<std::uint64_t> next_checkpoint : progress) {
    if (next_checkpoint.has_value()) {
      world.mutable_store<simulation::RaceProgress>().insert_or_assign(
          entity(id), simulation::RaceProgress{*next_checkpoint});
    }
    ++id;
  }
  world.mutable_match().running_started_tick = simulation::TickSequence::create(kRunningStart);
  return world;
}

[[nodiscard]] simulation::RaceStanding standing(const std::uint64_t id,
                                                const std::uint64_t placement,
                                                const std::uint64_t tick = kFirstFinish) {
  return simulation::RaceStanding{entity(id), simulation::ControllerId::create(id), placement,
                                  simulation::TickSequence::create(tick)};
}

[[nodiscard]] simulation::MatchOutcome outcome_at(const simulation::GameWorld& world,
                                                  const std::uint64_t tick,
                                                  const std::uint64_t window = kFinishWindow) {
  const testing::TickHarness harness{simulation::TickSequence::create(tick)};
  return objective(window).outcome(world, harness.context());
}

} // namespace

TEST_CASE("race starts through the shared lobby rule and publishes its configured durations",
          "[unit][gameplay][race][objective]") {
  simulation::GameWorld world = field({0, 0});
  CHECK_FALSE(objective().can_start(world));
  world.mutable_match().seats = testing::started_lobby(2);
  CHECK(objective().can_start(world));
  world.mutable_match().seats.clear_start_request();
  CHECK_FALSE(objective().can_start(world));
  CHECK(objective().durations().countdown_ticks == 4);
  CHECK(objective().durations().restart_delay_ticks == 10);
}

TEST_CASE("a solo race is a time trial until a finish or the clock and an empty race draws",
          "[unit][gameplay][race][objective]") {
  simulation::GameWorld world = field({std::nullopt});
  CHECK(outcome_at(world, 199) == simulation::MatchOutcome::undecided());
  CHECK(outcome_at(world, 200) == simulation::MatchOutcome::won_by_entity(entity(1)));
  gameplay::race_mode_state_in(world).standings = {standing(1, 1)};
  CHECK(outcome_at(world, kFirstFinish) == simulation::MatchOutcome::won_by_entity(entity(1)));
  world.destroy_entity(entity(1));
  CHECK(outcome_at(world, kFirstFinish) == simulation::MatchOutcome::drawn());
}

TEST_CASE("the race clock ranks gates with absent progress as zero and shared leaders drawing",
          "[unit][gameplay][race][objective]") {
  CHECK(outcome_at(field({1, std::nullopt}), 199) == simulation::MatchOutcome::undecided());
  CHECK(outcome_at(field({1, std::nullopt}), 200) ==
        simulation::MatchOutcome::won_by_entity(entity(1)));
  CHECK(outcome_at(field({std::nullopt, 1}), 200) ==
        simulation::MatchOutcome::won_by_entity(entity(2)));
  CHECK(outcome_at(field({std::nullopt, 0}), 200) == simulation::MatchOutcome::drawn());
  CHECK(outcome_at(field({1, 1, 0}), 200) == simulation::MatchOutcome::drawn());
}

TEST_CASE("a respawning participant can lead the race at the clock",
          "[unit][gameplay][race][objective]") {
  simulation::GameWorld world = field({1, 0});
  world.mutable_store<simulation::PhysicsBody>().erase(entity(1));
  CHECK(outcome_at(world, 199) == simulation::MatchOutcome::undecided());
  CHECK(outcome_at(world, 200) == simulation::MatchOutcome::won_by_entity(entity(1)));
}

TEST_CASE("a recorded finish makes the finish window take precedence over the total race clock",
          "[unit][gameplay][race][objective]") {
  simulation::GameWorld world = field({2, 1});
  gameplay::race_mode_state_in(world).standings = {standing(1, 1)};
  CHECK(outcome_at(world, 200) == simulation::MatchOutcome::undecided());
  CHECK(outcome_at(world, 209) == simulation::MatchOutcome::undecided());
  CHECK(outcome_at(world, 210) == simulation::MatchOutcome::won_by_entity(entity(1)));
  CHECK(outcome_at(world, kFirstFinish, 0) == simulation::MatchOutcome::won_by_entity(entity(1)));
}

TEST_CASE("race completion decides early when every participant is recorded",
          "[unit][gameplay][race][objective]") {
  simulation::GameWorld world = field({2, 2});
  gameplay::race_mode_state_in(world).standings = {standing(1, 1), standing(2, 2, 191)};
  CHECK(outcome_at(world, 191) == simulation::MatchOutcome::won_by_entity(entity(1)));
  gameplay::race_mode_state_in(world).standings = {standing(1, 1), standing(2, 1)};
  CHECK(outcome_at(world, kFirstFinish) == simulation::MatchOutcome::drawn());
}

TEST_CASE("shared first finishers draw when the window closes with another racer unfinished",
          "[unit][gameplay][race][objective]") {
  simulation::GameWorld world = field({2, 2, 0});
  gameplay::race_mode_state_in(world).standings = {standing(1, 1), standing(2, 1)};
  CHECK(outcome_at(world, 209) == simulation::MatchOutcome::undecided());
  CHECK(outcome_at(world, 210) == simulation::MatchOutcome::drawn());
}

TEST_CASE("a disconnected first finisher retains the recorded win while participants remain",
          "[unit][gameplay][race][objective]") {
  simulation::GameWorld world = field({2, 0});
  gameplay::race_mode_state_in(world).standings = {standing(1, 1)};
  world.destroy_entity(entity(1));
  CHECK(outcome_at(world, 209) == simulation::MatchOutcome::undecided());
  CHECK(outcome_at(world, 210) == simulation::MatchOutcome::won_by_entity(entity(1)));
}

TEST_CASE("race elapsed tick subtraction saturates before the recorded start or finish",
          "[unit][gameplay][race][objective]") {
  simulation::GameWorld world = field({1, 0});
  CHECK(outcome_at(world, kRunningStart - 1) == simulation::MatchOutcome::undecided());
  gameplay::race_mode_state_in(world).standings = {standing(1, 1)};
  CHECK(outcome_at(world, kFirstFinish - 1) == simulation::MatchOutcome::undecided());
}
