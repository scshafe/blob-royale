#include "components/shield_component.hpp"

#include "simulation_validation_error.hpp"
#include "tick_sequence.hpp"
#include "tick_window.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <type_traits>

namespace simulation = blob_royale::simulation;

namespace {

// The ADR's initial tuning at 400 Hz, so the values these tests reason about are the values the
// mode will actually construct: 0.4 s of protection, an 0.08 s opening, a 0.9 s cooldown, and a
// 0.6 s parry stun.
constexpr std::uint64_t kActivation = 100;
constexpr std::uint64_t kShieldDuration = 160;
constexpr std::uint64_t kPerfectDuration = 32;
constexpr std::uint64_t kCooldownDuration = 360;
constexpr std::uint64_t kParryStunDuration = 240;
constexpr std::uint64_t kShieldExpiry = kActivation + kShieldDuration;
constexpr std::uint64_t kPerfectExpiry = kActivation + kPerfectDuration;
constexpr std::uint64_t kCooldownExpiry = kActivation + kCooldownDuration;
constexpr std::uint64_t kOneTick = 1;

[[nodiscard]] simulation::TickSequence tick(const std::uint64_t value) {
  return simulation::TickSequence::create(value);
}

[[nodiscard]] simulation::Shield activated() {
  return simulation::Shield::activate(tick(kActivation), kShieldDuration, kPerfectDuration,
                                      kCooldownDuration, kParryStunDuration);
}

} // namespace

TEST_CASE("A shield activation dates all three windows from one positive activation tick",
          "[unit][simulation][shield]") {
  const simulation::Shield shield = activated();

  CHECK(shield.activation_tick() == tick(kActivation));
  CHECK(shield.shield_window().activation_tick() == tick(kActivation));
  CHECK(shield.perfect_window().activation_tick() == tick(kActivation));
  CHECK(shield.cooldown_window().activation_tick() == tick(kActivation));
  CHECK(shield.shield_window().expiry_tick() == tick(kShieldExpiry));
  CHECK(shield.perfect_window().expiry_tick() == tick(kPerfectExpiry));
  CHECK(shield.cooldown_window().expiry_tick() == tick(kCooldownExpiry));
  CHECK(shield.parry_stun_duration_ticks() == kParryStunDuration);

  // A value struct with private construction: the only way to hold one is to have activated it,
  // so no caller can assemble an activation whose endpoints were never validated.
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<simulation::Shield>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<simulation::Shield>);
  CHECK(shield == activated());
}

TEST_CASE("Every shield window contains its activation and excludes its expiry",
          "[unit][simulation][shield]") {
  const simulation::Shield shield = activated();

  CHECK(shield.shield_window().contains(tick(kActivation)));
  CHECK(shield.shield_window().contains(tick(kShieldExpiry - kOneTick)));
  CHECK_FALSE(shield.shield_window().expired(tick(kShieldExpiry - kOneTick)));
  CHECK_FALSE(shield.shield_window().contains(tick(kShieldExpiry)));
  CHECK(shield.shield_window().expired(tick(kShieldExpiry)));

  CHECK(shield.perfect_window().contains(tick(kActivation)));
  CHECK(shield.perfect_window().contains(tick(kPerfectExpiry - kOneTick)));
  CHECK_FALSE(shield.perfect_window().expired(tick(kPerfectExpiry - kOneTick)));
  CHECK_FALSE(shield.perfect_window().contains(tick(kPerfectExpiry)));
  CHECK(shield.perfect_window().expired(tick(kPerfectExpiry)));

  CHECK(shield.cooldown_window().contains(tick(kActivation)));
  CHECK(shield.cooldown_window().contains(tick(kCooldownExpiry - kOneTick)));
  CHECK_FALSE(shield.cooldown_window().expired(tick(kCooldownExpiry - kOneTick)));
  CHECK_FALSE(shield.cooldown_window().contains(tick(kCooldownExpiry)));
  CHECK(shield.cooldown_window().expired(tick(kCooldownExpiry)));

  // The opening is a prefix of the protection, so the tick the opening ends on is still protected.
  CHECK(shield.shield_window().contains(tick(kPerfectExpiry)));
}

TEST_CASE("A shield activation refuses a zero activation tick and every zero effect duration",
          "[unit][simulation][shield][validation]") {
  struct RefusedCase final {
    std::uint64_t activation;
    std::uint64_t shield_duration;
    std::uint64_t perfect_duration;
    std::uint64_t parry_stun_duration;
    const char* context;
  };
  // Tick zero is the loaded initial state, and each zero duration is an effect that could never
  // happen. Each is refused on its own context so a log names the setting that was wrong.
  const std::array<RefusedCase, 4> cases{
      RefusedCase{0, kShieldDuration, kPerfectDuration, kParryStunDuration,
                  "shield.activation_tick"},
      RefusedCase{kActivation, 0, kPerfectDuration, kParryStunDuration,
                  "shield.shield_duration_ticks"},
      RefusedCase{kActivation, kShieldDuration, 0, kParryStunDuration,
                  "shield.perfect_duration_ticks"},
      RefusedCase{kActivation, kShieldDuration, kPerfectDuration, 0,
                  "shield.parry_stun_duration_ticks"}};

  for (const RefusedCase& refused : cases) {
    CAPTURE(refused.context);
    try {
      static_cast<void>(simulation::Shield::activate(
          tick(refused.activation), refused.shield_duration, refused.perfect_duration,
          kCooldownDuration, refused.parry_stun_duration));
      FAIL("an impossible shield activation was accepted");
    } catch (const simulation::SimulationValidationError& error) {
      CHECK(error.validation_code() ==
            simulation::SimulationValidationCode::kShieldActivationInvalid);
      CHECK(error.code() == "SIMULATION.SHIELD_ACTIVATION_INVALID");
      CHECK(error.context() == refused.context);
    }
  }
}

TEST_CASE("A shield activation refuses a perfect opening longer than the protection",
          "[unit][simulation][shield][validation]") {
  try {
    static_cast<void>(simulation::Shield::activate(tick(kActivation), kPerfectDuration,
                                                   kPerfectDuration + kOneTick, kCooldownDuration,
                                                   kParryStunDuration));
    FAIL("a perfect opening outliving its protection was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kShieldActivationInvalid);
    CHECK(error.context() == "shield.perfect_duration_ticks");
  }

  // Equal is not longer: an opening that lasts the whole protection is a legal, if extreme, tuning.
  const simulation::Shield whole_opening = simulation::Shield::activate(
      tick(kActivation), kPerfectDuration, kPerfectDuration, kCooldownDuration, kParryStunDuration);
  CHECK(whole_opening.perfect_window() == whole_opening.shield_window());
}

TEST_CASE("A shield activation refuses an endpoint past the exact tick domain, never saturating",
          "[unit][simulation][shield][validation]") {
  const auto refuses = [](const std::uint64_t activation, const std::uint64_t shield_duration,
                          const std::uint64_t cooldown_duration) {
    try {
      static_cast<void>(simulation::Shield::activate(tick(activation), shield_duration, kOneTick,
                                                     cooldown_duration, kParryStunDuration));
      return false;
    } catch (const simulation::SimulationValidationError& error) {
      return error.code() == "SIMULATION.TICK_WINDOW_EXPIRY_OVERFLOW";
    }
  };

  // The protection overflows first, and so does a cooldown that alone runs past the domain: every
  // window is built through the same checked constructor, so none of them saturates silently.
  CHECK(refuses(simulation::TickSequence::kMaximumValue, kOneTick, 0));
  CHECK(refuses(simulation::TickSequence::kMaximumValue - kOneTick, kOneTick,
                kOneTick + kOneTick + kOneTick));
  CHECK_FALSE(refuses(simulation::TickSequence::kMaximumValue - kOneTick, kOneTick, kOneTick));
}

TEST_CASE("A shield accepts a zero cooldown and a cooldown shorter than its protection",
          "[unit][simulation][shield]") {
  // Admission requires the prior protection *and* the prior cooldown to have ended, so neither of
  // these is a shield a player may re-raise early: the protection is the longer wait in both.
  const simulation::Shield without_cooldown = simulation::Shield::activate(
      tick(kActivation), kShieldDuration, kPerfectDuration, 0, kParryStunDuration);
  CHECK(without_cooldown.cooldown_window().activation_tick() ==
        without_cooldown.cooldown_window().expiry_tick());
  CHECK(without_cooldown.cooldown_window().expired(tick(kActivation)));
  CHECK(without_cooldown.shield_window().contains(tick(kActivation)));

  const simulation::Shield brief_cooldown = simulation::Shield::activate(
      tick(kActivation), kShieldDuration, kPerfectDuration, kPerfectDuration, kParryStunDuration);
  CHECK(brief_cooldown.cooldown_window().expiry_tick() == tick(kPerfectExpiry));
  CHECK(brief_cooldown.cooldown_window().expired(tick(kPerfectExpiry)));
  CHECK_FALSE(brief_cooldown.shield_window().expired(tick(kPerfectExpiry)));
}

TEST_CASE("Cancelling a shield mid-protection ends it at the cancellation tick and nothing else",
          "[unit][simulation][shield][cancellation]") {
  constexpr std::uint64_t kCancellation = kActivation + 100;
  const simulation::Shield shield = activated();
  const simulation::Shield cancelled = shield.canceled_at(tick(kCancellation));

  CHECK(cancelled.shield_window().expiry_tick() == tick(kCancellation));
  CHECK_FALSE(cancelled.shield_window().contains(tick(kCancellation)));
  CHECK(cancelled.shield_window().contains(tick(kCancellation - kOneTick)));
  // The cancellation is what makes a cut-short shield still cost something, so nothing about the
  // wait for the next one moves.
  CHECK(cancelled.activation_tick() == shield.activation_tick());
  CHECK(cancelled.cooldown_window() == shield.cooldown_window());
  CHECK(cancelled.parry_stun_duration_ticks() == shield.parry_stun_duration_ticks());
}

TEST_CASE("Cancelling a shield after its opening elapsed preserves the opening exactly",
          "[unit][simulation][shield][cancellation]") {
  constexpr std::uint64_t kCancellation = kPerfectExpiry + kOneTick;
  const simulation::Shield shield = activated();
  const simulation::Shield cancelled = shield.canceled_at(tick(kCancellation));

  // There is nothing still active to end, so the elapsed opening is history and stays verbatim.
  CHECK(cancelled.perfect_window() == shield.perfect_window());
  CHECK(cancelled.shield_window().expiry_tick() == tick(kCancellation));
  CHECK(cancelled.shield_window() != shield.shield_window());
}

TEST_CASE("Cancelling a shield inside its opening ends the opening with the protection",
          "[unit][simulation][shield][cancellation]") {
  constexpr std::uint64_t kCancellation = kActivation + 10;
  const simulation::Shield cancelled = activated().canceled_at(tick(kCancellation));

  // The opening is part of the protection, not a second defence that outlives it: a shield ended
  // inside its opening must not still answer "perfect" to a contact response.
  CHECK(cancelled.perfect_window().expiry_tick() == tick(kCancellation));
  CHECK(cancelled.shield_window().expiry_tick() == tick(kCancellation));
  CHECK_FALSE(cancelled.perfect_window().contains(tick(kCancellation)));
  CHECK(cancelled.perfect_window().contains(tick(kCancellation - kOneTick)));
  // The already-elapsed part of the opening is untouched, which is what "preserves the elapsed
  // history" means: the ticks before the cancellation still parried.
  CHECK(cancelled.perfect_window().activation_tick() == tick(kActivation));
  CHECK(cancelled.cooldown_window() == activated().cooldown_window());
}

TEST_CASE("Cancelling a shield on its activation tick yields empty protection and keeps the wait",
          "[unit][simulation][shield][cancellation]") {
  const simulation::Shield shield = activated();
  const simulation::Shield cancelled = shield.canceled_at(tick(kActivation));

  // A pulse stunned on the tick it landed. Empty protection is a real value, not a missing one:
  // the activation still dates it and the cooldown still has to run out.
  CHECK(cancelled.activation_tick() == tick(kActivation));
  CHECK(cancelled.shield_window().activation_tick() == cancelled.shield_window().expiry_tick());
  CHECK(cancelled.perfect_window().activation_tick() == cancelled.perfect_window().expiry_tick());
  CHECK_FALSE(cancelled.shield_window().contains(tick(kActivation)));
  CHECK(cancelled.shield_window().expired(tick(kActivation)));
  CHECK(cancelled.cooldown_window() == shield.cooldown_window());
  CHECK_FALSE(cancelled.cooldown_window().expired(tick(kActivation)));
}

TEST_CASE("Cancelling a shield twice is an exact no-op the second time",
          "[unit][simulation][shield][cancellation]") {
  constexpr std::uint64_t kCancellation = kActivation + 100;
  const simulation::Shield cancelled = activated().canceled_at(tick(kCancellation));

  // A stun lasts many ticks and the status system cancels on every one of them, so the second and
  // hundredth cancellation must write back a value equal to the first -- otherwise a held stun
  // would keep rewriting the world and every tick of it would look like a change.
  CHECK(cancelled.canceled_at(tick(kCancellation)) == cancelled);
  CHECK(cancelled.canceled_at(tick(kCancellation + kOneTick)) == cancelled);
  CHECK(cancelled.canceled_at(tick(kShieldExpiry)) == cancelled);
  CHECK(activated().canceled_at(tick(kActivation)).canceled_at(tick(kActivation)) ==
        activated().canceled_at(tick(kActivation)));
}

TEST_CASE("Cancelling a shield after its protection expired changes nothing",
          "[unit][simulation][shield][cancellation]") {
  const simulation::Shield shield = activated();

  // The protection is already over; only the cooldown is still running, and cancellation never
  // touches a cooldown. Erasing the value is the ability system's job, not this one's.
  CHECK(shield.canceled_at(tick(kShieldExpiry)) == shield);
  CHECK(shield.canceled_at(tick(kCooldownExpiry)) == shield);
  CHECK(shield.canceled_at(tick(kCooldownExpiry)).cooldown_window() == shield.cooldown_window());
}

TEST_CASE("Cancelling a shield before its activation is a named chronology failure",
          "[unit][simulation][shield][cancellation][validation]") {
  const simulation::Shield shield = activated();

  try {
    static_cast<void>(shield.canceled_at(tick(kActivation - kOneTick)));
    FAIL("a cancellation before the activation was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kShieldCancellationBeforeActivation);
    CHECK(error.code() == "SIMULATION.SHIELD_CANCELLATION_BEFORE_ACTIVATION");
    CHECK(error.context() == "shield.cancellation_tick");
  }
  try {
    static_cast<void>(shield.canceled_at(simulation::TickSequence::zero()));
    FAIL("a cancellation at the initial tick was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kShieldCancellationBeforeActivation);
  }
}

TEST_CASE("Two shields compare equal only when every stored value agrees",
          "[unit][simulation][shield]") {
  const simulation::Shield shield = activated();

  CHECK(shield == activated());
  CHECK(shield != simulation::Shield::activate(tick(kActivation + kOneTick), kShieldDuration,
                                               kPerfectDuration, kCooldownDuration,
                                               kParryStunDuration));
  CHECK(shield != simulation::Shield::activate(tick(kActivation), kShieldDuration, kPerfectDuration,
                                               kCooldownDuration, kParryStunDuration + kOneTick));
  CHECK(shield != shield.canceled_at(tick(kActivation)));
}
