#include "movement_tuning_result_delivery.hpp"

#include "command_mailbox.hpp"

namespace blob_royale::runtime {

std::optional<MovementTuningResult>
MovementTuningResultDelivery::claim(const simulation::ControllerId controller,
                                    const simulation::TickSequence covering_snapshot_tick) {
  return mailbox_->claim_tuning_result(controller, covering_snapshot_tick);
}

} // namespace blob_royale::runtime
