#include "fixtures/hill_roam_movement_fixture.hpp"

#include "gameplay_validation_error.hpp"
#include "king_of_the_hill/hill_scoring_system.hpp"
#include "king_of_the_hill/king_of_the_hill_mode.hpp"
#include "simulation_validation_error.hpp"
#include "terrain_queries.hpp"

#include <catch2/catch_test_macros.hpp>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;
namespace fixture = blob_royale::testing::hill_roam_movement_fixture;

TEST_CASE("roaming hill resets nonparticipant motion without drawing in lobby and countdown",
          "[unit][gameplay][king_of_the_hill][hill_roam][lifecycle]") {
  for (const auto phase : {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown}) {
    auto world = fixture::world();
    fixture::schedule(world, fixture::outward_velocity());
    world.mutable_store<simulation::Hill>().insert_or_assign(
        fixture::hill_entity(), simulation::Hill{fixture::far_corner(), fixture::kRadius});
    world.mutable_match().phase = phase;
    fixture::move(world, fixture::kFirstTick);
    CHECK(fixture::hill(world).center == fixture::first_marker());
    CHECK(fixture::motion(world).velocity == fixture::zero());
    CHECK_FALSE(fixture::motion(world).schedule.has_value());
    CHECK(world.random_draw_counts() == simulation::RandomDrawCounts{});
  }
}

TEST_CASE("roaming hill moves on its first running system tick and retargets on its exact deadline",
          "[unit][gameplay][king_of_the_hill][hill_roam][random_streams]") {
  auto world = fixture::world();
  const auto initial = fixture::hill(world);
  fixture::move(world, fixture::kFirstTick);
  const auto first = fixture::motion(world);
  REQUIRE(first.schedule.has_value());
  CHECK(first.schedule->next_retarget_tick.value() ==
        fixture::kFirstTick + fixture::kRetargetTicks);
  CHECK(first.schedule->random_stream == simulation::RandomStreamKind::kHill);
  CHECK(fixture::hill(world).center != initial.center);
  CHECK(world.random(simulation::RandomStreamKind::kHill).draw_count() == 4);
  CHECK(world.random(simulation::RandomStreamKind::kHazards).draw_count() == 0);

  fixture::move(world, fixture::kFirstTick + 1);
  CHECK(fixture::motion(world) == first);
  CHECK(world.random(simulation::RandomStreamKind::kHill).draw_count() == 4);
  fixture::move(world, first.schedule->next_retarget_tick.value());
  CHECK(world.random(simulation::RandomStreamKind::kHill).draw_count() == 8);
  CHECK(fixture::motion(world).schedule->next_retarget_tick.value() ==
        fixture::kFirstTick + 2 * fixture::kRetargetTicks);
}

TEST_CASE("roaming hill same seed worlds remain equal and distinct seeds select different motion",
          "[unit][gameplay][king_of_the_hill][hill_roam][determinism]") {
  auto left = fixture::world();
  auto right = fixture::world();
  auto different = fixture::world(fixture::kDifferentSeed);
  for (std::uint64_t tick = fixture::kFirstTick; tick <= fixture::kContinuationTicks; ++tick) {
    fixture::move(left, tick);
    fixture::move(right, tick);
    fixture::move(different, tick);
    CHECK(left == right);
  }
  CHECK(fixture::motion(left).velocity != fixture::motion(different).velocity);
}

TEST_CASE(
    "roaming hill boundary cancellation rests without extra draws and still retargets on schedule",
    "[unit][gameplay][king_of_the_hill][hill_roam][boundary]") {
  auto world = fixture::world();
  world.mutable_store<simulation::Hill>().insert_or_assign(
      fixture::hill_entity(), simulation::Hill{fixture::far_corner(), fixture::kRadius});
  fixture::schedule(world, fixture::outward_velocity(), fixture::kBoundaryRetargetTick);
  for (std::uint64_t tick = fixture::kFirstTick; tick < fixture::kBoundaryRetargetTick; ++tick) {
    fixture::move(world, tick);
    CHECK(fixture::hill(world).center == fixture::far_corner());
    CHECK(fixture::motion(world).velocity == fixture::zero());
    CHECK(world.random_draw_counts() == simulation::RandomDrawCounts{});
  }
  fixture::move(world, fixture::kBoundaryRetargetTick);
  CHECK(world.random(simulation::RandomStreamKind::kHill).draw_count() == 4);
  CHECK(fixture::motion(world).schedule->next_retarget_tick.value() ==
        fixture::kBoundaryRetargetTick + fixture::kRetargetTicks);
  const auto map = fixture::map();
  CHECK(map.bounds().contains(fixture::hill(world).center));
}

TEST_CASE("roaming hill ended phase freezes position velocity schedule and random state",
          "[unit][gameplay][king_of_the_hill][hill_roam][lifecycle]") {
  auto world = fixture::world();
  fixture::move(world, fixture::kFirstTick);
  world.mutable_match().phase = simulation::MatchPhase::kEnded;
  const auto frozen = world;
  fixture::move(world, fixture::kFarRetargetTick);
  CHECK(world == frozen);
}

TEST_CASE(
    "roaming hill respects actual lifecycle transition staging and resets after lobby arrival",
    "[unit][gameplay][king_of_the_hill][hill_roam][lifecycle]") {
  testing::SteppedGame driver{testing::gameplay_simulation(
      gameplay::KingOfTheHillMode::create(fixture::lifecycle_configuration()), fixture::map(),
      fixture::kSeed, testing::started_lobby(fixture::kLifecycleSeatCount))};
  // Two participants prevent the existing solo-player victory rule from ending before the clock.
  const auto countdown = driver.step({testing::spawn_command(fixture::kScoringPlayer),
                                      testing::spawn_command(fixture::kLifecyclePeer)});
  CHECK(countdown.match().phase() == simulation::MatchPhase::kCountdown);
  const auto running_arrival = driver.step();
  CHECK(running_arrival.match().phase() == simulation::MatchPhase::kRunning);
  CHECK(running_arrival.components<simulation::Hill>().front().value.center ==
        fixture::first_marker());
  CHECK(running_arrival.random_draw_counts() == simulation::RandomDrawCounts{});
  const auto first_motion = driver.step();
  CHECK(first_motion.components<simulation::Hill>().front().value.center !=
        fixture::first_marker());
  auto ended = first_motion;
  while (ended.tick_sequence().value() < fixture::kLifecycleEndTick) {
    ended = driver.step();
  }
  REQUIRE(ended.match().phase() == simulation::MatchPhase::kEnded);
  const auto lobby_arrival = driver.step();
  REQUIRE(lobby_arrival.match().phase() == simulation::MatchPhase::kLobby);
  CHECK(lobby_arrival.components<simulation::Hill>().front().value ==
        ended.components<simulation::Hill>().front().value);
  CHECK(lobby_arrival.random_draw_counts() == ended.random_draw_counts());
  const auto reset = driver.step();
  CHECK(reset.components<simulation::Hill>().front().value.center == fixture::first_marker());
  CHECK(reset.components<simulation::HillMotion>().front().value.velocity == fixture::zero());
  CHECK(reset.random_draw_counts() == ended.random_draw_counts());
}

TEST_CASE("roaming hill crosses unsupported terrain into a disconnected safe island without "
          "terrain changes",
          "[unit][gameplay][king_of_the_hill][hill_roam][terrain]") {
  const auto map = fixture::map(true);
  const auto initial_terrain = map.terrain();
  auto world = fixture::world();
  fixture::schedule(world, fixture::crossing_velocity());
  const auto initial_motion = fixture::motion(world);
  bool crossed_void = false;
  for (std::uint64_t tick = fixture::kFirstTick; tick <= fixture::kCrossingTicks; ++tick) {
    fixture::move(world, tick, map);
    crossed_void = crossed_void ||
                   !simulation::terrain_supports_point(map.terrain(), fixture::hill(world).center);
    CHECK(fixture::motion(world) == initial_motion);
  }
  CHECK(crossed_void);
  CHECK(fixture::hill(world).center == fixture::far_marker());
  CHECK(simulation::terrain_supports_disc(map.terrain(), fixture::far_marker(), fixture::kRadius));
  CHECK(map.terrain() == initial_terrain);
  CHECK(world.store<simulation::PhysicsBody>().empty());
  CHECK(world.random_draw_counts() == simulation::RandomDrawCounts{});
}

TEST_CASE("roaming movement precedes scoring so the published moved circle decides the point",
          "[unit][gameplay][king_of_the_hill][hill_roam][scoring]") {
  auto world = fixture::scoring_world();
  fixture::schedule(world, fixture::scoring_velocity());
  auto game = simulation::GameSimulation::create(
      testing::gameplay_configuration(), world,
      simulation::GameSimulationSetup::of_mode(
          fixture::map(), gameplay::KingOfTheHillMode::create(fixture::configuration())));
  const auto before = game.snapshot();
  CHECK(before.components<simulation::Score>().empty());
  game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  const auto after = game.snapshot();
  REQUIRE(after.components<simulation::Hill>().size() == 1);
  CHECK(after.components<simulation::Hill>().front().value.center == fixture::scoring_center());
  REQUIRE(after.components<simulation::Score>().size() == 1);
  CHECK(after.components<simulation::Score>().front().entity.value() == fixture::kScoringPlayer);
  CHECK(after.components<simulation::Score>().front().value.points == 1);
}

TEST_CASE(
    "roaming hill downstream failure rolls back motion schedule and RNG before identical retry",
    "[unit][gameplay][king_of_the_hill][hill_roam][rollback]") {
  auto retried = fixture::rollback_simulation();
  auto control = fixture::rollback_simulation();
  for (std::uint64_t tick = fixture::kFirstTick; tick <= fixture::kContinuationTicks; ++tick) {
    const auto before = retried.snapshot();
    CHECK_THROWS_AS(
        retried.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()),
        simulation::SimulationValidationError);
    CHECK(retried.snapshot() == before);
    retried.step(simulation::FixedDelta::canonical(), fixture::reserved_input(tick));
    control.step(simulation::FixedDelta::canonical(), fixture::reserved_input(tick));
    CHECK(retried.snapshot() == control.snapshot());
  }
}

TEST_CASE("roaming hill deadline overflow and invalid stream fail before advancing random state",
          "[unit][gameplay][king_of_the_hill][hill_roam][validation]") {
  SECTION("deadline cannot represent every configured interval") {
    auto world = fixture::world();
    const auto before = world;
    try {
      fixture::move(world, simulation::TickSequence::kMaximumValue);
      FAIL("overflowing hill deadline was accepted");
    } catch (const gameplay::GameplayValidationError& error) {
      CHECK(error.validation_code() ==
            gameplay::GameplayValidationCode::kKingOfTheHillRetargetTickOverflow);
    }
    CHECK(world == before);
  }
  SECTION("committed stream is not the hill stream") {
    auto world = fixture::world();
    fixture::schedule(world, fixture::crossing_velocity());
    auto invalid = fixture::motion(world);
    invalid.schedule->random_stream = simulation::RandomStreamKind::kHazards;
    world.mutable_store<simulation::HillMotion>().insert_or_assign(fixture::hill_entity(), invalid);
    const auto before = world;
    CHECK_THROWS_AS(fixture::move(world, fixture::kFirstTick), gameplay::GameplayValidationError);
    CHECK(world == before);
  }
}

TEST_CASE("roaming initial running state may establish motion but ended state cannot invent it",
          "[unit][gameplay][king_of_the_hill][hill_roam][validation]") {
  auto running = fixture::world();
  running.mutable_store<simulation::HillMotion>().erase(fixture::hill_entity());
  fixture::move(running, fixture::kFirstTick);
  CHECK(fixture::motion(running).schedule.has_value());

  auto ended = fixture::world();
  ended.mutable_store<simulation::HillMotion>().erase(fixture::hill_entity());
  ended.mutable_match().phase = simulation::MatchPhase::kEnded;
  const auto before = ended;
  CHECK_THROWS_AS(fixture::move(ended, fixture::kFirstTick), gameplay::GameplayValidationError);
  CHECK(ended == before);
}

TEST_CASE("marker tour keeps its legacy entity shape and consumes no hill random draws",
          "[unit][gameplay][king_of_the_hill][hill_roam][compatibility]") {
  auto world = fixture::world();
  world.mutable_store<simulation::HillMotion>().erase(fixture::hill_entity());
  const auto movement = gameplay::HillMovementSystem::create(fixture::Configuration::defaults());
  const testing::TickHarness harness{simulation::TickSequence::create(fixture::kFarRetargetTick),
                                     fixture::map()};
  movement->apply(world, harness.context());
  CHECK(world.store<simulation::HillMotion>().empty());
  CHECK(world.random_draw_counts() == simulation::RandomDrawCounts{});
  CHECK(fixture::hill(world).center == fixture::first_marker());
}
