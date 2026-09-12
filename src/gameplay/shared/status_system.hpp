#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_STATUS_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_STATUS_SYSTEM_HPP

#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: status_system -- shared PostKernel stun application, expiry, input invalidation, and
// the cancellation of protection a stun ends.
// Body-loss cleanup belongs exclusively to GameWorld's body-bound lifetime sweep.
//
// **It also cancels a guard.** Every entity whose stun window contains this tick has any
// still-active Shield protection shortened to this tick, in the same final mutation loop that
// clears intent and acceleration. The original activation, already-elapsed perfect history, the
// cooldown window and the captured parry-stun duration all survive, so being stunned mid-guard
// costs the guard and not the cooldown (`../../simulation/components/shield_component.hpp`).
//
// **It never erases a Shield.** Cancelled-to-empty protection whose cooldown is still running is
// exactly the state that has to survive in order to refuse the next pulse. Only
// `shared/ability_system.hpp` removes a Shield, and only once protection and cooldown have both
// expired.
// related: shared/ability_system.hpp -- the other writer of a Shield, and its only remover.
class StatusSystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "status";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem> create();
  StatusSystem() = default;
  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  // Validates all applicable positive requests before any mutation. Throws
  // GAMEPLAY.STATUS_ACTIVATION_TICK_ZERO or SIMULATION.TICK_WINDOW_EXPIRY_OVERFLOW on rejection.
  // Zero duration and missing/bodyless/static targets are no-ops. Never changes velocity.
  // Cancels still-active protection for every stun window containing this tick; an entity with a
  // Shield and no stun, and one whose protection already expired, are both left untouched.
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;
};

} // namespace blob_royale::gameplay

#endif
