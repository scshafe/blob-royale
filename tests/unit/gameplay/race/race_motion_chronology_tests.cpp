#include "race/race_mode.hpp"

#include "race/fixtures/race_motion_fixture.hpp"

#include "components/lethal_on_contact_component.hpp"
#include "components/respawn_timer_component.hpp"

#include <catch2/catch_test_macros.hpp>

namespace simulation = blob_royale::simulation;
namespace gameplay = blob_royale::gameplay;
namespace testing = blob_royale::testing;
namespace fixture = blob_royale::testing::race_motion_fixture;

TEST_CASE("a fast racer crosses multiple gates and terminates exactly at the final certified gate",
          "[unit][gameplay][race][chronology]") {
  testing::SteppedGame driver{
      fixture::game(fixture::moving_world({fixture::point(100.0)}), testing::race_test_map())};
  const auto snapshot = driver.step();
  REQUIRE(snapshot.components<simulation::RaceProgress>().size() == 1);
  CHECK(snapshot.components<simulation::RaceProgress>().front().value.next_checkpoint == 2);
  REQUIRE(fixture::race(snapshot).standings.size() == 1);
  const auto& finish = fixture::race(snapshot).standings.front();
  CHECK(finish.finished_tick == simulation::TickSequence::create(1));
  CHECK(finish.finished_tick_offset > simulation::MotionTime::start());
  CHECK(finish.finished_tick_offset < simulation::MotionTime::end());
  const auto body = testing::published_body(snapshot, fixture::kFirstEntity);
  REQUIRE(body.has_value());
  CHECK(body->position().x() < 600.0);
  CHECK(body->position().x() > 500.0);
  CHECK(body->velocity() == fixture::zero());
  CHECK(body->acceleration() == fixture::zero());
}

TEST_CASE("same-tick race placements distinguish unequal times and tie only equal certified times",
          "[unit][gameplay][race][chronology]") {
  for (const bool tied : {false, true}) {
    const auto world = fixture::moving_world(
        {fixture::point(100.0, 300.0), fixture::point(tied ? 100.0 : 120.0, 340.0)});
    testing::SteppedGame driver{fixture::game(world)};
    const auto snapshot = driver.step();
    const auto& standings = fixture::race(snapshot).standings;
    REQUIRE(standings.size() == 2);
    if (tied) {
      CHECK(standings[0].entity == fixture::entity());
      CHECK(standings[1].entity == fixture::entity(fixture::kSecondEntity));
      CHECK(standings[0].finished_tick_offset == standings[1].finished_tick_offset);
      CHECK(standings[0].placement == 1);
      CHECK(standings[1].placement == 1);
      CHECK(snapshot.match().outcome() == simulation::MatchOutcome::drawn());
    } else {
      CHECK(standings[0].entity == fixture::entity(fixture::kSecondEntity));
      CHECK(standings[0].finished_tick_offset < standings[1].finished_tick_offset);
      CHECK(standings[0].placement == 1);
      CHECK(standings[1].placement == 2);
      CHECK(snapshot.match().outcome() ==
            simulation::MatchOutcome::won_by_entity(fixture::entity(fixture::kSecondEntity)));
    }
  }
}

TEST_CASE("a later support loss retains an earlier gate and prevents a later finish",
          "[unit][gameplay][race][chronology]") {
  const auto map = testing::race_test_map("race_gate_then_fall",
                                          {fixture::point(300.0, 340.0), fixture::point(600.0)});
  auto world = fixture::moving_world({fixture::point(250.0)}, fixture::point(80'000.0, 40'000.0));
  testing::SteppedGame driver{fixture::game(std::move(world), map)};
  const auto snapshot = driver.step();
  CHECK_FALSE(testing::published_body(snapshot, fixture::kFirstEntity).has_value());
  REQUIRE(snapshot.components<simulation::RaceProgress>().size() == 1);
  CHECK(snapshot.components<simulation::RaceProgress>().front().value.next_checkpoint == 1);
  CHECK(fixture::race(snapshot).standings.empty());
  CHECK(snapshot.components<simulation::RespawnTimer>().size() == 1);
}

TEST_CASE("a finish terminates before a later lethal contact and held input cannot reactivate it",
          "[unit][gameplay][race][chronology]") {
  auto world = fixture::moving_world({fixture::point(100.0)});
  const auto hazard = fixture::entity(fixture::kHazardEntity);
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      hazard, simulation::PhysicsBody::create_static(fixture::point(500.0)).with_radius(10.0));
  world.mutable_store<simulation::LethalOnContact>().insert_or_assign(hazard, {});
  testing::SteppedGame driver{fixture::game(std::move(world))};
  const auto finish = driver.step({testing::thrust_command(fixture::kFirstEntity, 1.0, 0.0)});
  const auto stopped = testing::published_body(finish, fixture::kFirstEntity);
  REQUIRE(stopped.has_value());
  CHECK(stopped->position().x() < fixture::kFinishX);
  CHECK(stopped->velocity() == fixture::zero());
  CHECK(finish.components<simulation::RespawnTimer>().empty());
  const auto later = driver.step({testing::thrust_command(fixture::kFirstEntity, 1.0, 0.0)});
  REQUIRE(testing::published_body(later, fixture::kFirstEntity).has_value());
  CHECK(*testing::published_body(later, fixture::kFirstEntity) == *stopped);
  CHECK(fixture::race(later).standings == fixture::race(finish).standings);
}

TEST_CASE("a fallen racer cannot transfer momentum to a later floating body on its abandoned path",
          "[unit][gameplay][race][chronology]") {
  auto world = fixture::moving_world({fixture::point(100.0, 400.0)});
  const auto target = fixture::entity(fixture::kHazardEntity);
  const auto floating = simulation::PhysicsBody::create(fixture::point(500.0, 400.0),
                                                        fixture::zero(), fixture::zero())
                            .with_radius(10.0);
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(target, floating);
  testing::SteppedGame driver{fixture::game(std::move(world))};
  const auto snapshot = driver.step();
  CHECK_FALSE(testing::published_body(snapshot, fixture::kFirstEntity).has_value());
  REQUIRE(testing::published_body(snapshot, fixture::kHazardEntity).has_value());
  CHECK(*testing::published_body(snapshot, fixture::kHazardEntity) == floating);
  CHECK(fixture::race(snapshot).standings.empty());
}

TEST_CASE("a directly seeded completed racer is input locked on its first quantum",
          "[unit][gameplay][race][chronology]") {
  auto world = fixture::moving_world({fixture::point(100.0)}, fixture::zero());
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(fixture::entity(),
                                                                   simulation::RaceProgress{1});
  world.mutable_store<simulation::Controllable>()
      .mutable_find(fixture::entity())
      ->normalized_thrust_intent = fixture::point(1.0, 0.0);
  testing::SteppedGame driver{fixture::game(std::move(world))};
  const auto snapshot = driver.step({testing::thrust_command(fixture::kFirstEntity, 1.0, 0.0)});
  const auto body = testing::published_body(snapshot, fixture::kFirstEntity);
  REQUIRE(body.has_value());
  CHECK(body->position() == fixture::point(100.0));
  CHECK(body->velocity() == fixture::zero());
  CHECK(body->acceleration() == fixture::zero());
  CHECK(fixture::race(snapshot).standings.empty());
}

TEST_CASE("a bounded multigate failure rolls back the entire quantum and repeats identically",
          "[unit][gameplay][race][chronology][rollback]") {
  const auto map =
      testing::race_test_map("race_multigate_budget",
                             {fixture::point(100.0), fixture::point(100.0), fixture::point(100.0)});
  simulation::MotionLimits limits;
  limits.events = 1;
  auto game =
      fixture::game(fixture::moving_world({fixture::point(100.0)}, fixture::zero()), map, limits);
  const auto before = game.snapshot();
  for (const int attempt : {0, 1}) {
    CAPTURE(attempt);
    try {
      game.step(testing::kGameplayFixedDelta, simulation::InputBatch::empty());
      FAIL("multigate trajectory silently exceeded its lower event budget");
    } catch (const simulation::SimulationValidationError& error) {
      CHECK(error.validation_code() ==
            simulation::SimulationValidationCode::kContinuousMotionBudgetExceeded);
    }
    CHECK(game.snapshot() == before);
    CHECK(game.tick_sequence() == simulation::TickSequence::zero());
  }
}
