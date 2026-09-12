#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_ABILITY_CONFIGURATION_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_ABILITY_CONFIGURATION_HPP

#include <cstdint>

namespace blob_royale::gameplay {

// canonical: ability_configuration -- the strict `[abilities]` section, validated once and held as
// the tick counts and dimensionless scalars an ability system consumes.
//
// **It lives in `shared/` because it belongs to no mode.** `hazards` and `movement` are the two
// members of `GameModeConfiguration` that already answer to no single game
// (`game_mode_configuration.hpp`), and an ability is the third: every mode declares the shared
// `ability` system, so filing this value under one mode's directory would make the other three
// reach into it. That is the rule `shared/duration_ticks.hpp` and `shared/hazard_archetype.hpp`
// were promoted under (`src/gameplay/README.md`).
//
// **Every duration is converted once, at load, and no value in seconds survives startup.**
// `shared/duration_ticks.hpp` states the rule and this value obeys it: `create` runs each of the
// five authored *durations* through `duration_ticks(seconds, "abilities.<key>")` and keeps only the
// resulting `std::uint64_t` tick counts. No system multiplies by the tick rate at runtime and
// nothing decrements a timer per tick, which is what keeps a clock out of the simulation
// (`docs/architecture/0004-gameplay-architecture.md` § "Determinism obligations for framework
// code"). The five accepted duration defaults are 160, 32, 360, 240 and 480 ticks at 400 Hz --
// exactly the counts `docs/architecture/0008-dynamic-arenas-and-combat.md` §§ "Timed shield and
// perfect opening" and "Charge" write out, so the ADR and this header cannot disagree without one
// of them being wrong.
//
// **Two of the seven stored values are `double`s, and that is not an exception to the rule above.**
// This header used to say the value "stores nothing but `std::uint64_t` tick counts", which was
// true while every key named a duration; Step 19's `charge_speed_fraction` and
// `charge_safety_envelope_speed` are not durations, so there is no tick count for them to become.
// The fraction is a dimensionless multiple of the *current* normal movement ceiling read from
// match state at activation, not an authored speed -- ADR 0008 says "0.75 times the current normal
// movement ceiling" and that ceiling is live-tunable (`../../simulation/movement_tuning_state.hpp`)
// -- and the envelope is a speed in world units per second. The rule that actually holds is the one
// restated above: *durations* convert once and never survive in seconds. A scalar that was never a
// duration is stored as authored, because rounding it to ticks would be a unit error.
//
// **A rejection names the section, not the bare key.** `duration_ticks` is shared and cannot know
// whose duration it is holding, so every call below passes the full `abilities.<key>` context: a
// reader of a `GAMEPLAY.DURATION_NEGATIVE` diagnostic is sent to the `[abilities]` section of the
// configuration file rather than left to guess which of a dozen sections authored the offending
// value (`shared/duration_ticks.hpp`).
//
// **Four of the five durations must be positive after rounding; the shield cooldown need not be.**
// An effect that lasts no tick is not a shorter effect, it is an absent one: a zero-length
// half-open window contains no tick at all (`../../simulation/tick_window.hpp`), so a shield,
// perfect or parry-stun duration that rounds to zero would publish a protection that could never
// protect and a parry that could never stun. The charge cooldown joins them for the different
// reason written out below. Each of the four is refused with
// `GAMEPLAY.ABILITY_DURATION_NOT_POSITIVE`.
//
// **A zero *shield* cooldown, and one shorter than the shield, are both legal and deliberate.**
// They are not a re-activation loophole, because admission requires **both** the prior protection
// and the cooldown to have ended before a new pulse is accepted (`shared/ability_system.hpp`). A
// zero cooldown therefore says "re-activate as soon as the previous protection ends", not
// "re-activate while it is still running"; a cooldown shorter than the shield simply expires first
// and leaves the still-active protection to refuse the pulse on its own. The alternative --
// requiring `cooldown >= shield` here -- would forbid an authored value whose meaning is well
// defined and would move an admission rule out of the system that owns it.
//
// **That exemption does not transfer to `charge_cooldown_seconds`, which must be positive.** The
// exemption above is justified entirely by the *second* gate: a shield's own protection window is
// still standing when its cooldown is short or zero, so something is always left to refuse the next
// pulse. Charge is one-shot. It has no protection window, no active window, and no captured effect
// parameter (`../../simulation/components/charge_component.hpp`), so its cooldown is the only gate
// it has, and a charge cooldown that rounds to zero ticks admits a burst on *every* tick -- four
// hundred activations a second at the canonical rate, each one additive. It is therefore refused
// with the same `GAMEPLAY.ABILITY_DURATION_NOT_POSITIVE` the three effect durations use: the code
// says "this rounded to nothing and nothing is not a legal setting", which is exactly the fault
// (`docs/reviews/2026-09-12-charge-contract.md` § "Authored tuning and value ownership").
//
// **The two charge scalars are validated where they are authored, not where they are used.** The
// fraction must be finite and strictly positive; the envelope must be finite, strictly positive and
// at most `kMaximumPhysicalComponentMagnitude`, because a burst the `Vector2` component domain
// cannot represent is not a large burst, it is a thrown tick. Both use
// `GAMEPLAY.ABILITY_SCALAR_NOT_FINITE` and `GAMEPLAY.ABILITY_SCALAR_OUT_OF_RANGE`, the
// owner-scoped pair every other section that authors a bare number already declares
// (`../royale/royale_configuration.hpp`, `shared/hazard_archetype.hpp`).
//
// **One cross-key rule bounds the fraction against the envelope**, and it is refused with
// `GAMEPLAY.ABILITY_CHARGE_BURST_EXCEEDS_SAFETY_ENVELOPE`:
// `charge_speed_fraction * kMaximumNormalTopSpeed <= charge_safety_envelope_speed`. In words: a
// charge from rest must stay admissible whatever a room tunes its ceiling to, up to the highest
// ceiling `MovementTuning` will accept. It is checked against that maximum rather than against the
// authored ceiling because the ceiling is live-tunable at runtime and this section is validated
// once at startup. Without it an unbounded fraction is a hole: `charge_speed_fraction=1e6` against
// a 10,000 wu/s ceiling is a 1e10 wu/s burst whose components still sit inside `Vector2`'s domain
// and which then exhausts `kMaximumMotionEventCount` on its first tick, and every exhaustion is
// fatal to the room. Bounding the fraction this way closes it without inventing an arbitrary
// ceiling on the fraction itself. The rule names `charge_speed_fraction` for the same reason the
// perfect-window rule names the window: the envelope is the bound the pair violates and the
// fraction is the key an operator has to change.
//
// **A perfect window longer than the shield it opens is refused** with
// `GAMEPLAY.ABILITY_PERFECT_WINDOW_EXCEEDS_SHIELD`. The perfect window is the opening *inside* the
// protection, sharing its activation tick (`../../simulation/components/shield_component.hpp`), so
// a perfect window that outlasted the shield would describe a parry still landing after the guard
// it belongs to had ended. The component would reject the same pair; refusing it here means an
// operator learns it at startup, naming the key, rather than on the first activation.
//
// **All seven numbers are ADR 0008 tuning proposals and one engineering guard, not owner-selected
// balance values.** The ADR calls them "hypotheses to tune with real latency, not a reproduction of
// Smash's exact mechanics", and the plan's 2026-09-10 amendment says the same of the quarter
// impulse. They are authored in the configuration file precisely so that tuning them is an
// operator's edit and not a rebuild, and nothing in the tree may treat 0.4/0.08/0.9/0.6 or
// 1.2/0.75/20000 as agreed balance. The envelope is the guard rather than a proposal: the owner
// accepted at Step 1 *that a separate validated safety envelope exists*, and has never selected its
// number (`docs/reviews/2026-09-12-charge-contract.md` § "Authored tuning and value ownership").
//
// **Charge joined this owner rather than founding a second one.** Step 18's header promised exactly
// that -- "Step 19 extends this same owner with charge's gain, cooldown and safety envelope" -- so
// there is no `[charge]` section, no second validated value, and no second place a mode has to
// thread through `GameModeConfiguration`. Every ability an entity can activate is admitted by one
// system (`shared/ability_system.hpp`), and one system reading one configuration is what lets it
// resolve a conflict between two abilities without a third place holding the priority.
// related: shared/duration_ticks.hpp -- the one seconds-to-ticks conversion the five durations use.
// related: shared/ability_system.hpp -- the system that reads these tick counts and scalars.
// related: ../../simulation/simulation_limits.hpp -- `kMaximumNormalTopSpeed`, the ceiling the
// cross-key rule bounds the fraction against, and the component domain the envelope may not leave.
// related: ../game_mode_configuration.hpp -- the aggregate that carries this section to a mode.
// related: ../gameplay_validation_error.hpp -- the `GAMEPLAY.ABILITY_*` rejections.
class AbilityConfiguration final {
public:
  static constexpr double kDefaultShieldDurationSeconds = 0.4;
  static constexpr double kDefaultShieldPerfectWindowSeconds = 0.08;
  static constexpr double kDefaultShieldCooldownSeconds = 0.9;
  static constexpr double kDefaultParryStunDurationSeconds = 0.6;
  static constexpr double kDefaultChargeCooldownSeconds = 1.2;
  static constexpr double kDefaultChargeSpeedFraction = 0.75;
  static constexpr double kDefaultChargeSafetyEnvelopeSpeed = 20'000.0;

  // The seven authored keys, in the order `[abilities]` declares them. Throws
  // GameplayValidationError: `GAMEPLAY.DURATION_NOT_FINITE`, `GAMEPLAY.DURATION_NEGATIVE` or
  // `GAMEPLAY.DURATION_TICK_OVERFLOW` from the shared conversion, then
  // `GAMEPLAY.ABILITY_DURATION_NOT_POSITIVE` for a shield, perfect, parry-stun or charge cooldown
  // duration that rounds to zero ticks, then `GAMEPLAY.ABILITY_SCALAR_NOT_FINITE` or
  // `GAMEPLAY.ABILITY_SCALAR_OUT_OF_RANGE` for either charge scalar, and last the two cross-key
  // rejections `GAMEPLAY.ABILITY_PERFECT_WINDOW_EXCEEDS_SHIELD` and
  // `GAMEPLAY.ABILITY_CHARGE_BURST_EXCEEDS_SAFETY_ENVELOPE`. Every rejection names
  // `abilities.<key>` and carries the offending value.
  //
  // Seven positional doubles rather than a section struct, which is what the four were: the
  // parameter list is the declared key order, and the one caller that reads a file assembles it
  // key by key from the same names (`../../application/application_config_loader.cpp`).
  [[nodiscard]] static AbilityConfiguration
  create(double shield_duration_seconds, double shield_perfect_window_seconds,
         double shield_cooldown_seconds, double parry_stun_duration_seconds,
         double charge_cooldown_seconds, double charge_speed_fraction,
         double charge_safety_envelope_speed);

  // The ADR's initial tuning: 0.4 s, 0.08 s, 0.9 s, 0.6 s and 1.2 s, which are 160, 32, 360, 240
  // and 480 ticks, with a 0.75 gain fraction and a 20,000 wu/s safety envelope.
  [[nodiscard]] static AbilityConfiguration defaults();

  [[nodiscard]] std::uint64_t shield_duration_ticks() const noexcept {
    return shield_duration_ticks_;
  }
  [[nodiscard]] std::uint64_t shield_perfect_window_ticks() const noexcept {
    return shield_perfect_window_ticks_;
  }
  [[nodiscard]] std::uint64_t shield_cooldown_ticks() const noexcept {
    return shield_cooldown_ticks_;
  }
  [[nodiscard]] std::uint64_t parry_stun_duration_ticks() const noexcept {
    return parry_stun_duration_ticks_;
  }
  [[nodiscard]] std::uint64_t charge_cooldown_ticks() const noexcept {
    return charge_cooldown_ticks_;
  }
  // Dimensionless. The burst speed is this times the **current** normal ceiling read from match
  // state at activation, never a stored speed, so a room that retunes its ceiling retunes its
  // charge with it (`shared/ability_system.cpp`).
  [[nodiscard]] double charge_speed_fraction() const noexcept { return charge_speed_fraction_; }
  // World units per second, the greatest post-burst speed the system will commit.
  [[nodiscard]] double charge_safety_envelope_speed() const noexcept {
    return charge_safety_envelope_speed_;
  }

  friend bool operator==(const AbilityConfiguration&, const AbilityConfiguration&) = default;

private:
  AbilityConfiguration(std::uint64_t shield_duration_ticks,
                       std::uint64_t shield_perfect_window_ticks,
                       std::uint64_t shield_cooldown_ticks, std::uint64_t parry_stun_duration_ticks,
                       std::uint64_t charge_cooldown_ticks, double charge_speed_fraction,
                       double charge_safety_envelope_speed) noexcept;

  std::uint64_t shield_duration_ticks_;
  std::uint64_t shield_perfect_window_ticks_;
  std::uint64_t shield_cooldown_ticks_;
  std::uint64_t parry_stun_duration_ticks_;
  std::uint64_t charge_cooldown_ticks_;
  double charge_speed_fraction_;
  double charge_safety_envelope_speed_;
};

} // namespace blob_royale::gameplay

#endif
