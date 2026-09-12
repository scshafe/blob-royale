#include "shared/ability_configuration.hpp"

#include "gameplay_validation_error.hpp"
#include "simulation_limits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <string>
#include <string_view>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;

namespace {

// The seven authored `[abilities]` keys in declared order. `AbilityConfiguration::create` takes
// seven positional doubles rather than a section struct, so this is the test's own carrier: it
// exists so a table-driven case can name one key, vary only that key, and leave the other six at
// the authored defaults.
struct Authored final {
  double shield_duration_seconds = gameplay::AbilityConfiguration::kDefaultShieldDurationSeconds;
  double shield_perfect_window_seconds =
      gameplay::AbilityConfiguration::kDefaultShieldPerfectWindowSeconds;
  double shield_cooldown_seconds = gameplay::AbilityConfiguration::kDefaultShieldCooldownSeconds;
  double parry_stun_duration_seconds =
      gameplay::AbilityConfiguration::kDefaultParryStunDurationSeconds;
  double charge_cooldown_seconds = gameplay::AbilityConfiguration::kDefaultChargeCooldownSeconds;
  double charge_speed_fraction = gameplay::AbilityConfiguration::kDefaultChargeSpeedFraction;
  double charge_safety_envelope_speed =
      gameplay::AbilityConfiguration::kDefaultChargeSafetyEnvelopeSpeed;
};

[[nodiscard]] gameplay::AbilityConfiguration created(const Authored& authored) {
  return gameplay::AbilityConfiguration::create(
      authored.shield_duration_seconds, authored.shield_perfect_window_seconds,
      authored.shield_cooldown_seconds, authored.parry_stun_duration_seconds,
      authored.charge_cooldown_seconds, authored.charge_speed_fraction,
      authored.charge_safety_envelope_speed);
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

constexpr std::array<Field, 5> kEveryDuration = {{
    {&Authored::shield_duration_seconds, "shield_duration_seconds"},
    {&Authored::shield_perfect_window_seconds, "shield_perfect_window_seconds"},
    {&Authored::shield_cooldown_seconds, "shield_cooldown_seconds"},
    {&Authored::parry_stun_duration_seconds, "parry_stun_duration_seconds"},
    {&Authored::charge_cooldown_seconds, "charge_cooldown_seconds"},
}};

// The four durations that must survive rounding. The three effect durations name an effect that a
// zero-length window could never deliver; the charge cooldown is here for the different reason the
// case below states. The *shield* cooldown is deliberately absent: it is the one key whose rounded
// value may legally be zero.
constexpr std::array<Field, 4> kPositiveDurations = {{
    {&Authored::shield_duration_seconds, "shield_duration_seconds"},
    {&Authored::shield_perfect_window_seconds, "shield_perfect_window_seconds"},
    {&Authored::parry_stun_duration_seconds, "parry_stun_duration_seconds"},
    {&Authored::charge_cooldown_seconds, "charge_cooldown_seconds"},
}};

// The two keys that are not durations at all, so no `duration_ticks` rule reaches them.
constexpr std::array<Field, 2> kEveryScalar = {{
    {&Authored::charge_speed_fraction, "charge_speed_fraction"},
    {&Authored::charge_safety_envelope_speed, "charge_safety_envelope_speed"},
}};

[[nodiscard]] std::string context_of(const std::string_view key) {
  return "abilities." + std::string(key);
}

} // namespace

TEST_CASE("the proposed [abilities] section converts to the accepted tick counts and scalars",
          "[unit][gameplay][abilities][configuration]") {
  // These counts are also written out in `docs/architecture/0008-dynamic-arenas-and-combat.md`
  // §§ "Timed shield and perfect opening" and "Charge". They are the ADR's initial tuning
  // assumptions and one engineering guard, not agreed balance, and this case exists so the two
  // statements of them cannot drift apart.
  const gameplay::AbilityConfiguration configuration = gameplay::AbilityConfiguration::defaults();
  CHECK(configuration.shield_duration_ticks() == 160);
  CHECK(configuration.shield_perfect_window_ticks() == 32);
  CHECK(configuration.shield_cooldown_ticks() == 360);
  CHECK(configuration.parry_stun_duration_ticks() == 240);
  CHECK(configuration.charge_cooldown_ticks() == 480);
  // The two scalars are stored exactly as authored rather than rounded to ticks: neither is a
  // duration, so a tick count for either would be a unit error. The fraction is a dimensionless
  // multiple of the current ceiling and the envelope is a speed in world units per second.
  CHECK(configuration.charge_speed_fraction() == 0.75);
  CHECK(configuration.charge_safety_envelope_speed() == 20'000.0);
  CHECK(configuration == created(Authored{}));
  CHECK(configuration ==
        gameplay::AbilityConfiguration::create(0.4, 0.08, 0.9, 0.6, 1.2, 0.75, 20'000.0));
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

TEST_CASE("a shield, perfect, parry-stun or charge cooldown rounding to zero ticks is refused",
          "[unit][gameplay][abilities][configuration][validation]") {
  // 0.001 s is 0.4 ticks, which rounds to none at all. A zero-length half-open window contains no
  // tick, so storing one would publish a guard that could never guard, a parry that could never
  // stun, or a charge cooldown that could never make anyone wait; the shared conversion accepts
  // zero, so this positivity rule is this owner's own.
  for (const Field& field : kPositiveDurations) {
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

TEST_CASE("a zero shield cooldown and one shorter than the shield are both accepted",
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

TEST_CASE("the shield's zero-cooldown exemption does not transfer to the charge cooldown",
          "[unit][gameplay][abilities][configuration][validation][charge]") {
  // The exemption above is justified entirely by the shield's *second* gate: its own protection
  // window is still standing when the cooldown is short or zero, so something is always left to
  // refuse the next pulse. Charge is one-shot -- no protection window, no active window -- so its
  // cooldown is the only gate it has, and one that rounds to zero would admit an additive burst on
  // every tick, four hundred a second at the canonical rate.
  Authored authored;
  authored.shield_cooldown_seconds = 0.0;
  authored.charge_cooldown_seconds = 0.0;
  const Rejection rejection = rejection_of(authored);
  CHECK(rejection.code == gameplay::GameplayValidationCode::kAbilityDurationNotPositive);
  CHECK(rejection.context == context_of("charge_cooldown_seconds"));
  // The same section with the shield cooldown at zero and a charge cooldown of one whole tick is
  // accepted, so the refusal is the charge cooldown alone and not the pair.
  authored.charge_cooldown_seconds = 0.0025;
  const gameplay::AbilityConfiguration configuration = created(authored);
  CHECK(configuration.shield_cooldown_ticks() == 0);
  CHECK(configuration.charge_cooldown_ticks() == 1);
}

TEST_CASE("both [abilities] charge scalars must be finite and strictly positive",
          "[unit][gameplay][abilities][configuration][validation][charge]") {
  // Neither key is a duration, so no `GAMEPLAY.DURATION_*` rule reaches it and this owner declares
  // its own scalar pair. Zero is refused for both: a zero gain is not a weak charge and a zero
  // envelope is not a strict one, they are an ability that can never activate at all.
  constexpr std::array<double, 3> nonfinite = {std::numeric_limits<double>::quiet_NaN(),
                                               std::numeric_limits<double>::infinity(),
                                               -std::numeric_limits<double>::infinity()};
  for (const Field& field : kEveryScalar) {
    CAPTURE(field.key);
    Authored authored;
    for (const double invalid : nonfinite) {
      CAPTURE(invalid);
      authored.*field.member = invalid;
      const Rejection rejection = rejection_of(authored);
      CHECK(rejection.code == gameplay::GameplayValidationCode::kAbilityScalarNotFinite);
      CHECK(rejection.name == "GAMEPLAY.ABILITY_SCALAR_NOT_FINITE");
      CHECK(rejection.context == context_of(field.key));
    }
    for (const double invalid : {0.0, -1.0}) {
      CAPTURE(invalid);
      authored.*field.member = invalid;
      const Rejection rejection = rejection_of(authored);
      CHECK(rejection.code == gameplay::GameplayValidationCode::kAbilityScalarOutOfRange);
      CHECK(rejection.name == "GAMEPLAY.ABILITY_SCALAR_OUT_OF_RANGE");
      CHECK(rejection.context == context_of(field.key));
    }
  }
}

TEST_CASE("a safety envelope outside the physical component domain is refused",
          "[unit][gameplay][abilities][configuration][validation][charge]") {
  // The envelope is the one bound the system checks before constructing a `Vector2`, so an envelope
  // above the component domain would admit a velocity `Vector2::create` then throws on -- turning a
  // gameplay bound into a thrown tick that stops the runtime worker.
  Authored authored;
  authored.charge_safety_envelope_speed = simulation::kMaximumPhysicalComponentMagnitude * 2.0;
  const Rejection rejection = rejection_of(authored);
  CHECK(rejection.code == gameplay::GameplayValidationCode::kAbilityScalarOutOfRange);
  CHECK(rejection.context == context_of("charge_safety_envelope_speed"));
  // The domain itself is the accepted boundary.
  authored.charge_safety_envelope_speed = simulation::kMaximumPhysicalComponentMagnitude;
  CHECK(created(authored).charge_safety_envelope_speed() ==
        simulation::kMaximumPhysicalComponentMagnitude);
}

TEST_CASE("a gain that could not charge from rest at the highest tunable ceiling is refused",
          "[unit][gameplay][abilities][configuration][validation][charge]") {
  // The one cross-key rule, and the one that bounds the fraction: a charge from rest must stay
  // admissible whatever a room tunes its ceiling to. It is checked against `kMaximumNormalTopSpeed`
  // rather than against the ceiling loaded beside it, because the ceiling is live-tunable at
  // runtime while this section is validated once at startup.
  Authored authored;
  authored.charge_speed_fraction =
      (authored.charge_safety_envelope_speed / simulation::kMaximumNormalTopSpeed) * 1.5;
  const Rejection rejection = rejection_of(authored);
  CHECK(rejection.code ==
        gameplay::GameplayValidationCode::kAbilityChargeBurstExceedsSafetyEnvelope);
  CHECK(rejection.name == "GAMEPLAY.ABILITY_CHARGE_BURST_EXCEEDS_SAFETY_ENVELOPE");
  // Named against the fraction rather than the envelope, exactly as the perfect-window rule is
  // named against the window: the envelope is the bound the pair violates and the fraction is the
  // key the rule exists to bound.
  CHECK(rejection.context == context_of("charge_speed_fraction"));

  // Equality is the accepted boundary: a burst that lands exactly on the envelope from rest is
  // admissible, so the rule refuses only a gain that could not be used at all.
  authored.charge_speed_fraction =
      authored.charge_safety_envelope_speed / simulation::kMaximumNormalTopSpeed;
  CHECK(created(authored).charge_speed_fraction() == authored.charge_speed_fraction);

  // The same rule seen from the other key. A fraction of 0.75 needs an envelope of at least 7,500
  // wu/s, which is far below the authored 20,000, so lowering the envelope is what breaks the pair.
  Authored lowered;
  lowered.charge_safety_envelope_speed =
      (lowered.charge_speed_fraction * simulation::kMaximumNormalTopSpeed) - 1.0;
  CHECK(rejection_of(lowered).code ==
        gameplay::GameplayValidationCode::kAbilityChargeBurstExceedsSafetyEnvelope);
  lowered.charge_safety_envelope_speed =
      lowered.charge_speed_fraction * simulation::kMaximumNormalTopSpeed;
  CHECK(created(lowered).charge_safety_envelope_speed() == 7'500.0);
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
  authored.charge_cooldown_seconds = 0.0075;
  const gameplay::AbilityConfiguration configuration = created(authored);
  CHECK(configuration.shield_duration_ticks() == 3);
  CHECK(configuration.shield_perfect_window_ticks() == 3);
  CHECK(configuration.parry_stun_duration_ticks() == 12);
  CHECK(configuration.charge_cooldown_ticks() == 3);
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
  // The cross-key rules run after every per-key rule, so a shield that rounded away is reported as
  // the missing shield it is rather than as the perfect window that now exceeds it.
  authored.shield_duration_seconds = 0.001;
  authored.shield_cooldown_seconds = gameplay::AbilityConfiguration::kDefaultShieldCooldownSeconds;
  CHECK(rejection_of(authored).code ==
        gameplay::GameplayValidationCode::kAbilityDurationNotPositive);
  CHECK(rejection_of(authored).context == context_of("shield_duration_seconds"));

  // The charge keys extend the same order rather than opening a second one. A malformed shield key
  // outranks a malformed charge key, whichever kind of fault each one is.
  Authored mixed;
  mixed.shield_perfect_window_seconds = 0.001;
  mixed.charge_cooldown_seconds = 0.001;
  mixed.charge_speed_fraction = -1.0;
  CHECK(rejection_of(mixed).context == context_of("shield_perfect_window_seconds"));
  mixed.shield_perfect_window_seconds =
      gameplay::AbilityConfiguration::kDefaultShieldPerfectWindowSeconds;
  CHECK(rejection_of(mixed).context == context_of("charge_cooldown_seconds"));
  mixed.charge_cooldown_seconds = gameplay::AbilityConfiguration::kDefaultChargeCooldownSeconds;
  CHECK(rejection_of(mixed).context == context_of("charge_speed_fraction"));
  // And the charge cross-key rule runs after both charge scalars have been judged on their own, so
  // a section with a negative envelope reports the envelope rather than the pair it also breaks.
  mixed.charge_speed_fraction = gameplay::AbilityConfiguration::kDefaultChargeSpeedFraction;
  mixed.charge_safety_envelope_speed = -1.0;
  CHECK(rejection_of(mixed).code == gameplay::GameplayValidationCode::kAbilityScalarOutOfRange);
  CHECK(rejection_of(mixed).context == context_of("charge_safety_envelope_speed"));
}
