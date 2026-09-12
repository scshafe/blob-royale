#ifndef BLOB_ROYALE_SIMULATION_TICK_WINDOW_HPP
#define BLOB_ROYALE_SIMULATION_TICK_WINDOW_HPP

#include "tick_sequence.hpp"

#include <cstdint>

namespace blob_royale::simulation {

// canonical: tick_window -- validated absolute half-open simulation time, [activation, expiry).
class TickWindow final {
public:
  // Accepts empty and zero-origin windows. Throws SIMULATION.TICK_WINDOW_EXPIRY_OVERFLOW
  // before addition when duration would exceed the exact tick domain.
  [[nodiscard]] static TickWindow create(TickSequence activation_tick,
                                         std::uint64_t duration_ticks);

  [[nodiscard]] TickSequence activation_tick() const noexcept { return activation_tick_; }
  [[nodiscard]] TickSequence expiry_tick() const noexcept { return expiry_tick_; }
  [[nodiscard]] bool contains(TickSequence tick) const noexcept {
    return activation_tick_ <= tick && tick < expiry_tick_;
  }
  [[nodiscard]] bool expired(TickSequence tick) const noexcept { return tick >= expiry_tick_; }

  friend bool operator==(const TickWindow&, const TickWindow&) = default;

private:
  TickWindow(TickSequence activation_tick, TickSequence expiry_tick) noexcept;

  TickSequence activation_tick_;
  TickSequence expiry_tick_;
};

} // namespace blob_royale::simulation

#endif
