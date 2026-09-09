#include "replay_fixture.hpp"

#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/hill_component.hpp"
#include "components/hill_presence_component.hpp"
#include "components/score_component.hpp"
#include "entity_id.hpp"
#include "match_outcome.hpp"
#include "match_phase.hpp"
#include "mode_match_state_registry.hpp"
#include "mode_states/king_of_the_hill_mode_state.hpp"
#include "physics_body.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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

[[nodiscard]] std::optional<simulation::Hill> hill_of(const simulation::WorldSnapshot& snapshot) {
  const auto hills = snapshot.components<simulation::Hill>();
  if (hills.empty()) {
    return std::nullopt;
  }
  return hills.front().value;
}

[[nodiscard]] std::int64_t score_of(const simulation::WorldSnapshot& snapshot,
                                    const simulation::EntityId entity) {
  for (const simulation::ComponentStore<simulation::Score>::Entry& entry :
       snapshot.components<simulation::Score>()) {
    if (entry.entity == entity) {
      return entry.value.points;
    }
  }
  return 0;
}

[[nodiscard]] std::optional<std::uint64_t> presence_of(const simulation::WorldSnapshot& snapshot,
                                                       const simulation::EntityId entity) {
  for (const simulation::ComponentStore<simulation::HillPresence>::Entry& entry :
       snapshot.components<simulation::HillPresence>()) {
    if (entry.entity == entity) {
      return entry.value.inside_ticks;
    }
  }
  return std::nullopt;
}

// The tick on which `entity`'s score first reads `points`, or nullopt when it never does.
[[nodiscard]] std::optional<std::uint64_t> first_tick_with_score(const Snapshots& snapshots,
                                                                 const simulation::EntityId entity,
                                                                 const std::int64_t points) {
  for (std::uint64_t tick = 1; tick <= snapshots.size(); ++tick) {
    if (score_of(at_tick(snapshots, tick), entity) == points) {
      return tick;
    }
  }
  return std::nullopt;
}

} // namespace

TEST_CASE("the replay format reads a hill directory into a runnable match",
          "[fixtures][replay][king_of_the_hill]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("hill-scripted-match");

  CHECK(fixture.mode_name() == std::string{"king_of_the_hill"});
  CHECK(fixture.tick_count() == 310);
  CHECK(fixture.map().spawn_points().size() == 2);
  CHECK(fixture.king_of_the_hill().hill_dwell_ticks() == 100);
  CHECK(fixture.king_of_the_hill().hill_travel_ticks() == 32);
  CHECK(fixture.king_of_the_hill().point_interval_ticks() == 4);
  CHECK(fixture.king_of_the_hill().time_limit_ticks() == 200);
  CHECK_FALSE(fixture.king_of_the_hill().contested_hill_scores());
  // The section the fixture does not name holds that mode's defaults, which the hill never reads.
  CHECK(fixture.royale().zone_shrink_ticks() == 36'000);
}

TEST_CASE("the hill exists from the first tick, tours by derived ticks, and freezes when the match "
          "ends",
          "[fixtures][replay][king_of_the_hill][hill]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("hill-scripted-match");
  const Snapshots snapshots = fixture.run();
  REQUIRE(snapshots.size() == fixture.tick_count());

  // Two spawns on tick 1 take ids 1 and 2; the hill takes the tick's one system-created id.
  const simulation::EntityId hill_entity = simulation::EntityId::create(3);
  REQUIRE(hill_of(at_tick(snapshots, 1)).has_value());
  CHECK(at_tick(snapshots, 1).components<simulation::Hill>().front().entity == hill_entity);
  CHECK(hill_of(at_tick(snapshots, 1))->center == simulation::Vector2::create(400.0, 320.0));
  CHECK(hill_of(at_tick(snapshots, 1))->radius == 90.0);

  // Running from tick 2. The first glide tick is e = 100 (tick 102) at fraction zero, so the first
  // tick the centre moves is 103, and every centre on the glide is exact: x = 400 + 10 * (e - 100).
  CHECK(at_tick(snapshots, 2).match().phase() == simulation::MatchPhase::kRunning);
  CHECK(hill_of(at_tick(snapshots, 102))->center == simulation::Vector2::create(400.0, 320.0));
  CHECK(hill_of(at_tick(snapshots, 103))->center == simulation::Vector2::create(410.0, 320.0));
  CHECK(hill_of(at_tick(snapshots, 118))->center == simulation::Vector2::create(560.0, 320.0));
  CHECK(hill_of(at_tick(snapshots, 134))->center == simulation::Vector2::create(720.0, 320.0));
  CHECK(hill_of(at_tick(snapshots, 200))->center == simulation::Vector2::create(720.0, 320.0));

  // The clock decides on tick 202 and the hill freezes at that tick's stop while the match is
  // `ended`; with a zero restart delay that is one tick, and on tick 203 the lobby begins and the
  // hill returns to the first stop, where the next match will find it.
  CHECK(at_tick(snapshots, 201).match().phase() == simulation::MatchPhase::kRunning);
  CHECK(at_tick(snapshots, 202).match().phase() == simulation::MatchPhase::kEnded);
  CHECK(hill_of(at_tick(snapshots, 202))->center == simulation::Vector2::create(720.0, 320.0));
  // Tick 203 commits `ended -> lobby` at kLifecycle, after `hill_movement` observed `ended` at
  // kPostKernel, so that tick's hill is still the frozen one; tick 204 is the first the lobby's
  // hill is written on.
  CHECK(at_tick(snapshots, 203).match().phase() == simulation::MatchPhase::kLobby);
  CHECK(hill_of(at_tick(snapshots, 203))->center == simulation::Vector2::create(720.0, 320.0));
  CHECK(hill_of(at_tick(snapshots, 204))->center == simulation::Vector2::create(400.0, 320.0));
  CHECK(hill_of(at_tick(snapshots, 310))->center == simulation::Vector2::create(400.0, 320.0));
  // The hill entity is never a player and never wiped.
  for (std::uint64_t tick = 1; tick <= fixture.tick_count(); ++tick) {
    INFO("tick " << tick);
    REQUIRE(hill_of(at_tick(snapshots, tick)).has_value());
  }
}

TEST_CASE("presence rolls into points on derived ticks and the clock ranks the leader",
          "[fixtures][replay][king_of_the_hill][scoring]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("hill-scripted-match");
  const Snapshots snapshots = fixture.run();
  const simulation::EntityId player_1 = fixture.spawned_entity_id(1, 0);
  const simulation::EntityId player_2 = fixture.spawned_entity_id(1, 1);

  // Player 1 counts from tick 3 and scores every fourth tick from 6; player 2 first scores on 128.
  CHECK(first_tick_with_score(snapshots, player_1, 1) == 6);
  CHECK(first_tick_with_score(snapshots, player_1, 2) == 10);
  CHECK(first_tick_with_score(snapshots, player_1, 27) == 110);
  CHECK(first_tick_with_score(snapshots, player_2, 1) == 128);
  // Player 1 leaves the hill on tick 112 and its partial point is erased, not banked.
  CHECK(presence_of(at_tick(snapshots, 111), player_1) == 1);
  CHECK_FALSE(presence_of(at_tick(snapshots, 112), player_1).has_value());
  CHECK(score_of(at_tick(snapshots, 112), player_1) == 27);
  // Player 2 is inside from tick 125.
  CHECK_FALSE(presence_of(at_tick(snapshots, 124), player_2).has_value());
  CHECK(presence_of(at_tick(snapshots, 125), player_2) == 1);

  // The clock: 27 to 19 on tick 202, won by player 1.
  const simulation::WorldSnapshot& decided = at_tick(snapshots, 202);
  CHECK(score_of(decided, player_1) == 27);
  CHECK(score_of(decided, player_2) == 19);
  CHECK(decided.match().outcome() == simulation::MatchOutcome::won_by_entity(player_1));
  CHECK(at_tick(snapshots, 201).match().outcome() == simulation::MatchOutcome::undecided());

  // The block carries the three denominators on every tick, including the first.
  for (const std::uint64_t tick : {1ULL, 2ULL, 150ULL, 202ULL, 310ULL}) {
    INFO("tick " << tick);
    const auto* held = std::get_if<simulation::KingOfTheHillModeState>(
        &at_tick(snapshots, tick).match().mode_state());
    REQUIRE(held != nullptr);
    CHECK(held->points_to_win == 50);
    CHECK(held->point_interval_ticks == 4);
    CHECK(held->time_limit_ticks == 200);
  }

  // Zero restart delay: tick 203 is lobby, and the shared reset wipes both players on tick 204,
  // the lobby tick after ended; the hill survives.
  CHECK(at_tick(snapshots, 203).match().phase() == simulation::MatchPhase::kLobby);
  CHECK(at_tick(snapshots, 203).players().size() == 2);
  CHECK(at_tick(snapshots, 204).players().empty());
  CHECK(hill_of(at_tick(snapshots, 204)).has_value());
}

TEST_CASE("the threshold decides on the tick the point is scored",
          "[fixtures][replay][king_of_the_hill][objective]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("hill-threshold");
  const Snapshots snapshots = fixture.run();
  const simulation::EntityId player_1 = fixture.spawned_entity_id(1, 0);

  CHECK(first_tick_with_score(snapshots, player_1, 1) == 6);
  CHECK(first_tick_with_score(snapshots, player_1, 2) == 10);
  CHECK(first_tick_with_score(snapshots, player_1, 3) == 14);
  CHECK(at_tick(snapshots, 13).match().phase() == simulation::MatchPhase::kRunning);
  CHECK(at_tick(snapshots, 14).match().phase() == simulation::MatchPhase::kEnded);
  CHECK(at_tick(snapshots, 14).match().outcome() ==
        simulation::MatchOutcome::won_by_entity(player_1));
}

TEST_CASE("a contested hill scores nobody until one player is pushed off it",
          "[fixtures][replay][king_of_the_hill][scoring]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("hill-contested");
  const Snapshots snapshots = fixture.run();
  const simulation::EntityId player_1 = fixture.spawned_entity_id(1, 0);
  const simulation::EntityId player_2 = fixture.spawned_entity_id(1, 1);

  // Through tick 29 both are inside and nothing counts.
  for (std::uint64_t tick = 1; tick <= 29; ++tick) {
    INFO("tick " << tick);
    CHECK(score_of(at_tick(snapshots, tick), player_1) == 0);
    CHECK(score_of(at_tick(snapshots, tick), player_2) == 0);
    CHECK_FALSE(presence_of(at_tick(snapshots, tick), player_1).has_value());
  }
  // Player 2 crosses the rim on tick 30; player 1 counts alone from there.
  CHECK(presence_of(at_tick(snapshots, 30), player_1) == 1);
  CHECK(first_tick_with_score(snapshots, player_1, 1) == 33);
  CHECK(first_tick_with_score(snapshots, player_1, 2) == 37);
  CHECK(at_tick(snapshots, 37).match().outcome() ==
        simulation::MatchOutcome::won_by_entity(player_1));
  CHECK(score_of(at_tick(snapshots, 37), player_2) == 0);
}

TEST_CASE("a level scoreboard at the clock is a draw",
          "[fixtures][replay][king_of_the_hill][objective]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("hill-time-limit-draw");
  const Snapshots snapshots = fixture.run();

  CHECK(at_tick(snapshots, 21).match().phase() == simulation::MatchPhase::kRunning);
  CHECK(at_tick(snapshots, 22).match().phase() == simulation::MatchPhase::kEnded);
  CHECK(at_tick(snapshots, 22).match().outcome() == simulation::MatchOutcome::drawn());
  CHECK(at_tick(snapshots, 22).components<simulation::Score>().empty());
}

TEST_CASE("100 fresh runs of the scripted hill match produce bit-identical ordered snapshots",
          "[fixtures][replay][king_of_the_hill][determinism]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("hill-scripted-match");
  const Snapshots reference = fixture.run();
  REQUIRE(reference.size() == fixture.tick_count());

  std::size_t divergent_run = 0;
  std::size_t divergent_tick = 0;
  for (std::size_t run = 2; run <= 100; ++run) {
    const std::size_t tick = testing::first_divergent_tick(reference, fixture.run());
    if (tick != 0 && divergent_run == 0) {
      divergent_run = run;
      divergent_tick = tick;
    }
  }
  INFO("first divergent run " << divergent_run << " at tick " << divergent_tick);
  CHECK(divergent_run == 0);
  CHECK(divergent_tick == 0);
}
