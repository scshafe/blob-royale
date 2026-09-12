#include "shared/ability_configuration.hpp"

#include "gameplay_validation_error.hpp"
#include "shared/duration_ticks.hpp"
#include "simulation_limits.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace blob_royale::gameplay {
namespace {

[[nodiscard]] std::string context_of(const std::string_view key) {
  return "abilities." + std::string(key);
}

// A rounded effect duration of zero ticks is refused rather than stored. The message carries both
// the authored seconds and the tick count they rounded to, because the two together are the whole
// diagnosis: the operator wrote a real number and the 400 Hz conversion turned it into nothing.
void require_positive_ticks(const double seconds, const std::uint64_t ticks,
                            const std::string_view key) {
  if (ticks != 0) {
    return;
  }
  throw GameplayValidationError(GameplayValidationCode::kAbilityDurationNotPositive,
                                context_of(key),
                                std::to_string(seconds) + " s rounds to " + std::to_string(ticks) +
                                    " ticks, and an effect lasting no tick is absent, not short");
}

// The two `[abilities]` keys that are not durations. `duration_ticks` cannot hold them -- rounding
// a dimensionless multiple or a speed to ticks would be a unit error -- so this owner validates
// them itself, with the `GAMEPLAY.ABILITY_SCALAR_*` pair rather than the `GAMEPLAY.DURATION_*` one.
// Zero is excluded from both: a zero gain is not a weak charge and a zero envelope is not a strict
// one, they are an ability that can never activate, and a key that silently disables a shipped
// mechanic is worse than a refused startup.
void require_positive_finite_scalar(const double value, const std::string_view key) {
  if (!std::isfinite(value)) {
    throw GameplayValidationError(GameplayValidationCode::kAbilityScalarNotFinite, context_of(key),
                                  "every [abilities] scalar must be a finite number");
  }
  if (value <= 0.0) {
    throw GameplayValidationError(GameplayValidationCode::kAbilityScalarOutOfRange, context_of(key),
                                  std::to_string(value) + " must be greater than zero");
  }
}

} // namespace

AbilityConfiguration AbilityConfiguration::create(const double shield_duration_seconds,
                                                  const double shield_perfect_window_seconds,
                                                  const double shield_cooldown_seconds,
                                                  const double parry_stun_duration_seconds,
                                                  const double charge_cooldown_seconds,
                                                  const double charge_speed_fraction,
                                                  const double charge_safety_envelope_speed) {
  // Named locals preserve validation order across compilers; argument evaluation order would not.
  // A configuration whose shield duration and cooldown are both malformed must always report the
  // shield duration -- the first key `[abilities]` declares -- rather than whichever one the
  // toolchain happened to evaluate first (`race/race_configuration.cpp` states the same rule).
  const std::uint64_t shield_duration_ticks =
      duration_ticks(shield_duration_seconds, context_of("shield_duration_seconds"));
  const std::uint64_t shield_perfect_window_ticks =
      duration_ticks(shield_perfect_window_seconds, context_of("shield_perfect_window_seconds"));
  const std::uint64_t shield_cooldown_ticks =
      duration_ticks(shield_cooldown_seconds, context_of("shield_cooldown_seconds"));
  const std::uint64_t parry_stun_duration_ticks =
      duration_ticks(parry_stun_duration_seconds, context_of("parry_stun_duration_seconds"));
  const std::uint64_t charge_cooldown_ticks =
      duration_ticks(charge_cooldown_seconds, context_of("charge_cooldown_seconds"));
  // The post-conversion rules, in declared key order. The **shield** cooldown is absent from this
  // list on purpose: zero is a legal shield cooldown and so is one shorter than the shield, because
  // admission requires both the prior protection and the cooldown to have ended
  // (`shared/ability_system.hpp`) and neither value can shorten the wait the other imposes.
  require_positive_ticks(shield_duration_seconds, shield_duration_ticks, "shield_duration_seconds");
  require_positive_ticks(shield_perfect_window_seconds, shield_perfect_window_ticks,
                         "shield_perfect_window_seconds");
  require_positive_ticks(parry_stun_duration_seconds, parry_stun_duration_ticks,
                         "parry_stun_duration_seconds");
  // The **charge** cooldown is in the list, and the difference is the second gate. A shield with a
  // zero cooldown is still refused by its own live protection window; charge is one-shot and has no
  // protection window, so its cooldown is the only gate it has and a zero one admits an additive
  // burst on every tick -- four hundred a second at the canonical rate
  // (`docs/reviews/2026-09-12-charge-contract.md` § "Authored tuning and value ownership").
  require_positive_ticks(charge_cooldown_seconds, charge_cooldown_ticks, "charge_cooldown_seconds");
  // The two scalars, still in declared key order. The fraction has no upper bound of its own: what
  // bounds it is the cross-key rule below, which is a bound with a stated meaning rather than an
  // invented ceiling.
  require_positive_finite_scalar(charge_speed_fraction, "charge_speed_fraction");
  require_positive_finite_scalar(charge_safety_envelope_speed, "charge_safety_envelope_speed");
  // The envelope may not leave the `Vector2` component domain. A magnitude bound at or below that
  // domain keeps both components of an admitted burst representable, which is what lets the system
  // check one magnitude instead of two components (`shared/ability_system.cpp`); an envelope above
  // it would admit a velocity `Vector2::create` then refuses, turning a gameplay bound into a
  // thrown tick.
  if (charge_safety_envelope_speed > simulation::kMaximumPhysicalComponentMagnitude) {
    throw GameplayValidationError(
        GameplayValidationCode::kAbilityScalarOutOfRange,
        context_of("charge_safety_envelope_speed"),
        std::to_string(charge_safety_envelope_speed) + " wu/s exceeds the " +
            std::to_string(simulation::kMaximumPhysicalComponentMagnitude) +
            " world-unit component domain a velocity has to be representable in");
  }
  // The cross-key rules run last, so a section with two faults reports the faulty key before it
  // reports the pair. Each is named against the key an operator has to change rather than against
  // the bound it violates: the shield duration bounds the perfect window, and the envelope bounds
  // the fraction.
  if (shield_perfect_window_ticks > shield_duration_ticks) {
    throw GameplayValidationError(
        GameplayValidationCode::kAbilityPerfectWindowExceedsShield,
        context_of("shield_perfect_window_seconds"),
        std::to_string(shield_perfect_window_ticks) + " perfect ticks exceed the " +
            std::to_string(shield_duration_ticks) + " tick shield the window opens inside");
  }
  // A charge from rest must stay admissible whatever a room tunes its ceiling to, which is why the
  // bound is `kMaximumNormalTopSpeed` and not the ceiling this configuration happens to load
  // beside: the ceiling is live-tunable at runtime (`../../simulation/movement_tuning_state.hpp`)
  // and this section is validated once at startup. Without the rule an unbounded fraction is a
  // hole -- 1e6 against a 10,000 wu/s ceiling is a 1e10 wu/s burst whose components still sit
  // inside `Vector2`'s domain and which exhausts `kMaximumMotionEventCount` on its first tick, and
  // an exhaustion is fatal to the room. The product is computed in the authored units, not in
  // ticks, because neither factor is a duration.
  const double burst_from_rest_at_maximum_ceiling =
      charge_speed_fraction * simulation::kMaximumNormalTopSpeed;
  if (burst_from_rest_at_maximum_ceiling > charge_safety_envelope_speed) {
    throw GameplayValidationError(
        GameplayValidationCode::kAbilityChargeBurstExceedsSafetyEnvelope,
        context_of("charge_speed_fraction"),
        std::to_string(burst_from_rest_at_maximum_ceiling) +
            " wu/s from rest at the highest tunable ceiling exceeds the " +
            std::to_string(charge_safety_envelope_speed) +
            " wu/s safety envelope, so a charge from rest would be refused at that tuning");
  }
  return AbilityConfiguration(shield_duration_ticks, shield_perfect_window_ticks,
                              shield_cooldown_ticks, parry_stun_duration_ticks,
                              charge_cooldown_ticks, charge_speed_fraction,
                              charge_safety_envelope_speed);
}

AbilityConfiguration AbilityConfiguration::defaults() {
  return create(kDefaultShieldDurationSeconds, kDefaultShieldPerfectWindowSeconds,
                kDefaultShieldCooldownSeconds, kDefaultParryStunDurationSeconds,
                kDefaultChargeCooldownSeconds, kDefaultChargeSpeedFraction,
                kDefaultChargeSafetyEnvelopeSpeed);
}

AbilityConfiguration::AbilityConfiguration(const std::uint64_t shield_duration_ticks,
                                           const std::uint64_t shield_perfect_window_ticks,
                                           const std::uint64_t shield_cooldown_ticks,
                                           const std::uint64_t parry_stun_duration_ticks,
                                           const std::uint64_t charge_cooldown_ticks,
                                           const double charge_speed_fraction,
                                           const double charge_safety_envelope_speed) noexcept
    : shield_duration_ticks_(shield_duration_ticks),
      shield_perfect_window_ticks_(shield_perfect_window_ticks),
      shield_cooldown_ticks_(shield_cooldown_ticks),
      parry_stun_duration_ticks_(parry_stun_duration_ticks),
      charge_cooldown_ticks_(charge_cooldown_ticks), charge_speed_fraction_(charge_speed_fraction),
      charge_safety_envelope_speed_(charge_safety_envelope_speed) {}

} // namespace blob_royale::gameplay
