#ifndef BLOB_ROYALE_RUNTIME_MOVEMENT_TUNING_RESULT_DELIVERY_HPP
#define BLOB_ROYALE_RUNTIME_MOVEMENT_TUNING_RESULT_DELIVERY_HPP

#include "movement_tuning_result.hpp"

#include <optional>

namespace blob_royale::runtime {

class CommandMailbox;

// canonical: movement_tuning_result_delivery -- consumes one controller's covered terminal result.
// Claim transfers ownership and frees the runtime slot before encoding/write. A session callback
// releases only its claimed value, never calls back here to clear a possibly newer exchange.
class MovementTuningResultDelivery final {
public:
  explicit MovementTuningResultDelivery(CommandMailbox& mailbox) noexcept : mailbox_(&mailbox) {}

  [[nodiscard]] std::optional<MovementTuningResult>
  claim(simulation::ControllerId controller, simulation::TickSequence covering_snapshot_tick);

private:
  CommandMailbox* mailbox_;
};

} // namespace blob_royale::runtime

#endif
