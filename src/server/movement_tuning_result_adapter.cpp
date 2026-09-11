#include "movement_tuning_result_adapter.hpp"

#include "game_server_error.hpp"
#include "protocol_v3_constants.hpp"
#include "runtime_limits.hpp"

#include <optional>
#include <type_traits>
#include <variant>

namespace blob_royale::server {
namespace {

using WireStatus = protocol::MovementTuningWireResultStatus;

static_assert(runtime::kMovementTuningMinimumIntervalMilliseconds ==
              protocol::kMovementTuningMinimumIntervalMilliseconds);
static_assert(protocol::MovementTuningWireResult::kMinimumRequestIntervalMilliseconds ==
              protocol::kMovementTuningMinimumIntervalMilliseconds);

[[noreturn]] void reject_status() {
  throw GameServerError{GameServerErrorCode::kSessionInvariantFailed,
                        "movement_tuning_result.status",
                        "runtime tuning result status is not registered"};
}

[[nodiscard]] WireStatus wire_status(const simulation::MovementTuningDecisionStatus status) {
  switch (status) {
  case simulation::MovementTuningDecisionStatus::kApplied:
    return WireStatus::kApplied;
  case simulation::MovementTuningDecisionStatus::kSuperseded:
    return WireStatus::kSuperseded;
  case simulation::MovementTuningDecisionStatus::kStaleRevision:
    return WireStatus::kStaleRevision;
  case simulation::MovementTuningDecisionStatus::kNotSeated:
    return WireStatus::kNotSeated;
  case simulation::MovementTuningDecisionStatus::kRevisionExhausted:
    return WireStatus::kRevisionExhausted;
  }
  reject_status();
}

[[nodiscard]] WireStatus wire_status(const runtime::MovementTuningAdmissionStatus status) {
  switch (status) {
  case runtime::MovementTuningAdmissionStatus::kRateLimited:
    return WireStatus::kRateLimited;
  case runtime::MovementTuningAdmissionStatus::kMailboxFull:
    return WireStatus::kMailboxFull;
  case runtime::MovementTuningAdmissionStatus::kMailboxEvicted:
    return WireStatus::kMailboxEvicted;
  }
  reject_status();
}

} // namespace

protocol::MovementTuningWireResult
movement_tuning_wire_result(const runtime::MovementTuningResult& result,
                            const simulation::ControllerId session_controller) {
  return std::visit(
      [session_controller]<typename Result>(const Result& value) {
        if (value.controller != session_controller) {
          throw GameServerError{GameServerErrorCode::kSessionInvariantFailed,
                                "movement_tuning_result.controller",
                                "claimed tuning result must belong to the receiving session"};
        }
        if constexpr (std::is_same_v<Result, simulation::MovementTuningDecision>) {
          return protocol::MovementTuningWireResult::create(
              value.tuning_request_id, wire_status(value.status), value.decision_tick.value(),
              value.revision, std::nullopt);
        } else {
          static_assert(std::is_same_v<Result, runtime::MovementTuningAdmissionRefusal>);
          return protocol::MovementTuningWireResult::create(
              value.tuning_request_id, wire_status(value.status), std::nullopt, std::nullopt,
              value.retry_after_milliseconds);
        }
      },
      result);
}

} // namespace blob_royale::server
