#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_ABILITY_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_ABILITY_SYSTEM_HPP

#include "shared/ability_configuration.hpp"
#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: ability_system -- one PreKernel owner for shield and additive charge activation.
// Admission requires a running match, positive tick, a dynamic controlled body, matching input
// generation, and the shared unlocked-input state. Own protection blocks either ability; cooldowns
// are independent. An eligible shield wins simultaneous pulses. Invalid directions or a resulting
// velocity outside the safety envelope silently refuse charge without consuming its cooldown.
// Held brakes and zero live charge strength also refuse charge, while shielding remains available.
// Charge captures independent active/cooldown windows and hit stun; active time never blocks
// natural cooldown readiness. Expiry cleanup retains either ability until all its windows have
// ended. related: shared/charge_contact_system.hpp -- commits frozen contact candidates after the
// kernel. related: shared/status_system.hpp -- cancels effects while preserving velocity and
// cooldowns.
class AbilitySystem final : public simulation::SimulationSystem {
public:
  // The stable name the pipeline, diagnostics, and fixtures know this system by. It is `ability`
  // rather than `shield` because charge joins the same system rather than founding a new one.
  static constexpr std::string_view kSystemName = "ability";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(AbilityConfiguration configuration);

  // Public because the pipeline holds `std::unique_ptr<const SimulationSystem>` and
  // `std::make_unique` needs an accessible constructor, exactly as `ZoneShrinkSystem` does.
  explicit AbilitySystem(AbilityConfiguration configuration) noexcept;

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  // **Never throws for any world a client can reach**, and this is a hard requirement rather than
  // a quality of the current implementation. A throw here escapes `GameSimulation::step`, the
  // runtime worker then records a worker failure and returns, and the simulation thread stops
  // permanently: one client's ability pulse would end the match for everyone in the room. So every
  // gate refuses rather than rejects; the direction is normalized through an optional
  // (`shared/locomotion.hpp`); and the safety envelope is checked in raw doubles *before* any
  // `Vector2` is constructed, because `Vector2` throws on a component past its domain.
  //
  // The two activations it does perform were proven constructible when the configuration was
  // validated: `shared/ability_configuration.hpp` requires the same positivity and ordering that
  // `Shield::activate` and `Charge::activate` enforce, and the strictly positive charge cooldown
  // the latter demands is exactly the rule the shield's zero-cooldown exemption does not transfer
  // to. A SIMULATION.SHIELD_ACTIVATION_INVALID or SIMULATION.CHARGE_ACTIVATION_INVALID from here
  // would therefore mean the configuration and the component disagree, which is a fault worth
  // surfacing, not absorbing.
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

private:
  // Held **by value**: the engine destroys a mode as soon as it has read the mode's declarations
  // (`../../simulation/game_mode.hpp`), so nothing a system reads may point back at one.
  AbilityConfiguration configuration_;
};

} // namespace blob_royale::gameplay

#endif
