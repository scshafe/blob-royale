#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_CHARGE_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_CHARGE_COMPONENT_HPP

#include "component_kind_name.hpp"
#include "component_lifetime.hpp"
#include "simulation_validation_error.hpp"
#include "tick_sequence.hpp"
#include "tick_window.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace blob_royale::simulation {

// canonical: charge_component -- a committed additive burst's cooldown and contact attempt.
// The burst changes velocity once. The independent half-open active window authorizes a later
// player impact, with its stun duration captured at activation. Active time may outlive cooldown:
// natural readiness permits a fresh activation to replace the previous attempt. A successful hit
// removes this value; blocking or stun cancels only the attempt and preserves its cooldown.
// related: ../../gameplay/shared/ability_system.hpp -- activation and expiry owner.
// related: ../../gameplay/shared/charge_contact_system.hpp -- first-impact commitment owner.
class Charge final {
public:
  // Explicit cooldown-only construction for historical timer fixtures. This has no active contact
  // attempt and captures no stun. Production activation must use the four-argument factory.
  // Throws SIMULATION.CHARGE_ACTIVATION_INVALID for zero activation/cooldown, or the canonical
  // TickWindow expiry overflow when the cooldown leaves the exact tick domain.
  [[nodiscard]] static Charge activate(const TickSequence activation_tick,
                                       const std::uint64_t cooldown_duration_ticks) {
    require_activation(activation_tick, cooldown_duration_ticks);
    return Charge(TickWindow::create(activation_tick, cooldown_duration_ticks),
                  TickWindow::create(activation_tick, 0), 0);
  }

  // Captures both windows and the positive hit-stun duration. Neither window constrains the other.
  // Throws SIMULATION.CHARGE_ACTIVATION_INVALID for any zero duration/activation or an unsafe
  // captured stun duration, and the canonical TickWindow expiry overflow for an endpoint.
  [[nodiscard]] static Charge activate(const TickSequence activation_tick,
                                       const std::uint64_t cooldown_duration_ticks,
                                       const std::uint64_t active_duration_ticks,
                                       const std::uint64_t hit_stun_duration_ticks) {
    require_activation(activation_tick, cooldown_duration_ticks);
    require_positive(active_duration_ticks, "charge.active_duration_ticks");
    require_positive(hit_stun_duration_ticks, "charge.hit_stun_duration_ticks");
    if (hit_stun_duration_ticks > TickSequence::kMaximumValue) {
      throw SimulationValidationError(SimulationValidationCode::kChargeActivationInvalid,
                                      "charge.hit_stun_duration_ticks",
                                      "charge hit stun duration exceeds the exact tick domain");
    }
    return Charge(TickWindow::create(activation_tick, cooldown_duration_ticks),
                  TickWindow::create(activation_tick, active_duration_ticks),
                  hit_stun_duration_ticks);
  }

  Charge(const Charge&) = default;
  Charge(Charge&&) noexcept = default;
  Charge& operator=(const Charge&) = default;
  Charge& operator=(Charge&&) noexcept = default;
  ~Charge() = default;

  [[nodiscard]] TickSequence activation_tick() const noexcept {
    return cooldown_window_.activation_tick();
  }
  [[nodiscard]] const TickWindow& cooldown_window() const noexcept { return cooldown_window_; }
  [[nodiscard]] const TickWindow& active_window() const noexcept { return active_window_; }
  [[nodiscard]] std::uint64_t hit_stun_duration_ticks() const noexcept {
    return hit_stun_duration_ticks_;
  }

  // Ends only a still-active contact attempt. Velocity, activation, cooldown and captured stun
  // remain unchanged. Cancelling an already-ended attempt is idempotent. Throws
  // SIMULATION.CHARGE_CANCELLATION_BEFORE_ACTIVATION for a tick preceding activation.
  [[nodiscard]] Charge canceled_at(const TickSequence tick) const {
    if (tick < activation_tick()) {
      throw SimulationValidationError(SimulationValidationCode::kChargeCancellationBeforeActivation,
                                      "charge.cancellation_tick",
                                      "cancellation precedes the charge activation");
    }
    return Charge(
        cooldown_window_,
        active_window_.contains(tick)
            ? TickWindow::create(activation_tick(), tick.value() - activation_tick().value())
            : active_window_,
        hit_stun_duration_ticks_);
  }

  friend bool operator==(const Charge&, const Charge&) = default;

private:
  Charge(const TickWindow cooldown_window, const TickWindow active_window,
         const std::uint64_t hit_stun_duration_ticks) noexcept
      : cooldown_window_(cooldown_window), active_window_(active_window),
        hit_stun_duration_ticks_(hit_stun_duration_ticks) {}

  static void require_positive(const std::uint64_t duration, const std::string_view context) {
    if (duration == 0) {
      throw SimulationValidationError(SimulationValidationCode::kChargeActivationInvalid,
                                      std::string(context), "charge duration must be positive");
    }
  }

  static void require_activation(const TickSequence tick, const std::uint64_t cooldown) {
    if (tick == TickSequence::zero()) {
      throw SimulationValidationError(SimulationValidationCode::kChargeActivationInvalid,
                                      "charge.activation_tick",
                                      "a charge activation tick must be positive");
    }
    require_positive(cooldown, "charge.cooldown_duration_ticks");
  }

  TickWindow cooldown_window_;
  TickWindow active_window_;
  std::uint64_t hit_stun_duration_ticks_;
};

template <> struct ComponentKindName<Charge> {
  static constexpr std::string_view value = "charge";
};

template <> struct ComponentLifetime<Charge> {
  static constexpr bool bound_to_body = true;
};

} // namespace blob_royale::simulation

#endif
