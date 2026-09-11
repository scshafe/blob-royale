#ifndef BLOB_ROYALE_SERVER_MOVEMENT_TUNING_RESULT_ADAPTER_HPP
#define BLOB_ROYALE_SERVER_MOVEMENT_TUNING_RESULT_ADAPTER_HPP

#include "movement_tuning_result.hpp"
#include "movement_tuning_wire_result.hpp"

namespace blob_royale::server {

// canonical: movement_tuning_result_adapter -- translates a claimed runtime result for its session.
// Refuses a foreign controller or unregistered status with SERVER.SESSION.INVARIANT_FAILED;
// the protocol value then validates numeric ranges and status-specific nullability.
[[nodiscard]] protocol::MovementTuningWireResult
movement_tuning_wire_result(const runtime::MovementTuningResult& result,
                            simulation::ControllerId session_controller);

} // namespace blob_royale::server

#endif
