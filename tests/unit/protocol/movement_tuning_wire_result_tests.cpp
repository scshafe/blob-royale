#include "fixtures/movement_tuning_wire_result_fixture.hpp"

#include "movement_tuning_wire_result.hpp"
#include "protocol_constants.hpp"
#include "protocol_encoding_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace protocol = blob_royale::protocol;
namespace fixture = blob_royale::protocol::test_fixture;

namespace {

using Inputs = fixture::MovementTuningWireResultInputs;
using Result = protocol::MovementTuningWireResult;
using Status = protocol::MovementTuningWireResultStatus;

static_assert(!std::is_default_constructible_v<Result>);
static_assert(!std::is_aggregate_v<Result>);
static_assert(std::is_nothrow_copy_constructible_v<Result>);
static_assert(std::is_nothrow_move_constructible_v<Result>);
static_assert(std::is_nothrow_copy_assignable_v<Result>);
static_assert(std::is_nothrow_move_assignable_v<Result>);
static_assert(
    std::is_same_v<decltype(std::declval<Result>().decision_tick()), std::optional<std::uint64_t>>);
static_assert(
    std::is_same_v<decltype(std::declval<Result>().revision()), std::optional<std::uint64_t>>);
static_assert(std::is_same_v<decltype(std::declval<Result>().retry_after_milliseconds()),
                             std::optional<std::uint64_t>>);

void require_rejected_result(const Inputs& inputs, const std::string_view field,
                             const std::string_view detail) {
  try {
    static_cast<void>(inputs.create());
    FAIL("an invalid movement tuning result was constructed");
  } catch (const protocol::ProtocolEncodingError& error) {
    CHECK(error.error_code() == protocol::ProtocolEncodingErrorCode::kMovementTuningResultInvalid);
    CHECK(error.code() == "PROTOCOL.ENCODING.MOVEMENT_TUNING_RESULT_INVALID");
    CHECK(error.context() == "snapshot_message.data.tuning_result." + std::string(field));
    CHECK(error.detail() == detail);
  }
}

} // namespace

TEST_CASE("movement tuning wire result preserves every closed status and its nullable fields",
          "[unit][protocol][movement_tuning][candidate]") {
  for (const auto& specimen : fixture::kMovementTuningWireStatuses) {
    CAPTURE(specimen.status_name);
    const Result result = specimen.inputs.create();
    CHECK(result.tuning_request_id() == specimen.inputs.tuning_request_id);
    CHECK(result.status() == specimen.inputs.status);
    CHECK(result.status_name() == specimen.status_name);
    CHECK(result.decision_tick() == specimen.inputs.decision_tick);
    CHECK(result.revision() == specimen.inputs.revision);
    CHECK(result.retry_after_milliseconds() == specimen.inputs.retry_after_milliseconds);
  }
}

TEST_CASE("movement tuning wire result accepts exact request id boundaries for every status",
          "[unit][protocol][movement_tuning][candidate][boundary]") {
  for (const auto& specimen : fixture::kMovementTuningWireStatuses) {
    for (const std::uint64_t request_id : {std::uint64_t{1}, protocol::kMaximumSafeInteger}) {
      Inputs inputs = specimen.inputs;
      inputs.tuning_request_id = request_id;
      CAPTURE(specimen.status_name, request_id);
      CHECK(inputs.create().tuning_request_id() == request_id);
    }
  }
}

TEST_CASE("movement tuning wire result rejects zero and unsafe request ids with field context",
          "[unit][protocol][movement_tuning][candidate][rejection]") {
  for (const auto& specimen : fixture::kMovementTuningWireStatuses) {
    for (const std::uint64_t request_id : {std::uint64_t{0}, protocol::kMaximumSafeInteger + 1,
                                           std::numeric_limits<std::uint64_t>::max()}) {
      Inputs inputs = specimen.inputs;
      inputs.tuning_request_id = request_id;
      CAPTURE(specimen.status_name, request_id);
      require_rejected_result(inputs, "tuning_request_id",
                              "request id must be in the inclusive range 1 to 2^53-1");
    }
  }
}

TEST_CASE("movement tuning wire result rejects every unregistered underlying status value",
          "[unit][protocol][movement_tuning][candidate][rejection]") {
  for (std::uint16_t underlying = 8; underlying <= std::numeric_limits<std::uint8_t>::max();
       ++underlying) {
    Inputs inputs;
    inputs.status = static_cast<Status>(underlying);
    CAPTURE(underlying);
    require_rejected_result(inputs, "status",
                            "status must name a registered movement tuning result");
  }
}

TEST_CASE("movement tuning wire committed decisions accept exact tick and revision ceilings",
          "[unit][protocol][movement_tuning][candidate][boundary]") {
  for (const auto& specimen : fixture::kMovementTuningWireStatuses) {
    if (!fixture::is_committed(specimen)) {
      continue;
    }
    for (const std::uint64_t tick : {std::uint64_t{1}, protocol::kMaximumSafeInteger}) {
      Inputs inputs = specimen.inputs;
      inputs.decision_tick = tick;
      inputs.revision = protocol::kMaximumSafeInteger;
      CAPTURE(specimen.status_name, tick);
      const Result result = inputs.create();
      CHECK(result.decision_tick() == tick);
      CHECK(result.revision() == protocol::kMaximumSafeInteger);
    }
  }
}

TEST_CASE("movement tuning wire committed decisions reject missing zero and unsafe ticks",
          "[unit][protocol][movement_tuning][candidate][rejection]") {
  for (const auto& specimen : fixture::kMovementTuningWireStatuses) {
    if (!fixture::is_committed(specimen)) {
      continue;
    }
    for (const std::optional<std::uint64_t> tick :
         {std::optional<std::uint64_t>{}, std::optional<std::uint64_t>{0},
          std::optional<std::uint64_t>{protocol::kMaximumSafeInteger + 1},
          std::optional<std::uint64_t>{std::numeric_limits<std::uint64_t>::max()}}) {
      Inputs inputs = specimen.inputs;
      inputs.decision_tick = tick;
      CAPTURE(specimen.status_name);
      require_rejected_result(inputs, "decision_tick",
                              "committed decision tick must be in the inclusive range 1 to 2^53-1");
    }
  }
}

TEST_CASE("movement tuning wire committed decisions reject missing and unsafe revisions",
          "[unit][protocol][movement_tuning][candidate][rejection]") {
  for (const auto& specimen : fixture::kMovementTuningWireStatuses) {
    if (!fixture::is_committed(specimen)) {
      continue;
    }
    for (const std::optional<std::uint64_t> revision :
         {std::optional<std::uint64_t>{},
          std::optional<std::uint64_t>{protocol::kMaximumSafeInteger + 1},
          std::optional<std::uint64_t>{std::numeric_limits<std::uint64_t>::max()}}) {
      Inputs inputs = specimen.inputs;
      inputs.revision = revision;
      CAPTURE(specimen.status_name);
      require_rejected_result(inputs, "revision",
                              "committed revision must be in the inclusive range 0 to 2^53-1");
    }
  }
}

TEST_CASE("movement tuning wire applied and superseded results require a positive revision",
          "[unit][protocol][movement_tuning][candidate][rejection]") {
  for (const Status status : {Status::kApplied, Status::kSuperseded}) {
    Inputs inputs;
    inputs.status = status;
    inputs.revision = 0;
    require_rejected_result(inputs, "revision",
                            "applied and superseded decisions require a positive revision");
    inputs.revision = 1;
    CHECK(inputs.create().revision() == 1);
  }
}

TEST_CASE("movement tuning wire exhaustion requires the exact maximum revision",
          "[unit][protocol][movement_tuning][candidate][rejection]") {
  for (const std::uint64_t revision :
       {std::uint64_t{0}, std::uint64_t{1}, protocol::kMaximumSafeInteger - 1}) {
    Inputs inputs;
    inputs.status = Status::kRevisionExhausted;
    inputs.revision = revision;
    require_rejected_result(inputs, "revision", "revision exhaustion requires revision 2^53-1");
  }
}

TEST_CASE("movement tuning wire admission refusals cannot claim a decision tick or revision",
          "[unit][protocol][movement_tuning][candidate][rejection]") {
  for (const auto& specimen : fixture::kMovementTuningWireStatuses) {
    if (fixture::is_committed(specimen)) {
      continue;
    }
    for (const std::uint64_t value :
         {std::uint64_t{0}, std::uint64_t{1}, protocol::kMaximumSafeInteger,
          std::numeric_limits<std::uint64_t>::max()}) {
      Inputs inputs = specimen.inputs;
      inputs.decision_tick = value;
      CAPTURE(specimen.status_name, value);
      require_rejected_result(inputs, "decision_tick",
                              "admission refusal decision tick must be null");
      inputs = specimen.inputs;
      inputs.revision = value;
      require_rejected_result(inputs, "revision", "admission refusal revision must be null");
    }
  }
}

TEST_CASE(
    "movement tuning wire retry interval accepts exact one and five hundred millisecond bounds",
    "[unit][protocol][movement_tuning][candidate][boundary]") {
  CHECK(Result::kMinimumRequestIntervalMilliseconds == 500);
  for (const std::uint64_t retry : {std::uint64_t{1}, std::uint64_t{500}}) {
    Inputs inputs;
    inputs.status = Status::kRateLimited;
    inputs.decision_tick.reset();
    inputs.revision.reset();
    inputs.retry_after_milliseconds = retry;
    CHECK(inputs.create().retry_after_milliseconds() == retry);
  }
}

TEST_CASE("movement tuning wire rate refusal rejects missing zero and oversized retry intervals",
          "[unit][protocol][movement_tuning][candidate][rejection]") {
  for (const std::optional<std::uint64_t> retry :
       {std::optional<std::uint64_t>{}, std::optional<std::uint64_t>{0},
        std::optional<std::uint64_t>{501},
        std::optional<std::uint64_t>{std::numeric_limits<std::uint64_t>::max()}}) {
    Inputs inputs;
    inputs.status = Status::kRateLimited;
    inputs.decision_tick.reset();
    inputs.revision.reset();
    inputs.retry_after_milliseconds = retry;
    require_rejected_result(inputs, "retry_after_milliseconds",
                            "rate-limited retry interval must be in the inclusive range 1 to 500");
  }
}

TEST_CASE("movement tuning wire nonrate results reject even zero retry intervals",
          "[unit][protocol][movement_tuning][candidate][rejection]") {
  for (const auto& specimen : fixture::kMovementTuningWireStatuses) {
    if (specimen.inputs.status == Status::kRateLimited) {
      continue;
    }
    for (const std::uint64_t retry : {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{500},
                                      std::numeric_limits<std::uint64_t>::max()}) {
      Inputs inputs = specimen.inputs;
      inputs.retry_after_milliseconds = retry;
      CAPTURE(specimen.status_name, retry);
      require_rejected_result(inputs, "retry_after_milliseconds",
                              "only a rate-limited refusal may carry a retry interval");
    }
  }
}

TEST_CASE("movement tuning wire result copies and moves preserve owned nullable state",
          "[unit][protocol][movement_tuning][candidate][ownership]") {
  for (const auto& specimen : fixture::kMovementTuningWireStatuses) {
    const Result original = specimen.inputs.create();
    Result copied = original;
    Result moved = std::move(copied);
    CHECK(moved == original);
    CHECK(copied == original);
    Result copy_assigned = Inputs{}.create();
    copy_assigned = original;
    CHECK(copy_assigned == original);
    Result move_assigned = Inputs{}.create();
    move_assigned = std::move(moved);
    CHECK(move_assigned == original);
    CHECK(moved == original);
    const std::string_view literal_after_temporary = specimen.inputs.create().status_name();
    CHECK(literal_after_temporary == specimen.status_name);
  }
}
