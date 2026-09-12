#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_STATUS_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_STATUS_SYSTEM_HPP

#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: status_system -- shared PostKernel stun application, expiry, and input invalidation.
// Body-loss cleanup belongs exclusively to GameWorld's body-bound lifetime sweep.
class StatusSystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "status";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem> create();
  StatusSystem() = default;
  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  // Validates all applicable positive requests before any mutation. Throws
  // GAMEPLAY.STATUS_ACTIVATION_TICK_ZERO or SIMULATION.TICK_WINDOW_EXPIRY_OVERFLOW on rejection.
  // Zero duration and missing/bodyless/static targets are no-ops. Never changes velocity.
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;
};

} // namespace blob_royale::gameplay

#endif
