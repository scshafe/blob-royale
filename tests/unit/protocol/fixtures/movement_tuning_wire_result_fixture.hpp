#ifndef BLOB_ROYALE_PROTOCOL_TEST_FIXTURE_MOVEMENT_TUNING_WIRE_RESULT_FIXTURE_HPP
#define BLOB_ROYALE_PROTOCOL_TEST_FIXTURE_MOVEMENT_TUNING_WIRE_RESULT_FIXTURE_HPP

#include "movement_tuning_wire_result.hpp"
#include "protocol_constants.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace blob_royale::protocol::test_fixture {

struct MovementTuningWireResultInputs final {
  std::uint64_t tuning_request_id = 41;
  MovementTuningWireResultStatus status = MovementTuningWireResultStatus::kApplied;
  std::optional<std::uint64_t> decision_tick = 19;
  std::optional<std::uint64_t> revision = 7;
  std::optional<std::uint64_t> retry_after_milliseconds;

  [[nodiscard]] MovementTuningWireResult create() const {
    return MovementTuningWireResult::create(tuning_request_id, status, decision_tick, revision,
                                            retry_after_milliseconds);
  }
};

struct MovementTuningWireStatusFixture final {
  MovementTuningWireResultInputs inputs;
  std::string_view status_name;
};

inline constexpr std::array<MovementTuningWireStatusFixture, 8> kMovementTuningWireStatuses{{
    {{41, MovementTuningWireResultStatus::kApplied, 19, 7, std::nullopt}, "applied"},
    {{41, MovementTuningWireResultStatus::kSuperseded, 19, 7, std::nullopt}, "superseded"},
    {{41, MovementTuningWireResultStatus::kStaleRevision, 19, 0, std::nullopt}, "stale_revision"},
    {{41, MovementTuningWireResultStatus::kNotSeated, 19, 0, std::nullopt}, "not_seated"},
    {{41, MovementTuningWireResultStatus::kRevisionExhausted, 19, kMaximumSafeInteger,
      std::nullopt},
     "revision_exhausted"},
    {{41, MovementTuningWireResultStatus::kRateLimited, std::nullopt, std::nullopt, 500},
     "rate_limited"},
    {{41, MovementTuningWireResultStatus::kMailboxFull, std::nullopt, std::nullopt, std::nullopt},
     "mailbox_full"},
    {{41, MovementTuningWireResultStatus::kMailboxEvicted, std::nullopt, std::nullopt,
      std::nullopt},
     "mailbox_evicted"},
}};

[[nodiscard]] inline bool is_committed(const MovementTuningWireStatusFixture& fixture) noexcept {
  return fixture.inputs.decision_tick.has_value();
}

} // namespace blob_royale::protocol::test_fixture

#endif
