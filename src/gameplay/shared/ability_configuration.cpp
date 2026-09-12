#include "shared/ability_configuration.hpp"

#include "gameplay_validation_error.hpp"
#include "shared/duration_ticks.hpp"

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

} // namespace

AbilityConfiguration AbilityConfiguration::create(const double shield_duration_seconds,
                                                  const double shield_perfect_window_seconds,
                                                  const double shield_cooldown_seconds,
                                                  const double parry_stun_duration_seconds) {
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
  // The post-conversion rules, in declared key order. The cooldown is absent from this list on
  // purpose: zero is a legal cooldown and so is one shorter than the shield, because admission
  // requires both the prior protection and the cooldown to have ended (`shared/ability_system.hpp`)
  // and neither value can shorten the wait the other imposes.
  require_positive_ticks(shield_duration_seconds, shield_duration_ticks, "shield_duration_seconds");
  require_positive_ticks(shield_perfect_window_seconds, shield_perfect_window_ticks,
                         "shield_perfect_window_seconds");
  require_positive_ticks(parry_stun_duration_seconds, parry_stun_duration_ticks,
                         "parry_stun_duration_seconds");
  // The cross-key rule runs last, so a section with two faults reports the faulty key before it
  // reports the pair. It is named against the perfect window, which is the key an operator has to
  // change: the shield duration is the bound, not the offender.
  if (shield_perfect_window_ticks > shield_duration_ticks) {
    throw GameplayValidationError(
        GameplayValidationCode::kAbilityPerfectWindowExceedsShield,
        context_of("shield_perfect_window_seconds"),
        std::to_string(shield_perfect_window_ticks) + " perfect ticks exceed the " +
            std::to_string(shield_duration_ticks) + " tick shield the window opens inside");
  }
  return AbilityConfiguration(shield_duration_ticks, shield_perfect_window_ticks,
                              shield_cooldown_ticks, parry_stun_duration_ticks);
}

AbilityConfiguration AbilityConfiguration::defaults() {
  return create(kDefaultShieldDurationSeconds, kDefaultShieldPerfectWindowSeconds,
                kDefaultShieldCooldownSeconds, kDefaultParryStunDurationSeconds);
}

AbilityConfiguration::AbilityConfiguration(const std::uint64_t shield_duration_ticks,
                                           const std::uint64_t shield_perfect_window_ticks,
                                           const std::uint64_t shield_cooldown_ticks,
                                           const std::uint64_t parry_stun_duration_ticks) noexcept
    : shield_duration_ticks_(shield_duration_ticks),
      shield_perfect_window_ticks_(shield_perfect_window_ticks),
      shield_cooldown_ticks_(shield_cooldown_ticks),
      parry_stun_duration_ticks_(parry_stun_duration_ticks) {}

} // namespace blob_royale::gameplay
