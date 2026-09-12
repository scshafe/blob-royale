#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_ABILITY_CONFIGURATION_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_ABILITY_CONFIGURATION_HPP

#include <cstdint>

namespace blob_royale::gameplay {

// canonical: ability_configuration -- the strict `[abilities]` section, validated once and held as
// the tick counts an ability system consumes.
//
// **It lives in `shared/` because it belongs to no mode.** `hazards` and `movement` are the two
// members of `GameModeConfiguration` that already answer to no single game
// (`game_mode_configuration.hpp`), and an ability is the third: every mode declares the shared
// `ability` system, so filing this value under one mode's directory would make the other three
// reach into it. That is the rule `shared/duration_ticks.hpp` and `shared/hazard_archetype.hpp`
// were promoted under (`src/gameplay/README.md`).
//
// **Every duration is converted once, at load.** `shared/duration_ticks.hpp` states the rule and
// this value obeys it: `create` runs the four authored seconds through
// `duration_ticks(seconds, "abilities.<key>")` and stores nothing but `std::uint64_t` tick counts.
// No system multiplies by the tick rate at runtime, nothing decrements a timer per tick, and no
// value in seconds survives startup, which is what keeps a clock out of the simulation
// (`docs/architecture/0004-gameplay-architecture.md` § "Determinism obligations for framework
// code"). The four accepted defaults are 160, 32, 360 and 240 ticks at 400 Hz -- exactly the counts
// `docs/architecture/0008-dynamic-arenas-and-combat.md` § "Timed shield and perfect opening" writes
// out, so the ADR and this header cannot disagree without one of them being wrong.
//
// **A rejection names the section, not the bare key.** `duration_ticks` is shared and cannot know
// whose duration it is holding, so every call below passes the full `abilities.<key>` context: a
// reader of a `GAMEPLAY.DURATION_NEGATIVE` diagnostic is sent to the `[abilities]` section of the
// configuration file rather than left to guess which of a dozen sections authored the offending
// value (`shared/duration_ticks.hpp`).
//
// **The three effect durations must be positive after rounding; the cooldown need not be.** An
// effect that lasts no tick is not a shorter effect, it is an absent one: a zero-length half-open
// window contains no tick at all (`../../simulation/tick_window.hpp`), so a shield, perfect or
// parry-stun duration that rounds to zero would publish a protection that could never protect and
// a parry that could never stun. Each of the three is therefore refused with
// `GAMEPLAY.ABILITY_DURATION_NOT_POSITIVE`.
//
// **A zero cooldown, and a cooldown shorter than the shield, are both legal and deliberate.** They
// are not a re-activation loophole, because admission requires **both** the prior protection and
// the cooldown to have ended before a new pulse is accepted (`shared/ability_system.hpp`). A zero
// cooldown therefore says "re-activate as soon as the previous protection ends", not "re-activate
// while it is still running"; a cooldown shorter than the shield simply expires first and leaves
// the still-active protection to refuse the pulse on its own. The alternative -- requiring
// `cooldown >= shield` here -- would forbid an authored value whose meaning is well defined and
// would move an admission rule out of the system that owns it.
//
// **A perfect window longer than the shield it opens is refused** with
// `GAMEPLAY.ABILITY_PERFECT_WINDOW_EXCEEDS_SHIELD`. The perfect window is the opening *inside* the
// protection, sharing its activation tick (`../../simulation/components/shield_component.hpp`), so
// a perfect window that outlasted the shield would describe a parry still landing after the guard
// it belongs to had ended. The component would reject the same pair; refusing it here means an
// operator learns it at startup, naming the key, rather than on the first activation.
//
// **The four numbers are ADR 0008's initial tuning assumptions, not owner-selected balance
// values.** The ADR calls them "hypotheses to tune with real latency, not a reproduction of
// Smash's exact mechanics", and the plan's 2026-09-10 amendment says the same of the quarter
// impulse. They are authored in the configuration file precisely so that tuning them is an
// operator's edit and not a rebuild, and nothing in the tree may treat 0.4/0.08/0.9/0.6 as agreed
// balance.
//
// **Step 19 extends this same owner with charge.** Charge's gain, cooldown and safety envelope
// join the four keys below rather than founding a second `[charge]` section or a second validated
// value, exactly as the plan's Step 19 block describes. **There is no charge field here yet** --
// this step adds only the working shield timings, so a reader must not infer a reserved slot,
// a placeholder, or a partially implemented move from anything in this file.
// related: shared/duration_ticks.hpp -- the one seconds-to-ticks conversion these four keys use.
// related: shared/ability_system.hpp -- the system that reads these tick counts.
// related: ../game_mode_configuration.hpp -- the aggregate that carries this section to a mode.
// related: ../gameplay_validation_error.hpp -- the `GAMEPLAY.ABILITY_*` rejections.
class AbilityConfiguration final {
public:
  static constexpr double kDefaultShieldDurationSeconds = 0.4;
  static constexpr double kDefaultShieldPerfectWindowSeconds = 0.08;
  static constexpr double kDefaultShieldCooldownSeconds = 0.9;
  static constexpr double kDefaultParryStunDurationSeconds = 0.6;

  // The four authored keys, in the order `[abilities]` declares them. Throws
  // GameplayValidationError: `GAMEPLAY.DURATION_NOT_FINITE`, `GAMEPLAY.DURATION_NEGATIVE` or
  // `GAMEPLAY.DURATION_TICK_OVERFLOW` from the shared conversion, then
  // `GAMEPLAY.ABILITY_DURATION_NOT_POSITIVE` for a shield, perfect or parry-stun duration that
  // rounds to zero ticks, then `GAMEPLAY.ABILITY_PERFECT_WINDOW_EXCEEDS_SHIELD`. Every rejection
  // names `abilities.<key>` and carries the offending value.
  [[nodiscard]] static AbilityConfiguration create(double shield_duration_seconds,
                                                   double shield_perfect_window_seconds,
                                                   double shield_cooldown_seconds,
                                                   double parry_stun_duration_seconds);

  // The ADR's initial tuning: 0.4 s, 0.08 s, 0.9 s, 0.6 s, which are 160, 32, 360 and 240 ticks.
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

  friend bool operator==(const AbilityConfiguration&, const AbilityConfiguration&) = default;

private:
  AbilityConfiguration(std::uint64_t shield_duration_ticks,
                       std::uint64_t shield_perfect_window_ticks,
                       std::uint64_t shield_cooldown_ticks,
                       std::uint64_t parry_stun_duration_ticks) noexcept;

  std::uint64_t shield_duration_ticks_;
  std::uint64_t shield_perfect_window_ticks_;
  std::uint64_t shield_cooldown_ticks_;
  std::uint64_t parry_stun_duration_ticks_;
};

} // namespace blob_royale::gameplay

#endif
