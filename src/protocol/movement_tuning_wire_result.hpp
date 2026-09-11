#ifndef BLOB_ROYALE_PROTOCOL_MOVEMENT_TUNING_WIRE_RESULT_HPP
#define BLOB_ROYALE_PROTOCOL_MOVEMENT_TUNING_WIRE_RESULT_HPP

#include <cstdint>
#include <optional>
#include <string_view>

namespace blob_royale::protocol {

enum class MovementTuningWireResultStatus : std::uint8_t {
  kApplied = 0,
  kSuperseded = 1,
  kStaleRevision = 2,
  kNotSeated = 3,
  kRevisionExhausted = 4,
  kRateLimited = 5,
  kMailboxFull = 6,
  kMailboxEvicted = 7,
};

// canonical: movement_tuning_wire_result -- a validated session-specific tuning result.
//
// Committed decisions carry their tick and resulting revision; admission refusals carry neither.
// Only a rate refusal carries a retry interval. Runtime ownership and controller routing do not
// belong to this protocol value; the server adapter supplies already claimed result fields.
class MovementTuningWireResult final {
public:
  static constexpr std::uint64_t kMinimumRequestIntervalMilliseconds = 500;

  // Validates the closed status, positive safe request id, committed positive safe tick and safe
  // revision, and status-specific nullability. Applied/superseded revisions are positive;
  // revision_exhausted is exactly the safe-integer ceiling. A rate retry is 1..500 milliseconds.
  // Throws PROTOCOL.ENCODING.MOVEMENT_TUNING_RESULT_INVALID with the exact rejected field context.
  [[nodiscard]] static MovementTuningWireResult
  create(std::uint64_t tuning_request_id, MovementTuningWireResultStatus status,
         std::optional<std::uint64_t> decision_tick, std::optional<std::uint64_t> revision,
         std::optional<std::uint64_t> retry_after_milliseconds);

  MovementTuningWireResult(const MovementTuningWireResult&) noexcept = default;
  MovementTuningWireResult(MovementTuningWireResult&&) noexcept = default;
  MovementTuningWireResult& operator=(const MovementTuningWireResult&) noexcept = default;
  MovementTuningWireResult& operator=(MovementTuningWireResult&&) noexcept = default;
  ~MovementTuningWireResult() = default;

  [[nodiscard]] std::uint64_t tuning_request_id() const noexcept { return tuning_request_id_; }
  [[nodiscard]] MovementTuningWireResultStatus status() const noexcept { return status_; }
  // This view refers to a process-lifetime literal, including when read from a temporary value.
  [[nodiscard]] std::string_view status_name() const noexcept { return status_name_; }
  [[nodiscard]] std::optional<std::uint64_t> decision_tick() const noexcept {
    return decision_tick_;
  }
  [[nodiscard]] std::optional<std::uint64_t> revision() const noexcept { return revision_; }
  [[nodiscard]] std::optional<std::uint64_t> retry_after_milliseconds() const noexcept {
    return retry_after_milliseconds_;
  }

  friend bool operator==(const MovementTuningWireResult&,
                         const MovementTuningWireResult&) = default;

private:
  MovementTuningWireResult(std::uint64_t tuning_request_id, MovementTuningWireResultStatus status,
                           std::string_view status_name, std::optional<std::uint64_t> decision_tick,
                           std::optional<std::uint64_t> revision,
                           std::optional<std::uint64_t> retry_after_milliseconds) noexcept;

  std::uint64_t tuning_request_id_;
  MovementTuningWireResultStatus status_;
  std::string_view status_name_;
  std::optional<std::uint64_t> decision_tick_;
  std::optional<std::uint64_t> revision_;
  std::optional<std::uint64_t> retry_after_milliseconds_;
};

} // namespace blob_royale::protocol

#endif
