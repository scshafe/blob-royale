#include "movement_tuning_wire_result.hpp"

#include "protocol_constants.hpp"
#include "protocol_encoding_error.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace blob_royale::protocol {
namespace {

[[noreturn]] void reject_result(const std::string_view field, const std::string_view detail) {
  throw ProtocolEncodingError{ProtocolEncodingErrorCode::kMovementTuningResultInvalid,
                              "snapshot_message.data.tuning_result." + std::string(field),
                              std::string(detail)};
}

struct StatusDefinition final {
  std::string_view name;
  bool committed;
};

[[nodiscard]] StatusDefinition definition_of(const MovementTuningWireResultStatus status) {
  switch (status) {
  case MovementTuningWireResultStatus::kApplied:
    return {"applied", true};
  case MovementTuningWireResultStatus::kSuperseded:
    return {"superseded", true};
  case MovementTuningWireResultStatus::kStaleRevision:
    return {"stale_revision", true};
  case MovementTuningWireResultStatus::kNotSeated:
    return {"not_seated", true};
  case MovementTuningWireResultStatus::kRevisionExhausted:
    return {"revision_exhausted", true};
  case MovementTuningWireResultStatus::kRateLimited:
    return {"rate_limited", false};
  case MovementTuningWireResultStatus::kMailboxFull:
    return {"mailbox_full", false};
  case MovementTuningWireResultStatus::kMailboxEvicted:
    return {"mailbox_evicted", false};
  }
  reject_result("status", "status must name a registered movement tuning result");
}

} // namespace

MovementTuningWireResult MovementTuningWireResult::create(
    const std::uint64_t tuning_request_id, const MovementTuningWireResultStatus status,
    const std::optional<std::uint64_t> decision_tick, const std::optional<std::uint64_t> revision,
    const std::optional<std::uint64_t> retry_after_milliseconds) {
  const StatusDefinition definition = definition_of(status);
  if (tuning_request_id == 0 || tuning_request_id > kMaximumSafeInteger) {
    reject_result("tuning_request_id", "request id must be in the inclusive range 1 to 2^53-1");
  }

  if (definition.committed) {
    if (!decision_tick.has_value() || *decision_tick == 0 || *decision_tick > kMaximumSafeInteger) {
      reject_result("decision_tick",
                    "committed decision tick must be in the inclusive range 1 to 2^53-1");
    }
    if (!revision.has_value() || *revision > kMaximumSafeInteger) {
      reject_result("revision", "committed revision must be in the inclusive range 0 to 2^53-1");
    }
    if ((status == MovementTuningWireResultStatus::kApplied ||
         status == MovementTuningWireResultStatus::kSuperseded) &&
        *revision == 0) {
      reject_result("revision", "applied and superseded decisions require a positive revision");
    }
    if (status == MovementTuningWireResultStatus::kRevisionExhausted &&
        *revision != kMaximumSafeInteger) {
      reject_result("revision", "revision exhaustion requires revision 2^53-1");
    }
  } else {
    if (decision_tick.has_value()) {
      reject_result("decision_tick", "admission refusal decision tick must be null");
    }
    if (revision.has_value()) {
      reject_result("revision", "admission refusal revision must be null");
    }
  }

  if (status == MovementTuningWireResultStatus::kRateLimited) {
    if (!retry_after_milliseconds.has_value() || *retry_after_milliseconds == 0 ||
        *retry_after_milliseconds > kMinimumRequestIntervalMilliseconds) {
      reject_result("retry_after_milliseconds",
                    "rate-limited retry interval must be in the inclusive range 1 to 500");
    }
  } else if (retry_after_milliseconds.has_value()) {
    reject_result("retry_after_milliseconds",
                  "only a rate-limited refusal may carry a retry interval");
  }

  return MovementTuningWireResult{tuning_request_id, status,   definition.name,
                                  decision_tick,     revision, retry_after_milliseconds};
}

MovementTuningWireResult::MovementTuningWireResult(
    const std::uint64_t tuning_request_id, const MovementTuningWireResultStatus status,
    const std::string_view status_name, const std::optional<std::uint64_t> decision_tick,
    const std::optional<std::uint64_t> revision,
    const std::optional<std::uint64_t> retry_after_milliseconds) noexcept
    : tuning_request_id_(tuning_request_id), status_(status), status_name_(status_name),
      decision_tick_(decision_tick), revision_(revision),
      retry_after_milliseconds_(retry_after_milliseconds) {}

} // namespace blob_royale::protocol
