#include "fixtures/movement_tuning_exchange_fixture.hpp"

#include "command_sink_error.hpp"
#include "simulation_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <thread>

namespace simulation = blob_royale::simulation;
namespace runtime = blob_royale::runtime;
namespace fixture = blob_royale::testing::movement_tuning_exchange_fixture;
using Submission = runtime::CommandSubmissionResult;
using Admission = runtime::MovementTuningAdmissionStatus;

TEST_CASE("movement tuning sink rejects foreign and unsafe requests before exchange admission",
          "[unit][runtime][movement_tuning][validation]") {
  fixture::Exchange state;
  const auto actor = state.sink.open_session("human", "boundary");
  const auto other = state.sink.open_session("human", "other");
  CHECK(state.sink.submit(actor, fixture::request(1, other)) ==
        Submission::kRejectedForeignController);
  CHECK(state.sink.submit(actor, fixture::request(0, actor)) ==
        Submission::kRejectedTuningRequestIdOutOfRange);
  CHECK(state.sink.submit(actor,
                          fixture::request(simulation::kMaximumProtocolSafeInteger + 1, actor)) ==
        Submission::kRejectedTuningRequestIdOutOfRange);
  CHECK(state.sink.submit(
            actor, fixture::request(1, actor, simulation::kMaximumProtocolSafeInteger + 1)) ==
        Submission::kRejectedTuningRevisionOutOfRange);
  CHECK(state.mailbox.statistics().pending_command_count == 0);
  CHECK(state.mailbox.statistics().unresolved_tuning_exchange_count == 0);
  CHECK_FALSE(state.delivery.claim(actor, simulation::TickSequence::zero()));
  CHECK(state.sink.submit(actor, fixture::request(1, actor)) == Submission::kAccepted);
}

TEST_CASE(
    "movement tuning committed but unclaimed result remains the controller's unresolved request",
    "[unit][runtime][movement_tuning]") {
  fixture::Exchange state;
  const auto actor = state.sink.open_session("human", "unclaimed");
  REQUIRE(state.mailbox.submit_tuning(fixture::request(1, actor), fixture::kStart) ==
          Submission::kAccepted);
  static_cast<void>(state.mailbox.drain());
  fixture::complete(state.mailbox, fixture::decision(1, 2, actor));
  CHECK(state.mailbox.submit_tuning(fixture::request(2, actor),
                                    fixture::kStart + fixture::kInterval) ==
        Submission::kRejectedTuningRequestInFlight);
  CHECK(state.mailbox.statistics().unresolved_tuning_exchange_count == 1);
  const auto first = state.delivery.claim(actor, simulation::TickSequence::create(2));
  REQUIRE(first.has_value());
  CHECK(std::get<simulation::MovementTuningDecision>(*first).tuning_request_id == 1);
  CHECK(state.mailbox.submit_tuning(fixture::request(2, actor),
                                    fixture::kStart + fixture::kInterval) ==
        Submission::kRejectedTuningRequestIdReused);
  CHECK(state.mailbox.submit_tuning(fixture::request(3, actor),
                                    fixture::kStart + fixture::kInterval) == Submission::kAccepted);
}

TEST_CASE("movement tuning exchange claims only covered committed results and rate deadlines do "
          "not slide",
          "[unit][runtime][movement_tuning]") {
  fixture::Exchange state;
  const auto actor = state.sink.open_session("human", "tuning");
  REQUIRE(state.mailbox.submit_tuning(fixture::request(1, actor), fixture::kStart) ==
          Submission::kAccepted);
  CHECK_FALSE(state.delivery.claim(actor, simulation::TickSequence::create(2)));
  static_cast<void>(state.mailbox.drain());
  fixture::complete(state.mailbox, fixture::decision(1, 2, actor));
  CHECK_FALSE(state.delivery.claim(actor, simulation::TickSequence::create(1)));
  const auto accepted = state.delivery.claim(actor, simulation::TickSequence::create(3));
  REQUIRE(accepted.has_value());
  CHECK(std::get<simulation::MovementTuningDecision>(*accepted).decision_tick ==
        simulation::TickSequence::create(2));
  REQUIRE(
      state.mailbox.submit_tuning(fixture::request(2, actor), fixture::kStart + fixture::kEarly) ==
      Submission::kRejectedTuningRateLimited);
  const auto limited = state.delivery.claim(actor, simulation::TickSequence::zero());
  REQUIRE(limited.has_value());
  CHECK(std::get<runtime::MovementTuningAdmissionRefusal>(*limited).retry_after_milliseconds ==
        400);
  REQUIRE(state.mailbox.submit_tuning(fixture::request(3, actor),
                                      fixture::kStart + fixture::kJustBefore) ==
          Submission::kRejectedTuningRateLimited);
  const auto last_millisecond = state.delivery.claim(actor, simulation::TickSequence::zero());
  REQUIRE(last_millisecond.has_value());
  CHECK(std::get<runtime::MovementTuningAdmissionRefusal>(*last_millisecond)
            .retry_after_milliseconds == 1);
  CHECK(state.mailbox.submit_tuning(fixture::request(4, actor),
                                    fixture::kStart + fixture::kInterval) == Submission::kAccepted);
}

TEST_CASE(
    "movement tuning ID reuse precedes in-flight refusal and newer refused IDs advance high-water",
    "[unit][runtime][movement_tuning]") {
  fixture::Exchange state;
  const auto actor = state.sink.open_session("human", "tuning");
  REQUIRE(state.mailbox.submit_tuning(fixture::request(5, actor), fixture::kStart) ==
          Submission::kAccepted);
  CHECK(state.mailbox.submit_tuning(fixture::request(5, actor), fixture::kStart) ==
        Submission::kRejectedTuningRequestIdReused);
  CHECK(state.mailbox.submit_tuning(fixture::request(6, actor), fixture::kStart) ==
        Submission::kRejectedTuningRequestInFlight);
  CHECK(state.mailbox.submit_tuning(fixture::request(6, actor), fixture::kStart) ==
        Submission::kRejectedTuningRequestIdReused);
  static_cast<void>(state.mailbox.drain());
  fixture::complete(state.mailbox, fixture::decision(5, 2, actor));
  const auto result = state.delivery.claim(actor, simulation::TickSequence::create(2));
  REQUIRE(result.has_value());
  CHECK(std::get<simulation::MovementTuningDecision>(*result).tuning_request_id == 5);
  CHECK(state.mailbox.submit_tuning(fixture::request(6, actor),
                                    fixture::kStart + fixture::kInterval) ==
        Submission::kRejectedTuningRequestIdReused);
  CHECK(state.mailbox.submit_tuning(fixture::request(7, actor),
                                    fixture::kStart + fixture::kInterval) == Submission::kAccepted);
}

TEST_CASE(
    "movement tuning mailbox-full refusal owns a terminal result and starts the eligible deadline",
    "[unit][runtime][movement_tuning]") {
  fixture::Exchange state;
  const auto actor = state.sink.open_session("human", "tuning");
  for (std::size_t index = 0; index < runtime::kMaximumMailboxCommandCount; ++index) {
    REQUIRE(state.mailbox.submit(fixture::thrust(index + 1)) == Submission::kAccepted);
  }
  REQUIRE(state.mailbox.submit_tuning(fixture::request(1, actor), fixture::kStart) ==
          Submission::kDroppedMailboxFull);
  CHECK(state.mailbox.statistics().pending_command_count == runtime::kMaximumMailboxCommandCount);
  const auto result = state.delivery.claim(actor, simulation::TickSequence::zero());
  REQUIRE(result.has_value());
  const auto& refusal = std::get<runtime::MovementTuningAdmissionRefusal>(*result);
  CHECK(refusal.status == Admission::kMailboxFull);
  CHECK_FALSE(refusal.retry_after_milliseconds.has_value());
  static_cast<void>(state.mailbox.drain());
  CHECK(
      state.mailbox.submit_tuning(fixture::request(2, actor), fixture::kStart + fixture::kEarly) ==
      Submission::kRejectedTuningRateLimited);
}

TEST_CASE("movement tuning eviction by lifecycle traffic atomically retains an explicit refusal",
          "[unit][runtime][movement_tuning]") {
  fixture::Exchange state;
  const auto actor = state.sink.open_session("human", "tuning");
  REQUIRE_FALSE(runtime::is_entity_lifecycle_command(simulation::CommandKind::kSetMovementTuning));
  REQUIRE(state.mailbox.submit_tuning(fixture::request(1, actor), fixture::kStart) ==
          Submission::kAccepted);
  for (std::size_t index = 1; index < runtime::kMaximumMailboxCommandCount; ++index) {
    REQUIRE(state.mailbox.submit(fixture::thrust(index)) == Submission::kAccepted);
  }
  REQUIRE(state.mailbox.submit(simulation::SpawnCommand{fixture::controller(2)}) ==
          Submission::kAccepted);
  const auto result = state.delivery.claim(actor, simulation::TickSequence::zero());
  REQUIRE(result.has_value());
  CHECK(std::get<runtime::MovementTuningAdmissionRefusal>(*result).status ==
        Admission::kMailboxEvicted);
  const auto drained = state.mailbox.drain();
  CHECK(std::none_of(drained.begin(), drained.end(), [](const simulation::Command& command) {
    return std::holds_alternative<simulation::SetMovementTuningCommand>(command);
  }));
  CHECK(state.mailbox.statistics().dropped_command_count == 1);
}

TEST_CASE("movement tuning claim frees runtime B while late A completion and write release cannot "
          "clear it",
          "[unit][runtime][movement_tuning]") {
  fixture::Exchange state;
  const auto actor = state.sink.open_session("human", "tuning");
  REQUIRE(state.mailbox.submit_tuning(fixture::request(1, actor), fixture::kStart) ==
          Submission::kAccepted);
  static_cast<void>(state.mailbox.drain());
  fixture::complete(state.mailbox, fixture::decision(1, 2, actor));
  auto session_a = state.delivery.claim(actor, simulation::TickSequence::create(2));
  REQUIRE(session_a.has_value());
  REQUIRE(state.mailbox.submit_tuning(fixture::request(2, actor),
                                      fixture::kStart + fixture::kInterval) ==
          Submission::kAccepted);
  fixture::complete(state.mailbox, fixture::decision(1, 2, actor));
  CHECK_FALSE(state.delivery.claim(actor, simulation::TickSequence::create(5)));
  static_cast<void>(state.mailbox.drain());
  fixture::complete(state.mailbox, fixture::decision(2, 6, actor));
  session_a.reset(); // The late write callback releases only the session's owned A.
  CHECK_FALSE(state.delivery.claim(actor, simulation::TickSequence::create(5)));
  const auto session_b = state.delivery.claim(actor, simulation::TickSequence::create(7));
  REQUIRE(session_b.has_value());
  CHECK(std::get<simulation::MovementTuningDecision>(*session_b).tuning_request_id == 2);
  CHECK(state.mailbox.statistics().unresolved_tuning_exchange_count == 0);
}

TEST_CASE("movement tuning retirement discards late completion and enqueues Leave exactly once",
          "[unit][runtime][movement_tuning][concurrency]") {
  fixture::Exchange state;
  const auto actor = state.sink.open_session("human", "tuning");
  REQUIRE(state.mailbox.submit_tuning(fixture::request(1, actor), fixture::kStart) ==
          Submission::kAccepted);
  static_cast<void>(state.mailbox.drain());
  std::barrier race(2);
  std::jthread completer([&] {
    race.arrive_and_wait();
    fixture::complete(state.mailbox, fixture::decision(1, 2, actor));
  });
  race.arrive_and_wait();
  CHECK(state.sink.close_session(actor) == runtime::ControllerCloseResult::kClosed);
  completer.join();
  CHECK_FALSE(state.delivery.claim(actor, simulation::TickSequence::create(2)));
  CHECK(state.mailbox.submit_tuning(fixture::request(2, actor),
                                    fixture::kStart + fixture::kInterval) ==
        Submission::kRejectedSessionNotOpen);
  CHECK(state.sink.close_session(actor) == runtime::ControllerCloseResult::kUnknownControllerId);
  const auto drained = state.mailbox.drain();
  REQUIRE(drained.size() == 1);
  CHECK(std::holds_alternative<simulation::LeaveCommand>(drained.front()));
  CHECK(state.directory.size() == 0);
  CHECK(state.mailbox.statistics().open_tuning_exchange_count == 0);
}

TEST_CASE(
    "movement tuning session churn retains no exchanges and controller exhaustion never reuses IDs",
    "[unit][runtime][movement_tuning]") {
  fixture::Exchange state;
  std::uint64_t last = 0;
  for (std::size_t index = 0; index < runtime::kMaximumControllerDirectoryEntryCount + 1; ++index) {
    const auto actor = state.sink.open_session("human", "churn");
    REQUIRE(actor.value() > last);
    last = actor.value();
    REQUIRE(state.sink.close_session(actor) == runtime::ControllerCloseResult::kClosed);
    static_cast<void>(state.mailbox.drain());
  }
  CHECK(state.directory.size() == 0);
  CHECK(state.mailbox.statistics().open_tuning_exchange_count == 0);
  runtime::CommandSink final_sink{state.mailbox, state.directory, state.allocator,
                                  simulation::kMaximumControllerId};
  CHECK(final_sink.open_session("human", "last").value() == simulation::kMaximumControllerId);
  CHECK_THROWS_AS(final_sink.open_session("human", "exhausted"), runtime::CommandSinkError);
  CHECK_THROWS_AS(final_sink.open_session("human", "still_exhausted"), runtime::CommandSinkError);
  CHECK(final_sink.next_controller_id() == simulation::kMaximumControllerId + 1);
}

TEST_CASE("movement tuning exchange registration failure rolls back the presentation directory",
          "[unit][runtime][movement_tuning]") {
  fixture::Exchange state;
  REQUIRE(state.mailbox.register_controller(fixture::controller()));
  CHECK_THROWS_AS(state.sink.open_session("human", "conflict"), runtime::CommandSinkError);
  CHECK(state.directory.size() == 0);
  CHECK(state.mailbox.statistics().open_tuning_exchange_count == 1);
  CHECK(state.sink.next_controller_id() == 2);
}

TEST_CASE("movement tuning exchange entries have the existing directory concurrency bound",
          "[unit][runtime][movement_tuning]") {
  fixture::Exchange state;
  for (std::size_t index = 0; index < runtime::kMaximumControllerDirectoryEntryCount; ++index) {
    REQUIRE(state.mailbox.register_controller(fixture::controller(index + 1)));
  }
  CHECK_FALSE(state.mailbox.register_controller(
      fixture::controller(runtime::kMaximumControllerDirectoryEntryCount + 1)));
  CHECK(state.mailbox.statistics().open_tuning_exchange_count ==
        runtime::kMaximumControllerDirectoryEntryCount);
}

TEST_CASE(
    "movement tuning results and rate state are isolated between rooms with equal controller IDs",
    "[unit][runtime][movement_tuning]") {
  fixture::Exchange first;
  fixture::Exchange second;
  const auto actor = first.sink.open_session("human", "first");
  REQUIRE(second.sink.open_session("human", "second") == actor);
  REQUIRE(first.mailbox.submit_tuning(fixture::request(1, actor), fixture::kStart) ==
          Submission::kAccepted);
  REQUIRE(second.mailbox.submit_tuning(fixture::request(1, actor), fixture::kStart) ==
          Submission::kAccepted);
  static_cast<void>(first.mailbox.drain());
  fixture::complete(first.mailbox);
  CHECK(first.delivery.claim(actor, simulation::TickSequence::create(2)).has_value());
  CHECK_FALSE(second.delivery.claim(actor, simulation::TickSequence::create(2)));
}

TEST_CASE(
    "movement tuning runtime exposes a committed result only with a covering published snapshot",
    "[unit][runtime][movement_tuning]") {
  runtime::SimulationRuntime worker{fixture::game()};
  const auto actor = worker.command_sink().open_session("human", "runtime");
  REQUIRE(worker.command_sink().submit(actor, simulation::JoinCommand{actor, std::nullopt}) ==
          Submission::kAccepted);
  worker.start();
  REQUIRE(fixture::wait_until(
      [&] { return worker.snapshot_publication().latest()->tick_sequence().value() >= 1; }));
  worker.pause();
  const auto before = worker.snapshot_publication().latest();
  REQUIRE(worker.command_sink().submit(actor, fixture::request(1, actor)) == Submission::kAccepted);
  worker.resume();
  REQUIRE(fixture::wait_until([&] {
    return worker.snapshot_publication().latest()->tick_sequence() > before->tick_sequence();
  }));
  worker.pause();
  CHECK_FALSE(worker.tuning_result_delivery().claim(actor, before->tick_sequence()));
  const auto snapshot = worker.snapshot_publication().latest();
  const auto result = worker.tuning_result_delivery().claim(actor, snapshot->tick_sequence());
  REQUIRE(result.has_value());
  CHECK(std::get<simulation::MovementTuningDecision>(*result).status ==
        simulation::MovementTuningDecisionStatus::kApplied);
  CHECK(snapshot->match().movement().current == fixture::kPair);
  CHECK(snapshot->match().movement().revision == 1);
  worker.stop();
}

TEST_CASE("movement tuning runtime failure retains the prior snapshot and emits no application "
          "acknowledgment",
          "[unit][runtime][movement_tuning]") {
  runtime::SimulationRuntime worker{fixture::game(true)};
  const auto actor = worker.command_sink().open_session("human", "runtime_failure");
  REQUIRE(worker.command_sink().submit(actor, simulation::JoinCommand{actor, std::nullopt}) ==
          Submission::kAccepted);
  worker.start();
  REQUIRE(fixture::wait_until(
      [&] { return worker.snapshot_publication().latest()->tick_sequence().value() >= 1; }));
  worker.pause();
  const auto before = worker.snapshot_publication().latest();
  REQUIRE(worker.command_sink().submit(actor, fixture::request(1, actor)) == Submission::kAccepted);
  worker.resume();
  REQUIRE(worker.wait_for_state(runtime::SimulationRuntimeState::kFailed,
                                fixture::kObservationTimeout));
  CHECK(*worker.snapshot_publication().latest() == *before);
  CHECK_FALSE(worker.tuning_result_delivery().claim(
      actor, simulation::TickSequence::create(simulation::kMaximumProtocolSafeInteger)));
  CHECK_THROWS_AS(worker.rethrow_if_failed(), simulation::SimulationValidationError);
  worker.stop();
}
