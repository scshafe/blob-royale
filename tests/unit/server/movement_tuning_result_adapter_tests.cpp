#include "command_mailbox.hpp"
#include "game_server_error.hpp"
#include "movement_tuning_result_adapter.hpp"
#include "movement_tuning_result_delivery.hpp"
#include "runtime_limits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

namespace server = blob_royale::server;
namespace protocol = blob_royale::protocol;
namespace runtime = blob_royale::runtime;
namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::ControllerId controller() { return simulation::ControllerId::create(3); }
[[nodiscard]] simulation::SetMovementTuningCommand command(const std::uint64_t id,
                                                           const std::uint64_t revision) {
  return {controller(), id, revision, simulation::MovementTuning::create(500, 700)};
}
[[nodiscard]] simulation::MovementTuningDecision
decision(const std::uint64_t id, const std::uint64_t tick, const std::uint64_t revision) {
  return {controller(), id, simulation::MovementTuningDecisionStatus::kApplied,
          simulation::TickSequence::create(tick), revision};
}

} // namespace

TEST_CASE("tuning adapter translates every committed status without exposing its controller",
          "[unit][server][movement_tuning][adapter]") {
  using Status = simulation::MovementTuningDecisionStatus;
  for (const auto& [status, name] : {std::pair{Status::kApplied, std::string_view{"applied"}},
                                     {Status::kSuperseded, "superseded"},
                                     {Status::kStaleRevision, "stale_revision"},
                                     {Status::kNotSeated, "not_seated"},
                                     {Status::kRevisionExhausted, "revision_exhausted"}}) {
    auto committed = decision(
        1, 2, status == Status::kRevisionExhausted ? simulation::kMaximumProtocolSafeInteger : 1);
    committed.status = status;
    const auto result = server::movement_tuning_wire_result(committed, controller());
    CHECK(result.status_name() == name);
    CHECK(result.decision_tick() == 2);
    CHECK(result.revision() == committed.revision);
    CHECK_FALSE(result.retry_after_milliseconds().has_value());
  }
}

TEST_CASE("tuning adapter preserves admission nullability and only rate limiting retry time",
          "[unit][server][movement_tuning][adapter]") {
  using Status = runtime::MovementTuningAdmissionStatus;
  for (const auto& [status, name] :
       {std::pair{Status::kRateLimited, std::string_view{"rate_limited"}},
        {Status::kMailboxFull, "mailbox_full"},
        {Status::kMailboxEvicted, "mailbox_evicted"}}) {
    const runtime::MovementTuningAdmissionRefusal refusal{
        controller(), 1, status,
        status == Status::kRateLimited ? std::optional<std::uint64_t>{1} : std::nullopt};
    const auto result = server::movement_tuning_wire_result(refusal, controller());
    CHECK(result.status_name() == name);
    CHECK_FALSE(result.decision_tick().has_value());
    CHECK_FALSE(result.revision().has_value());
    CHECK(result.retry_after_milliseconds() == refusal.retry_after_milliseconds);
  }
}

TEST_CASE("tuning adapter rejects foreign controllers and invalid runtime status enums",
          "[unit][server][movement_tuning][adapter][rejection]") {
  CHECK_THROWS_AS(
      server::movement_tuning_wire_result(decision(1, 1, 1), simulation::ControllerId::create(4)),
      server::GameServerError);
  auto invalid = decision(1, 1, 1);
  invalid.status = static_cast<simulation::MovementTuningDecisionStatus>(255);
  CHECK_THROWS_AS(server::movement_tuning_wire_result(invalid, controller()),
                  server::GameServerError);
  const runtime::MovementTuningAdmissionRefusal refusal{
      controller(), 1, static_cast<runtime::MovementTuningAdmissionStatus>(255), std::nullopt};
  CHECK_THROWS_AS(server::movement_tuning_wire_result(refusal, controller()),
                  server::GameServerError);
}

TEST_CASE("releasing session owned result A cannot clear a newer runtime result B",
          "[unit][server][movement_tuning][ownership]") {
  runtime::CommandMailbox mailbox{simulation::CommandKindMask::all()};
  runtime::CommandMailbox other_room{simulation::CommandKindMask::all()};
  runtime::MovementTuningResultDelivery delivery{mailbox};
  runtime::MovementTuningResultDelivery other_delivery{other_room};
  REQUIRE(mailbox.register_controller(controller()));
  REQUIRE(other_room.register_controller(controller()));
  const auto now = runtime::CommandMailbox::Clock::time_point{};
  REQUIRE(mailbox.submit_tuning(command(1, 0), now) == runtime::CommandSubmissionResult::kAccepted);
  static_cast<void>(mailbox.drain());
  const std::array first{decision(1, 1, 1)};
  mailbox.complete_tuning_decisions(first);
  auto active_write_result = delivery.claim(controller(), simulation::TickSequence::create(1));
  REQUIRE(active_write_result.has_value());
  CHECK(
      server::movement_tuning_wire_result(*active_write_result, controller()).tuning_request_id() ==
      1);
  REQUIRE(
      mailbox.submit_tuning(
          command(2, 1),
          now + std::chrono::milliseconds{runtime::kMovementTuningMinimumIntervalMilliseconds}) ==
      runtime::CommandSubmissionResult::kAccepted);
  static_cast<void>(mailbox.drain());
  const std::array second{decision(2, 2, 2)};
  mailbox.complete_tuning_decisions(second);
  CHECK_FALSE(delivery.claim(controller(), simulation::TickSequence::create(1)).has_value());
  CHECK_FALSE(other_delivery.claim(controller(), simulation::TickSequence::create(2)).has_value());
  // Both successful and failed write completion destroy only this owned A. No runtime ack exists.
  active_write_result.reset();
  const auto later = delivery.claim(controller(), simulation::TickSequence::create(3));
  REQUIRE(later.has_value());
  CHECK(server::movement_tuning_wire_result(*later, controller()).tuning_request_id() == 2);
  CHECK_FALSE(delivery.claim(controller(), simulation::TickSequence::create(3)).has_value());
}
