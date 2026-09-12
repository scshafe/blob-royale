#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_SHIELD_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_SHIELD_COMPONENT_HPP

#include "component_kind_name.hpp"
#include "component_lifetime.hpp"
#include "simulation_validation_error.hpp"
#include "tick_sequence.hpp"
#include "tick_window.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace blob_royale::simulation {

// canonical: shield_component -- one body's committed tap-shield activation, as three absolute
// windows sharing one activation tick plus the stun duration that activation captured.
//
// **Three windows, one activation, no countdown.** Protection, the perfect opening, and the
// cooldown all begin on the tick the pulse was admitted, so the value carries one activation and
// three expiries rather than three independent timers. Every one of them is a `TickWindow`, the
// canonical half-open `[activation, expiry)` value every ability timer in this codebase is built
// from (`tick_window.hpp`); nothing here is decremented per tick, so a shield that is read
// twice in one tick answers the same both times and a shield read after a pause answers by the
// tick it is asked about rather than by how many ticks elapsed.
//
// **The perfect opening is the first part of the protection, not a separate defence.** It is
// constructed with the same activation and a duration that may not exceed the protection's, so
// `perfect_window()` is always a prefix of `shield_window()`. That nesting is what lets a contact
// response ask one question in one order -- perfect, then ordinary, then unguarded -- and is why
// `canceled_at` ends both at once rather than leaving a parry alive inside protection that has
// already been cancelled.
//
// **The captured parry-stun duration is a copy, taken at activation.** The defender supplies the
// effect it owns, so the stun a perfect parry inflicts is the one configured when the shield went
// up and not the one configured when the contact happened. A contact response reads it from the
// committed defender rather than from a mode's configuration, which is what keeps the response
// noncapturing: it needs no second configuration owner and no process global
// (`docs/reviews/2026-09-12-shield-composition-contract.md` § "One live response path").
//
// **Construction is private and validated**, so a stored Shield is always a positive activation,
// positive effect durations, and ordered protection endpoints. A structurally impossible
// activation is a named coded refusal here rather than a clamped value later.
//
// Every stored value is published verbatim: there is no `ComponentPublication<Shield>`
// specialization, because the current effect parameters of a live ability are public to browser
// readers and C++ bots alike rather than hidden from one and exposed to the other. Charge state is
// a later step's addition to this same owner and is deliberately absent here.
// related: ../../gameplay/shared/ability_system.hpp -- the only writer of an activation.
// related: ../../gameplay/shared/status_system.hpp -- the only caller of canceled_at.
// related: component_registry.hpp -- the closed list this kind is registered in.
// related: docs/protocol/schema/v3/shield-component.schema.json -- the wire shape.
class Shield final {
public:
  // The committed activation. `activation_tick` is the tick the pulse was admitted on and dates
  // all three windows.
  //
  // Throws SIMULATION.SHIELD_ACTIVATION_INVALID for a zero activation tick, a zero shield,
  // perfect, or parry-stun duration, or a perfect opening longer than the protection that
  // contains it; and SIMULATION.TICK_WINDOW_EXPIRY_OVERFLOW from `TickWindow::create` when an
  // endpoint would leave the exact tick domain, which stays a named refusal rather than
  // saturating to the maximum tick.
  //
  // The checks run in declared parameter order rather than as one combined predicate, so an
  // activation with two bad durations reports the first declared one instead of whichever a
  // compiler happened to evaluate first.
  //
  // **A zero cooldown is legal, and so is a cooldown shorter than the protection.** Admission
  // requires the prior protection *and* the prior cooldown to have ended, so a cooldown that ends
  // first never shortens the wait, and a cooldown of zero simply means the wait is the protection
  // alone (`docs/reviews/2026-09-12-shield-composition-contract.md` § "Authored tuning and value
  // ownership").
  [[nodiscard]] static Shield activate(const TickSequence activation_tick,
                                       const std::uint64_t shield_duration_ticks,
                                       const std::uint64_t perfect_duration_ticks,
                                       const std::uint64_t cooldown_duration_ticks,
                                       const std::uint64_t parry_stun_duration_ticks) {
    // Tick zero is the loaded initial state and no pulse is admitted on it, so an activation dated
    // zero is a caller that lost the tick rather than a shield raised at the start of the match.
    if (activation_tick == TickSequence::zero()) {
      throw SimulationValidationError(SimulationValidationCode::kShieldActivationInvalid,
                                      "shield.activation_tick",
                                      "a shield activation tick must be positive");
    }
    if (shield_duration_ticks == 0) {
      throw SimulationValidationError(
          SimulationValidationCode::kShieldActivationInvalid, "shield.shield_duration_ticks",
          "a shield duration of " + std::to_string(shield_duration_ticks) +
              " ticks protects nothing");
    }
    if (perfect_duration_ticks == 0) {
      throw SimulationValidationError(
          SimulationValidationCode::kShieldActivationInvalid, "shield.perfect_duration_ticks",
          "a perfect opening of " + std::to_string(perfect_duration_ticks) +
              " ticks can never be entered");
    }
    if (parry_stun_duration_ticks == 0) {
      throw SimulationValidationError(
          SimulationValidationCode::kShieldActivationInvalid, "shield.parry_stun_duration_ticks",
          "a parry stun duration of " + std::to_string(parry_stun_duration_ticks) +
              " ticks is a parry with no effect");
    }
    // The opening is a prefix of the protection, not an overlapping second window: a perfect
    // window outliving the shield would answer "perfect" for a body that is no longer guarded.
    if (perfect_duration_ticks > shield_duration_ticks) {
      throw SimulationValidationError(
          SimulationValidationCode::kShieldActivationInvalid, "shield.perfect_duration_ticks",
          "a perfect opening of " + std::to_string(perfect_duration_ticks) +
              " ticks is longer than the " + std::to_string(shield_duration_ticks) +
              " ticks of protection that contain it");
    }
    return Shield(TickWindow::create(activation_tick, shield_duration_ticks),
                  TickWindow::create(activation_tick, perfect_duration_ticks),
                  TickWindow::create(activation_tick, cooldown_duration_ticks),
                  parry_stun_duration_ticks);
  }

  Shield(const Shield&) = default;
  Shield(Shield&&) noexcept = default;
  Shield& operator=(const Shield&) = default;
  Shield& operator=(Shield&&) noexcept = default;
  ~Shield() = default;

  // The tick the pulse was admitted on, shared by all three windows. Cancellation never moves it,
  // so the activation is still the answer to "when did this shield go up?" after protection ends.
  [[nodiscard]] TickSequence activation_tick() const noexcept {
    return shield_window_.activation_tick();
  }

  [[nodiscard]] const TickWindow& shield_window() const noexcept { return shield_window_; }
  [[nodiscard]] const TickWindow& perfect_window() const noexcept { return perfect_window_; }
  [[nodiscard]] const TickWindow& cooldown_window() const noexcept { return cooldown_window_; }

  // The stun a perfect parry by this defender inflicts, captured from the ability configuration at
  // activation and never rewritten. Cancellation preserves it: a shield that was cut short still
  // records what it would have inflicted, and only a fresh activation re-reads the configuration.
  [[nodiscard]] std::uint64_t parry_stun_duration_ticks() const noexcept {
    return parry_stun_duration_ticks_;
  }

  // Ends still-active protection at `tick`, preserving the original activation, the already
  // elapsed part of the perfect opening, the cooldown window, and the captured stun duration.
  //
  // **Only protection ends.** The cooldown is what makes a cancelled shield cost something, so a
  // stun that cuts protection short does not also hand the defender an immediate second pulse; and
  // the activation stays put so the value keeps describing when the shield was raised rather than
  // when it was taken away.
  //
  // **Both protection windows end, because the perfect opening is part of the protection.** The
  // opening is a prefix of the shield window by construction, so a cancellation inside it shortens
  // both to the same tick. Preserving an unelapsed remainder instead would leave a cancelled
  // shield still answering "perfect" to a contact response, which is protection the cancellation
  // was supposed to remove. A cancellation after the opening has elapsed leaves it untouched,
  // because there is nothing still active to end.
  //
  // Cancelling at the activation tick legitimately yields empty protection -- a pulse that was
  // stunned on the tick it landed -- and the empty window is a real value, not a missing one.
  // Cancelling a shield whose protection is already empty or already expired is an exact no-op:
  // the returned value compares equal to this one, so a status system that cancels every tick of
  // a long stun writes the same value back every time.
  //
  // Throws SIMULATION.SHIELD_CANCELLATION_BEFORE_ACTIVATION for a `tick` before the activation.
  // That is a caller reading a tick out of order rather than a world state, so it is a named
  // chronology failure rather than a silently ignored cancellation.
  [[nodiscard]] Shield canceled_at(const TickSequence tick) const {
    if (tick < activation_tick()) {
      throw SimulationValidationError(SimulationValidationCode::kShieldCancellationBeforeActivation,
                                      "shield.cancellation_tick",
                                      "cancellation at tick " + std::to_string(tick.value()) +
                                          " precedes the shield activation at tick " +
                                          std::to_string(activation_tick().value()));
    }
    return Shield(ended_at(shield_window_, tick), ended_at(perfect_window_, tick), cooldown_window_,
                  parry_stun_duration_ticks_);
  }

  friend bool operator==(const Shield&, const Shield&) = default;

private:
  Shield(const TickWindow shield_window, const TickWindow perfect_window,
         const TickWindow cooldown_window, const std::uint64_t parry_stun_duration_ticks) noexcept
      : shield_window_(shield_window), perfect_window_(perfect_window),
        cooldown_window_(cooldown_window), parry_stun_duration_ticks_(parry_stun_duration_ticks) {}

  // One window shortened to `tick`, or the window unchanged when `tick` is not inside it. Built
  // through the same `TickWindow::create` an activation uses, so a shortened window is the same
  // validated value an activation of that length would have produced and the overflow rule is not
  // restated here. `contains` is half-open, so a window that already ends at or before `tick` --
  // including an empty one -- is returned by value and the whole operation is idempotent.
  [[nodiscard]] static TickWindow ended_at(const TickWindow& window, const TickSequence tick) {
    if (!window.contains(tick)) {
      return window;
    }
    return TickWindow::create(window.activation_tick(),
                              tick.value() - window.activation_tick().value());
  }

  TickWindow shield_window_;
  TickWindow perfect_window_;
  TickWindow cooldown_window_;
  std::uint64_t parry_stun_duration_ticks_;
};

template <> struct ComponentKindName<Shield> {
  static constexpr std::string_view value = "shield";
};

// Body-bound for the reason `Stun` is: it is a status window on a player's body, and a body that
// was destroyed leaves nothing for it to protect. The shared respawn sweep erases it with the
// body, so zero-delay return and round reset both clear it without a per-owner cleanup pass
// (`component_lifetime.hpp`).
template <> struct ComponentLifetime<Shield> {
  static constexpr bool bound_to_body = true;
};

} // namespace blob_royale::simulation

#endif
