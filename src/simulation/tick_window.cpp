#include "tick_window.hpp"

#include "simulation_validation_error.hpp"

#include <string>

namespace blob_royale::simulation {

TickWindow TickWindow::create(const TickSequence activation_tick,
                              const std::uint64_t duration_ticks) {
  if (duration_ticks > TickSequence::kMaximumValue - activation_tick.value()) {
    throw SimulationValidationError(
        SimulationValidationCode::kTickWindowExpiryOverflow, "tick_window.expiry_tick",
        "activation tick " + std::to_string(activation_tick.value()) + " plus duration " +
            std::to_string(duration_ticks) + " exceeds the maximum exact tick");
  }
  return TickWindow(activation_tick,
                    TickSequence::create(activation_tick.value() + duration_ticks));
}

TickWindow::TickWindow(const TickSequence activation_tick, const TickSequence expiry_tick) noexcept
    : activation_tick_(activation_tick), expiry_tick_(expiry_tick) {}

} // namespace blob_royale::simulation
