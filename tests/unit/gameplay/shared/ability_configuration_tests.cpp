#include "shared/ability_configuration.hpp"

#include "gameplay_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <string>
#include <string_view>

namespace gameplay = blob_royale::gameplay;

namespace {

// The four authored `[abilities]` keys in declared order. `AbilityConfiguration::create` takes four
// positional doubles rather than a section struct, so this is the test's own carrier: it exists so
// a table-driven case can name one key, vary only that key, and leave the other three at the
// authored defaults.
struct Authored final {
  double shield_duration_seconds = gameplay::AbilityConfiguration::kDefaultShieldDurationSeconds;
  double shield_perfect_window_seconds =
      gameplay::AbilityConfiguration::kDefaultShieldPerfectWindowSeconds;
  double shield_cooldown_seconds = gameplay::AbilityConfiguration::kDefaultShieldCooldownSeconds;
  double parry_stun_duration_seconds =
      gameplay::AbilityConfiguration::kDefaultParryStunDurationSeconds;
};

[[nodiscard]] gameplay::AbilityConfiguration created(const Authored& authored) {
  return gameplay::AbilityConfiguration::create(
      authored.shield_duration_seconds, authored.shield_perfect_window_seconds,
      authored.shield_cooldown_seconds, authored.parry_stun_duration_seconds);
}

struct Rejection final {
  gameplay::GameplayValidationCode code;
  std::string name;
  std::string context;
};

[[nodiscard]] Rejection rejection_of(const Authored& authored) {
  try {
    static_cast<void>(created(authored));
  } catch (const gameplay::GameplayValidationError& error) {
    return Rejection{error.validation_code(), std::string(error.code()), error.context()};
  }
  FAIL("the [abilities] section was accepted");
  return Rejection{gameplay::GameplayValidationCode::kGameModeNameUnknown, {}, {}};
}

struct Field final {
  double Authored::*member;
  std::string_view key;
};

constexpr std::array<Field, 4> kEveryDuration = {{
    {&Authored::shield_duration_seconds, "shield_duration_seconds"},
    {&Authored::shield_perfect_window_seconds, "shield_perfect_window_seconds"},
    {&Authored::shield_cooldown_seconds, "shield_cooldown_seconds"},
    {&Authored::parry_stun_duration_seconds, "parry_stun_duration_seconds"},
}};

// The three durations that name an effect. The cooldown is deliberately absent: it is the one key
// whose rounded value may legally be zero.
constexpr std::array<Field, 3> kEffectDurations = {{
    {&Authored::shield_duration_seconds, "shield_duration_seconds"},
    {&Authored::shield_perfect_window_seconds, "shield_perfect_window_seconds"},
    {&Authored::parry_stun_duration_seconds, "parry_stun_duration_seconds"},
}};

[[nodiscard]] std::string context_of(const std::string_view key) {
  return "abilities." + std::string(key);
}

} // namespace

TEST_CASE("the proposed [abilities] section converts to the accepted tick counts",
          "[unit][gameplay][abilities][configuration]") {
  // These four counts are also written out in `docs/architecture/0008-dynamic-arenas-and-combat.md`
  // § "Timed shield and perfect opening". They are the ADR's initial tuning assumptions, not agreed
  // balance, and this case exists so the two statements of them cannot drift apart.
  const gameplay::AbilityConfiguration configuration = gameplay::AbilityConfiguration::defaults();
  CHECK(configuration.shield_duration_ticks() == 160);
  CHECK(configuration.shield_perfect_window_ticks() == 32);
  CHECK(configuration.shield_cooldown_ticks() == 360);
  CHECK(configuration.parry_stun_duration_ticks() == 240);
  CHECK(configuration == created(Authored{}));
  CHECK(configuration == gameplay::AbilityConfiguration::create(0.4, 0.08, 0.9, 0.6));
}

TEST_CASE("every [abilities] duration uses the shared rejection and names its own key",
          "[unit][gameplay][abilities][configuration][validation]") {
  // The codes are the owner-neutral `GAMEPLAY.DURATION_*` family, because the rule belongs to the
  // shared conversion and not to abilities; the context is what tells an operator which section and
  // which key to open (`shared/duration_ticks.hpp`).
  constexpr std::array<double, 3> nonfinite = {std::numeric_limits<double>::quiet_NaN(),
                                               std::numeric_limits<double>::infinity(),
                                               -std::numeric_limits<double>::infinity()};
  for (const Field& field : kEveryDuration) {
    CAPTURE(field.key);
    Authored authored;
    authored.*field.member = -1.0;
    CHECK(rejection_of(authored).code == gameplay::GameplayValidationCode::kDurationNegative);
    CHECK(rejection_of(authored).context == context_of(field.key));
    for (const double invalid : nonfinite) {
      CAPTURE(invalid);
      authored.*field.member = invalid;
      CHECK(rejection_of(authored).code == gameplay::GameplayValidationCode::kDurationNotFinite);
      CHECK(rejection_of(authored).context == context_of(field.key));
    }
    authored.*field.member = std::numeric_limits<double>::max();
    CHECK(rejection_of(authored).code == gameplay::GameplayValidationCode::kDurationTickOverflow);
    CHECK(rejection_of(authored).context == context_of(field.key));
  }
}

TEST_CASE("a shield, perfect or parry-stun duration that rounds to zero ticks is refused",
          "[unit][gameplay][abilities][configuration][validation]") {
  // 0.001 s is 0.4 ticks, which rounds to none at all. A zero-length half-open window contains no
  // tick, so storing one would publish a guard that could never guard and a parry that could never
  // stun; the shared conversion accepts zero, so this positivity rule is this owner's own.
  for (const Field& field : kEffectDurations) {
    CAPTURE(field.key);
    Authored authored;
    authored.*field.member = 0.001;
    const Rejection rejection = rejection_of(authored);
    CHECK(rejection.code == gameplay::GameplayValidationCode::kAbilityDurationNotPositive);
    CHECK(rejection.name == "GAMEPLAY.ABILITY_DURATION_NOT_POSITIVE");
    CHECK(rejection.context == context_of(field.key));
    authored.*field.member = 0.0;
    CHECK(rejection_of(authored).code ==
          gameplay::GameplayValidationCode::kAbilityDurationNotPositive);
  }
}

TEST_CASE("a perfect window longer than the shield it opens is refused at its own key",
          "[unit][gameplay][abilities][configuration][validation]") {
  Authored authored;
  authored.shield_perfect_window_seconds = 0.5;
  const Rejection rejection = rejection_of(authored);
  CHECK(rejection.code == gameplay::GameplayValidationCode::kAbilityPerfectWindowExceedsShield);
  CHECK(rejection.name == "GAMEPLAY.ABILITY_PERFECT_WINDOW_EXCEEDS_SHIELD");
  // Named against the perfect window rather than the shield: the shield duration is the bound the
  // pair violates, and the window is the key an operator has to change.
  CHECK(rejection.context == context_of("shield_perfect_window_seconds"));
  // Equal endpoints are the accepted boundary. A perfect opening that lasts the whole guard is a
  // legal, if extreme, tuning; only one that outlasts the guard describes a parry landing after the
  // guard it belongs to has ended.
  authored.shield_perfect_window_seconds = authored.shield_duration_seconds;
  const gameplay::AbilityConfiguration configuration = created(authored);
  CHECK(configuration.shield_perfect_window_ticks() == configuration.shield_duration_ticks());
}

TEST_CASE("a zero cooldown and a cooldown shorter than the shield are both accepted",
          "[unit][gameplay][abilities][configuration]") {
  // Neither is a re-activation loophole. Admission requires **both** the prior protection and the
  // cooldown to have ended (`shared/ability_system.hpp`), so a zero cooldown says "again as soon as
  // this guard ends" and a short one simply expires while the guard it started with is still
  // refusing the next pulse on its own.
  Authored authored;
  authored.shield_cooldown_seconds = 0.0;
  const gameplay::AbilityConfiguration immediate = created(authored);
  CHECK(immediate.shield_cooldown_ticks() == 0);
  CHECK(immediate.shield_duration_ticks() == 160);
  authored.shield_cooldown_seconds = 0.1;
  const gameplay::AbilityConfiguration brief = created(authored);
  CHECK(brief.shield_cooldown_ticks() == 40);
  CHECK(brief.shield_cooldown_ticks() < brief.shield_duration_ticks());
}

TEST_CASE("every [abilities] duration rounds to the nearest tick at 400 Hz",
          "[unit][gameplay][abilities][configuration]") {
  // The shared conversion is `round(seconds * 400)`, ties away from zero. An *exact* tie is
  // unreachable from an authored decimal -- 400 is not a power of two, so `(k + 0.5) / 400` is
  // never a binary64 value -- so the boundary is proven from either side of it, exactly as
  // `shared/duration_ticks_tests.cpp` proves it for the conversion itself.
  Authored authored;
  authored.shield_cooldown_seconds = 0.001;
  CHECK(created(authored).shield_cooldown_ticks() == 0);
  authored.shield_cooldown_seconds = 0.002;
  CHECK(created(authored).shield_cooldown_ticks() == 1);
  authored.shield_duration_seconds = 0.0075;
  authored.shield_perfect_window_seconds = 0.0075;
  authored.parry_stun_duration_seconds = 0.03;
  const gameplay::AbilityConfiguration configuration = created(authored);
  CHECK(configuration.shield_duration_ticks() == 3);
  CHECK(configuration.shield_perfect_window_ticks() == 3);
  CHECK(configuration.parry_stun_duration_ticks() == 12);
}

TEST_CASE("[abilities] validation reports the first invalid key in the declared order",
          "[unit][gameplay][abilities][configuration][validation]") {
  // The keys are parsed into named locals in declared order rather than inline as constructor
  // arguments, because C++ leaves argument evaluation order unspecified and a section with two bad
  // keys would otherwise report a compiler-dependent first error.
  Authored authored;
  authored.shield_duration_seconds = -1.0;
  authored.shield_cooldown_seconds = -1.0;
  CHECK(rejection_of(authored).context == context_of("shield_duration_seconds"));
  // The cross-key rule runs after every per-key rule, so a shield that rounded away is reported as
  // the missing shield it is rather than as the perfect window that now exceeds it.
  authored.shield_duration_seconds = 0.001;
  authored.shield_cooldown_seconds = gameplay::AbilityConfiguration::kDefaultShieldCooldownSeconds;
  CHECK(rejection_of(authored).code ==
        gameplay::GameplayValidationCode::kAbilityDurationNotPositive);
  CHECK(rejection_of(authored).context == context_of("shield_duration_seconds"));
}
