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

// canonical: charge_component -- one body's committed one-shot charge, as the single absolute
// window its next activation has to wait out.
//
// **One window, because a charge has no duration.** The effect is an instantaneous additive
// velocity burst written on the tick the pulse was admitted, and by the time anything reads this
// value the burst has already been committed to the body's velocity. What survives the tick is the
// wait and nothing else, so the value is exactly one cooldown `TickWindow` -- the canonical
// half-open `[activation, expiry)` value every ability timer in this codebase is built from
// (`tick_window.hpp`). `Shield` carries three windows because a shield is a state a later contact
// has to ask about; a charge is an event, and a second window here would have to describe an
// effect that no longer exists (`docs/reviews/2026-09-12-charge-contract.md` § "The component and
// the command").
//
// **No captured effect parameter, unlike `Shield::parry_stun_duration_ticks()`.** The shield
// captures its stun duration because the effect it owns is inflicted by a *later* contact, so the
// defender has to carry the configuration forward. A charge's gain is spent in the same tick it is
// authorized, so a copy of `charge_speed_fraction` here would be state nobody reads and state
// nobody reads is state that drifts from the configuration it was copied from.
//
// **Both endpoints are published, never a countdown**, for the reason the shield encoder states: an
// absolute tick stays true in a frame a client buffered or received late, while a remaining-ticks
// number is wrong the moment it is delayed. The activation is published as well as the expiry
// because a cooldown arc needs the denominator and `charge_cooldown_seconds` is deliberately
// server-side, so a client that saw only the expiry could not say how much of the wait is left.
//
// **The cooldown is validated strictly positive, and the shield's exemption deliberately does not
// transfer.** `Shield::activate` accepts a zero cooldown because shield admission is gated twice --
// the prior protection *and* the prior cooldown must both have ended -- so a cooldown that rounds
// away leaves the protection as the wait. A charge has no protection window and therefore only one
// gate, so a cooldown of zero ticks would admit a burst on every tick, four hundred a second at the
// authored rate. The rule lives here rather than in the ability system that reads it, because a
// value that cannot exist should be unconstructable rather than refused by whichever caller
// remembers to check (`docs/reviews/2026-09-12-charge-contract.md` § "Authored tuning and value
// ownership").
//
// **There is no `canceled_at`: nothing cancels a cooldown.** A stun cancels shield *protection*
// because protection is a defence still in effect, and shield cancellation pointedly leaves the
// cooldown alone -- that is what makes a cut-short shield still cost something. A charge is all
// cooldown, so there is nothing for a cancellation to shorten, and declaring an always-idempotent
// method would advertise a capability no caller has and invite a future one to use it.
//
// **Construction is private and validated**, so a stored Charge is always a positive activation and
// a strictly positive cooldown. A structurally impossible activation is a named coded refusal here
// rather than a clamped value later.
//
// Every stored value is published verbatim: there is no `ComponentPublication<Charge>`
// specialization, because the current cooldown of a live ability is public to browser readers and
// C++ bots alike rather than hidden from one and exposed to the other, exactly as `Shield`'s is.
// related: ../../gameplay/shared/ability_system.hpp -- the only writer of an activation and the
// only remover of an expired cooldown.
// related: component_registry.hpp -- the closed list this kind is registered in.
// related: shield_component.hpp -- the other ability value, and every contrast drawn above.
// related: docs/protocol/schema/v3/charge-component.schema.json -- the wire shape.
class Charge final {
public:
  // The committed activation. `activation_tick` is the tick the burst was applied on and dates the
  // cooldown that follows it.
  //
  // Throws SIMULATION.CHARGE_ACTIVATION_INVALID for a zero activation tick or a zero cooldown
  // duration; and SIMULATION.TICK_WINDOW_EXPIRY_OVERFLOW from `TickWindow::create` when the
  // endpoint would leave the exact tick domain, which stays a named refusal rather than saturating
  // to the maximum tick.
  //
  // The checks run in declared parameter order rather than as one combined predicate, so an
  // activation that is wrong twice reports the first declared fault instead of whichever a compiler
  // happened to evaluate first.
  [[nodiscard]] static Charge activate(const TickSequence activation_tick,
                                       const std::uint64_t cooldown_duration_ticks) {
    // Tick zero is the loaded initial state and no pulse is admitted on it, so an activation dated
    // zero is a caller that lost the tick rather than a charge fired at the start of the match.
    if (activation_tick == TickSequence::zero()) {
      throw SimulationValidationError(SimulationValidationCode::kChargeActivationInvalid,
                                      "charge.activation_tick",
                                      "a charge activation tick must be positive");
    }
    // Strictly positive, unlike the shield's cooldown: this is the only gate between two bursts, so
    // an empty window here is not "the other gate is the wait" but "there is no wait at all".
    if (cooldown_duration_ticks == 0) {
      throw SimulationValidationError(
          SimulationValidationCode::kChargeActivationInvalid, "charge.cooldown_duration_ticks",
          "a charge cooldown of " + std::to_string(cooldown_duration_ticks) +
              " ticks is the only gate between two bursts and would admit one every tick");
    }
    return Charge(TickWindow::create(activation_tick, cooldown_duration_ticks));
  }

  Charge(const Charge&) = default;
  Charge(Charge&&) noexcept = default;
  Charge& operator=(const Charge&) = default;
  Charge& operator=(Charge&&) noexcept = default;
  ~Charge() = default;

  // The tick the burst was applied on, which is also the tick the cooldown starts from. It never
  // moves, so the activation is still the answer to "when did this body charge?" for the whole
  // wait, which is what makes it usable as the cooldown arc's denominator.
  [[nodiscard]] TickSequence activation_tick() const noexcept {
    return cooldown_window_.activation_tick();
  }

  [[nodiscard]] const TickWindow& cooldown_window() const noexcept { return cooldown_window_; }

  friend bool operator==(const Charge&, const Charge&) = default;

private:
  explicit Charge(const TickWindow cooldown_window) noexcept : cooldown_window_(cooldown_window) {}

  TickWindow cooldown_window_;
};

template <> struct ComponentKindName<Charge> {
  static constexpr std::string_view value = "charge";
};

// Body-bound for the reason `Shield` and `Stun` are: it is a per-body ability timer, and a body
// that was destroyed has no next activation to wait for. The shared respawn sweep erases it with
// the body, so zero-delay return and round reset both clear it without a per-owner cleanup pass
// (`component_lifetime.hpp`).
template <> struct ComponentLifetime<Charge> {
  static constexpr bool bound_to_body = true;
};

} // namespace blob_royale::simulation

#endif
