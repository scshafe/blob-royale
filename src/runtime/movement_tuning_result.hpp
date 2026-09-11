#ifndef BLOB_ROYALE_RUNTIME_MOVEMENT_TUNING_RESULT_HPP
#define BLOB_ROYALE_RUNTIME_MOVEMENT_TUNING_RESULT_HPP

#include "movement_tuning_decision.hpp"

#include <cstdint>
#include <optional>
#include <variant>

namespace blob_royale::runtime {

enum class MovementTuningAdmissionStatus : std::uint8_t {
  kRateLimited = 0,
  kMailboxFull = 1,
  kMailboxEvicted = 2,
};

// No decision tick/revision exists for admission refusals. Only rate limiting carries retry time.
struct MovementTuningAdmissionRefusal final {
  simulation::ControllerId controller;
  std::uint64_t tuning_request_id;
  MovementTuningAdmissionStatus status;
  std::optional<std::uint64_t> retry_after_milliseconds;

  friend bool operator==(const MovementTuningAdmissionRefusal&,
                         const MovementTuningAdmissionRefusal&) = default;
};

// canonical: movement_tuning_result -- the runtime's closed committed/admission outcome.
// A server adapter translates to its protocol value; runtime and protocol remain independent.
using MovementTuningResult =
    std::variant<simulation::MovementTuningDecision, MovementTuningAdmissionRefusal>;

} // namespace blob_royale::runtime

#endif
