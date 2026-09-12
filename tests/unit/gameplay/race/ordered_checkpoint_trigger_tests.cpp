#include "race/ordered_checkpoint_trigger.hpp"

#include "race/fixtures/race_motion_fixture.hpp"

#include "gameplay_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

namespace simulation = blob_royale::simulation;
namespace gameplay = blob_royale::gameplay;
namespace testing = blob_royale::testing;
namespace fixture = blob_royale::testing::race_motion_fixture;

TEST_CASE("ordered checkpoint binding uses absent zero and frozen unfinished progress only",
          "[unit][gameplay][race][motion_trigger]") {
  const testing::TickHarness harness{simulation::TickSequence::create(1), testing::race_test_map()};
  const auto course =
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration());
  const auto trigger = gameplay::OrderedCheckpointTrigger::create(course);
  auto world = testing::race_test_world({fixture::point(100.0)});
  CHECK(trigger->bind(world, fixture::entity(), harness.context()) == 0);
  for (const std::uint64_t count : {1U, 2U}) {
    world.mutable_store<simulation::RaceProgress>().insert_or_assign(
        fixture::entity(), simulation::RaceProgress{count});
    const auto bound = trigger->bind(world, fixture::entity(), harness.context());
    if (count == 1) {
      CHECK(bound == 1);
    } else {
      CHECK_FALSE(bound.has_value());
    }
  }
  world.mutable_store<simulation::RaceProgress>().erase(fixture::entity());
  for (const auto phase : {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
                           simulation::MatchPhase::kEnded}) {
    world.mutable_match().phase = phase;
    CHECK_FALSE(trigger->bind(world, fixture::entity(), harness.context()).has_value());
  }
  world.mutable_match().phase = simulation::MatchPhase::kRunning;
  world.mutable_store<simulation::Controllable>().erase(fixture::entity());
  CHECK_FALSE(trigger->bind(world, fixture::entity(), harness.context()).has_value());
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      fixture::entity(), simulation::Controllable{simulation::ControllerId::create(1)});
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      fixture::entity(), simulation::PhysicsBody::create_static(fixture::point(100.0)));
  CHECK_FALSE(trigger->bind(world, fixture::entity(), harness.context()).has_value());
  world.mutable_store<simulation::PhysicsBody>().erase(fixture::entity());
  CHECK_FALSE(trigger->bind(world, fixture::entity(), harness.context()).has_value());
}

TEST_CASE("owned race trigger table queries its source gates after mode and map destruction",
          "[unit][gameplay][race][motion_trigger]") {
  const auto table = [] {
    auto mode = gameplay::RaceMode::create(testing::race_test_configuration());
    mode->validate_map(testing::race_test_map());
    return mode->motion_triggers();
  }();
  const testing::TickHarness harness{
      simulation::TickSequence::create(1),
      testing::race_test_map("different_context_gates", {fixture::point(700.0)})};
  const auto world = fixture::moving_world({fixture::point(300.0)});
  const auto bindings = table.bind(world, harness.context());
  REQUIRE(bindings.rows().size() == 2);
  const auto& gate = bindings.rows()[1];
  CHECK(gate.feature_base == 1);
  CHECK(gate.cursor_limit == 2);
  const simulation::ContactRule::Subject subject{
      fixture::entity(), *world.store<simulation::PhysicsBody>().find(fixture::entity())};
  const simulation::MotionTriggerWindow window{
      {subject, simulation::MotionTime::start(), fixture::point(400.0, 0.0)},
      simulation::MotionTime::start()};
  simulation::MotionLimits limits;
  simulation::MotionWorkCounts work;
  simulation::MotionQueryBudget budget{work, limits};
  REQUIRE(gate.facts_override.has_value());
  const auto& facts = gate.facts_override->get();
  const auto proposal = gate.query(world, subject, window, harness.context(), facts, 0, budget);
  REQUIRE(proposal.has_value());
  CHECK(proposal->time == simulation::MotionTime::start());
  CHECK(work.root_queries == 1);
  const auto event =
      simulation::MotionTriggerEvent{simulation::MotionEventKey::boundary(
                                         proposal->time, proposal->priority, fixture::entity(), 1),
                                     0};
  const auto response = gate.response(world, subject, event, harness.context(), facts);
  CHECK(response.cursor == 1);
  CHECK(response.body.body == subject.body);
  CHECK(response.body.disposition == simulation::MotionDisposition::kContinue);
  REQUIRE(response.effects.size() == 1);
  CHECK(std::get<simulation::RaceCheckpointEvent>(response.effects.front()) ==
        simulation::RaceCheckpointEvent{fixture::entity(), 1, event.key.time()});
}

TEST_CASE("finish response preserves certified time while stopping velocity acceleration and epoch",
          "[unit][gameplay][race][motion_trigger]") {
  const testing::TickHarness harness{simulation::TickSequence::create(1), fixture::one_gate_map()};
  const auto trigger = gameplay::OrderedCheckpointTrigger::create(
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration()));
  const auto world = fixture::moving_world({fixture::point(100.0)});
  const auto body = world.store<simulation::PhysicsBody>()
                        .find(fixture::entity())
                        ->with_acceleration(fixture::point(400.0, 0.0));
  const simulation::ContactRule::Subject subject{fixture::entity(), body};
  const auto offset = simulation::MotionTime::create(0.375);
  const simulation::MotionTriggerEvent event{
      simulation::MotionEventKey::boundary(offset, simulation::MotionEventPriority::kCheckpoint,
                                           fixture::entity(), 1),
      0};
  const auto response = trigger->respond(world, subject, event, harness.context());
  CHECK(response.cursor == 1);
  CHECK(response.body.disposition == simulation::MotionDisposition::kTerminate);
  CHECK(response.body.body ==
        body.with_velocity(fixture::zero()).with_acceleration(fixture::zero()));
  REQUIRE(response.effects.size() == 1);
  CHECK(std::get<simulation::RaceCheckpointEvent>(response.effects.front()).tick_offset == offset);
}

TEST_CASE("race trigger rejects impossible frozen progress and completed response cursors",
          "[unit][gameplay][race][motion_trigger][validation]") {
  const testing::TickHarness harness{simulation::TickSequence::create(1), fixture::one_gate_map()};
  const auto trigger = gameplay::OrderedCheckpointTrigger::create(
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration()));
  auto world = fixture::moving_world({fixture::point(100.0)});
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(fixture::entity(),
                                                                   simulation::RaceProgress{2});
  CHECK_THROWS_AS(trigger->bind(world, fixture::entity(), harness.context()),
                  gameplay::GameplayValidationError);
  const simulation::ContactRule::Subject subject{
      fixture::entity(), *world.store<simulation::PhysicsBody>().find(fixture::entity())};
  const simulation::MotionTriggerEvent event{
      simulation::MotionEventKey::boundary(simulation::MotionTime::start(),
                                           simulation::MotionEventPriority::kCheckpoint,
                                           fixture::entity(), 2),
      1};
  CHECK_THROWS_AS(trigger->respond(world, subject, event, harness.context()),
                  gameplay::GameplayValidationError);
}
