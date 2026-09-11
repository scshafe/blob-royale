#include "replay_fixture.hpp"

#include "components/controllable_component.hpp"
#include "components/race_progress_component.hpp"
#include "components/respawn_timer_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "match_outcome.hpp"
#include "match_phase.hpp"
#include "mode_states/race_mode_state.hpp"
#include "physics_body.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

using Snapshots = std::vector<simulation::WorldSnapshot>;

[[nodiscard]] const simulation::WorldSnapshot& at_tick(const Snapshots& snapshots,
                                                       const std::uint64_t tick) {
  REQUIRE(tick >= 1);
  REQUIRE(tick <= snapshots.size());
  return snapshots[static_cast<std::size_t>(tick) - 1];
}

template <typename Component>
[[nodiscard]] std::optional<Component> component_of(const simulation::WorldSnapshot& snapshot,
                                                    const simulation::EntityId entity) {
  for (const auto& entry : snapshot.components<Component>()) {
    if (entry.entity == entity) {
      return entry.value;
    }
  }
  return std::nullopt;
}

[[nodiscard]] const simulation::RaceModeState& race_of(const simulation::WorldSnapshot& snapshot) {
  const auto* race = std::get_if<simulation::RaceModeState>(&snapshot.match().mode_state());
  REQUIRE(race != nullptr);
  return *race;
}

[[nodiscard]] std::optional<std::uint64_t>
first_tick_with_progress(const Snapshots& snapshots, const simulation::EntityId entity,
                         const std::uint64_t next_checkpoint) {
  for (std::uint64_t tick = 1; tick <= snapshots.size(); ++tick) {
    const auto progress = component_of<simulation::RaceProgress>(at_tick(snapshots, tick), entity);
    if (progress.has_value() && progress->next_checkpoint == next_checkpoint) {
      return tick;
    }
  }
  return std::nullopt;
}

// Each named case below gets its own timeout and its own 100 fresh simulations. Equality is
// supplementary to the independently derived motion and lifecycle assertions, not their oracle.
void require_one_hundred_identical_runs(const std::string& fixture_name) {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named(fixture_name);
  const Snapshots reference = fixture.run();
  REQUIRE(reference.size() == fixture.tick_count());
  for (std::size_t run = 2; run <= 100; ++run) {
    const std::size_t divergent_tick = testing::first_divergent_tick(reference, fixture.run());
    INFO(fixture_name << ": run " << run << ", first divergent tick " << divergent_tick);
    REQUIRE(divergent_tick == 0);
  }
}

} // namespace

TEST_CASE("the replay format reads the race section and publishes its course from tick one",
          "[fixtures][replay][race]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("race-scripted-course");
  CHECK(fixture.mode_name() == "race");
  CHECK(fixture.tick_count() == 130);
  CHECK(fixture.race().thrust_maximum() == 4000.0);
  CHECK(fixture.race().respawn_delay_ticks() == 8);
  CHECK(fixture.race().finish_window_ticks() == 8);
  CHECK(fixture.race().time_limit_ticks() == 400);
  CHECK(fixture.royale().zone_shrink_ticks() == 36'000);

  const Snapshots snapshots = fixture.run();
  for (const auto& snapshot : snapshots) {
    const auto& race = race_of(snapshot);
    CHECK(race.road.value() == fixture.race().road());
    const auto* road = snapshot.terrain().find_corridor(race.road.value());
    REQUIRE(road != nullptr);
    CHECK(road->half_width() == 20.0);
    CHECK(race.checkpoint_radius == 5.0);
    CHECK(race.time_limit_ticks == 400);
    CHECK(race.finish_window_ticks == 8);
    CHECK(std::vector<simulation::Vector2>(road->points().begin(), road->points().end()) ==
          std::vector<simulation::Vector2>{simulation::Vector2::create(100.0, 320.0),
                                           simulation::Vector2::create(140.0, 320.0),
                                           simulation::Vector2::create(140.0, 280.0)});
    CHECK(race.checkpoints ==
          std::vector<simulation::Vector2>{simulation::Vector2::create(110.0, 320.0),
                                           simulation::Vector2::create(140.0, 320.0),
                                           simulation::Vector2::create(140.0, 290.0)});
  }
}

TEST_CASE("a solo race brakes at the bend and takes each gate on its derived tick",
          "[fixtures][replay][race][progress]") {
  // ADR 0007, Race / Progress and finishing; the independent integration derivation lives beside
  // the recorded commands in race-scripted-course/commands.csv.
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("race-scripted-course");
  const Snapshots snapshots = fixture.run();
  const simulation::EntityId racer = fixture.spawned_entity_id(1, 0);

  CHECK(first_tick_with_progress(snapshots, racer, 0) == 3);
  CHECK(first_tick_with_progress(snapshots, racer, 1) == 22);
  CHECK(first_tick_with_progress(snapshots, racer, 2) == 62);
  CHECK(first_tick_with_progress(snapshots, racer, 3) == 127);
  const auto bend = component_of<simulation::PhysicsBody>(at_tick(snapshots, 82), racer);
  REQUIRE(bend.has_value());
  CHECK(bend->position().x() == Catch::Approx(140.0).margin(1e-9));
  CHECK(bend->position().y() == 320.0);
  CHECK(bend->velocity() == simulation::Vector2::create(0.0, 0.0));
  for (std::uint64_t tick = 2; tick < 127; ++tick) {
    INFO("solo running tick " << tick);
    CHECK(at_tick(snapshots, tick).match().phase() == simulation::MatchPhase::kRunning);
    CHECK(at_tick(snapshots, tick).match().outcome() == simulation::MatchOutcome::undecided());
    CHECK(race_of(at_tick(snapshots, tick)).standings.empty());
  }

  const auto& finished = at_tick(snapshots, 127);
  CHECK(finished.match().phase() == simulation::MatchPhase::kEnded);
  CHECK(finished.match().outcome() == simulation::MatchOutcome::won_by_entity(racer));
  CHECK(component_of<simulation::PhysicsBody>(finished, racer).has_value());
  CHECK(race_of(finished).standings ==
        std::vector<simulation::RaceStanding>{{racer, simulation::ControllerId::create(1), 1,
                                               simulation::TickSequence::create(127)}});
  CHECK(at_tick(snapshots, 128).match().phase() == simulation::MatchPhase::kLobby);
  CHECK(component_of<simulation::PhysicsBody>(at_tick(snapshots, 128), racer).has_value());
  CHECK(at_tick(snapshots, 129).players().empty());
  CHECK(at_tick(snapshots, 129).components<simulation::RaceProgress>().empty());
  CHECK(race_of(at_tick(snapshots, 129)).standings == race_of(finished).standings);
}

TEST_CASE("an off-track racer disappears on the crossing tick and returns at its gate at rest",
          "[fixtures][replay][race][respawn]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("race-off-track-return");
  const Snapshots snapshots = fixture.run();
  const simulation::EntityId racer = fixture.spawned_entity_id(1, 0);
  CHECK(first_tick_with_progress(snapshots, racer, 1) == 22);
  CHECK_FALSE(first_tick_with_progress(snapshots, racer, 2).has_value());
  const auto before = component_of<simulation::PhysicsBody>(at_tick(snapshots, 61), racer);
  REQUIRE(before.has_value());
  CHECK(before->position().y() == Catch::Approx(339.5).margin(1e-9));
  for (std::uint64_t tick = 62; tick <= 70; ++tick) {
    INFO("out-of-play tick " << tick);
    const auto& snapshot = at_tick(snapshots, tick);
    CHECK_FALSE(component_of<simulation::PhysicsBody>(snapshot, racer).has_value());
    CHECK(component_of<simulation::Controllable>(snapshot, racer).has_value());
    CHECK(component_of<simulation::RaceProgress>(snapshot, racer) == simulation::RaceProgress{1});
    CHECK(snapshot.match().phase() == simulation::MatchPhase::kRunning);
    const auto timer = component_of<simulation::RespawnTimer>(snapshot, racer);
    if (tick < 70) {
      REQUIRE(timer.has_value());
      CHECK(timer->ticks_remaining == 70 - tick);
    } else {
      CHECK_FALSE(timer.has_value());
    }
  }
  for (std::uint64_t tick = 71; tick <= fixture.tick_count(); ++tick) {
    INFO("returned tick " << tick);
    const auto& snapshot = at_tick(snapshots, tick);
    const auto body = component_of<simulation::PhysicsBody>(snapshot, racer);
    REQUIRE(body.has_value());
    CHECK(body->position() == simulation::Vector2::create(110.0, 320.0));
    CHECK(body->velocity() == simulation::Vector2::create(0.0, 0.0));
    CHECK(body->acceleration() == simulation::Vector2::create(0.0, 0.0));
    CHECK(component_of<simulation::RaceProgress>(snapshot, racer) == simulation::RaceProgress{1});
  }
}

TEST_CASE("two racers crossing together share first placement and finish with a draw",
          "[fixtures][replay][race][standings]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("race-shared-finish");
  const Snapshots snapshots = fixture.run();
  const simulation::EntityId first = fixture.spawned_entity_id(1, 0);
  const simulation::EntityId second = fixture.spawned_entity_id(1, 1);
  for (const auto racer : {first, second}) {
    CHECK(first_tick_with_progress(snapshots, racer, 1) == 24);
    CHECK(first_tick_with_progress(snapshots, racer, 2) == 38);
    CHECK(first_tick_with_progress(snapshots, racer, 3) == 48);
    CHECK(component_of<simulation::PhysicsBody>(at_tick(snapshots, 48), racer).has_value());
  }
  CHECK(race_of(at_tick(snapshots, 47)).standings.empty());
  CHECK(at_tick(snapshots, 47).match().phase() == simulation::MatchPhase::kRunning);
  const auto& finished = at_tick(snapshots, 48);
  CHECK(finished.match().phase() == simulation::MatchPhase::kEnded);
  CHECK(finished.match().outcome() == simulation::MatchOutcome::drawn());
  CHECK(
      race_of(finished).standings ==
      std::vector<simulation::RaceStanding>{
          {first, simulation::ControllerId::create(1), 1, simulation::TickSequence::create(48)},
          {second, simulation::ControllerId::create(2), 1, simulation::TickSequence::create(48)}});
}

TEST_CASE("the first finisher stays physical and its finish window takes precedence over the clock",
          "[fixtures][replay][race][objective]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("race-finish-window");
  const Snapshots snapshots = fixture.run();
  const simulation::EntityId winner = fixture.spawned_entity_id(1, 0);
  const simulation::EntityId unfinished = fixture.spawned_entity_id(1, 1);
  CHECK(first_tick_with_progress(snapshots, winner, 3) == 47);
  CHECK_FALSE(first_tick_with_progress(snapshots, unfinished, 1).has_value());
  CHECK(fixture.race().time_limit_ticks() == 48);
  CHECK(fixture.race().finish_window_ticks() == 8);
  for (std::uint64_t tick = 47; tick < 55; ++tick) {
    INFO("open finish window tick " << tick);
    const auto& snapshot = at_tick(snapshots, tick);
    CHECK(snapshot.match().phase() == simulation::MatchPhase::kRunning);
    CHECK(snapshot.match().outcome() == simulation::MatchOutcome::undecided());
    const auto body = component_of<simulation::PhysicsBody>(snapshot, winner);
    REQUIRE(body.has_value());
    CHECK(body->velocity().x() == static_cast<double>(tick - 2) * 10.0);
    CHECK(race_of(snapshot).standings ==
          std::vector<simulation::RaceStanding>{{winner, simulation::ControllerId::create(1), 1,
                                                 simulation::TickSequence::create(47)}});
  }
  CHECK(at_tick(snapshots, 55).match().phase() == simulation::MatchPhase::kEnded);
  CHECK(at_tick(snapshots, 55).match().outcome() ==
        simulation::MatchOutcome::won_by_entity(winner));
}

TEST_CASE("without a finisher the clock ranks the racers by gates taken",
          "[fixtures][replay][race][objective]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("race-time-limit");
  const Snapshots snapshots = fixture.run();
  const simulation::EntityId leader = fixture.spawned_entity_id(1, 0);
  const simulation::EntityId trailing = fixture.spawned_entity_id(1, 1);
  CHECK(first_tick_with_progress(snapshots, leader, 1) == 22);
  CHECK(first_tick_with_progress(snapshots, leader, 2) == 37);
  CHECK_FALSE(first_tick_with_progress(snapshots, leader, 3).has_value());
  CHECK(at_tick(snapshots, 41).match().phase() == simulation::MatchPhase::kRunning);
  const auto& decided = at_tick(snapshots, 42);
  CHECK(component_of<simulation::RaceProgress>(decided, leader) == simulation::RaceProgress{2});
  CHECK(component_of<simulation::RaceProgress>(decided, trailing) == simulation::RaceProgress{0});
  CHECK(race_of(decided).standings.empty());
  CHECK(decided.match().phase() == simulation::MatchPhase::kEnded);
  CHECK(decided.match().outcome() == simulation::MatchOutcome::won_by_entity(leader));
}

TEST_CASE("100 fresh runs of race-scripted-course produce bit-identical ordered snapshots",
          "[fixtures][replay][race][determinism]") {
  require_one_hundred_identical_runs("race-scripted-course");
}

TEST_CASE("100 fresh runs of race-off-track-return produce bit-identical ordered snapshots",
          "[fixtures][replay][race][determinism]") {
  require_one_hundred_identical_runs("race-off-track-return");
}

TEST_CASE("100 fresh runs of race-shared-finish produce bit-identical ordered snapshots",
          "[fixtures][replay][race][determinism]") {
  require_one_hundred_identical_runs("race-shared-finish");
}

TEST_CASE("100 fresh runs of race-finish-window produce bit-identical ordered snapshots",
          "[fixtures][replay][race][determinism]") {
  require_one_hundred_identical_runs("race-finish-window");
}

TEST_CASE("100 fresh runs of race-time-limit produce bit-identical ordered snapshots",
          "[fixtures][replay][race][determinism]") {
  require_one_hundred_identical_runs("race-time-limit");
}
