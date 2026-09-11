#ifndef BLOB_ROYALE_SIMULATION_COMMANDS_SET_MOVEMENT_TUNING_COMMAND_HPP
#define BLOB_ROYALE_SIMULATION_COMMANDS_SET_MOVEMENT_TUNING_COMMAND_HPP

#include "controller_id.hpp"
#include "movement_tuning.hpp"

#include <cstdint>

namespace blob_royale::simulation {

// canonical: set_movement_tuning_command -- one correlated, atomic room-wide tuning request.
// The boundary stamps controller. Request IDs correlate, never order commands; InputBatch checks
// ID in [1, safe maximum] and expected_revision in [0, safe maximum]. Tuning owns pair validation.
struct SetMovementTuningCommand final {
  ControllerId controller;
  std::uint64_t tuning_request_id;
  std::uint64_t expected_revision;
  MovementTuning tuning;

  friend bool operator==(const SetMovementTuningCommand&,
                         const SetMovementTuningCommand&) = default;
};

} // namespace blob_royale::simulation

#endif
