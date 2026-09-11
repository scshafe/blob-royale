#ifndef BLOB_ROYALE_SIMULATION_MOVEMENT_TUNING_DECISION_HPP
#define BLOB_ROYALE_SIMULATION_MOVEMENT_TUNING_DECISION_HPP

#include "controller_id.hpp"
#include "tick_sequence.hpp"

#include <cstdint>

namespace blob_royale::simulation {

enum class MovementTuningDecisionStatus : std::uint8_t {
  kApplied = 0,
  kSuperseded = 1,
  kStaleRevision = 2,
  kNotSeated = 3,
  kRevisionExhausted = 4,
};

// canonical: movement_tuning_decision -- one successful tick's decision for a canonical request.
// Every decision carries the tick's final committed revision, including refused contenders.
struct MovementTuningDecision final {
  ControllerId controller;
  std::uint64_t tuning_request_id;
  MovementTuningDecisionStatus status;
  TickSequence decision_tick;
  std::uint64_t revision;

  friend bool operator==(const MovementTuningDecision&, const MovementTuningDecision&) = default;
};

} // namespace blob_royale::simulation

#endif
