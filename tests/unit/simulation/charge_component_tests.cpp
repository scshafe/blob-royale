#include "components/charge_component.hpp"

#include "components/shield_component.hpp"
#include "simulation_validation_error.hpp"
#include "tick_sequence.hpp"
#include "tick_window.hpp"

#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace simulation = blob_royale::simulation;

namespace {

// The ADR's initial tuning at 400 Hz, so the values these tests reason about are the values the
// mode will actually construct: a 1.2 s wait between two bursts.
constexpr std::uint64_t kActivation = 100;
constexpr std::uint64_t kCooldownDuration = 480;
constexpr std::uint64_t kCooldownExpiry = kActivation + kCooldownDuration;
constexpr std::uint64_t kOneTick = 1;

// The shield's own tuning, needed only where these tests contrast the two cooldown rules.
constexpr std::uint64_t kShieldDuration = 160;
constexpr std::uint64_t kPerfectDuration = 32;
constexpr std::uint64_t kParryStunDuration = 240;
constexpr std::uint64_t kNoCooldown = 0;

[[nodiscard]] simulation::TickSequence tick(const std::uint64_t value) {
  return simulation::TickSequence::create(value);
}

[[nodiscard]] simulation::Charge activated() {
  return simulation::Charge::activate(tick(kActivation), kCooldownDuration);
}

// Both abilities expose cancellation of their effects while preserving incurred cooldowns.
template <typename Ability>
concept CancelableAbility = requires(const Ability& ability, const simulation::TickSequence when) {
  { ability.canceled_at(when) } -> std::same_as<Ability>;
};

} // namespace

TEST_CASE("A charge activation dates its one cooldown window from one positive activation tick",
          "[unit][simulation][charge]") {
  const simulation::Charge charge = activated();

  CHECK(charge.activation_tick() == tick(kActivation));
  CHECK(charge.cooldown_window().activation_tick() == tick(kActivation));
  CHECK(charge.cooldown_window().expiry_tick() == tick(kCooldownExpiry));

  // A value struct with private construction: the only way to hold one is to have activated it,
  // so no caller can assemble a cooldown whose endpoints were never validated.
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<simulation::Charge>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<simulation::Charge>);
  CHECK(charge == activated());
}

TEST_CASE("A charge cooldown contains its activation and excludes its expiry",
          "[unit][simulation][charge]") {
  const simulation::Charge charge = activated();

  // Half-open `[activation, expiry)`, so the activation tick is inside the wait -- a body cannot
  // charge twice on the tick it charged -- and the expiry tick is the first tick it may charge
  // again on, rather than the last tick of the wait.
  CHECK(charge.cooldown_window().contains(tick(kActivation)));
  CHECK_FALSE(charge.cooldown_window().expired(tick(kActivation)));
  CHECK(charge.cooldown_window().contains(tick(kCooldownExpiry - kOneTick)));
  CHECK_FALSE(charge.cooldown_window().expired(tick(kCooldownExpiry - kOneTick)));
  CHECK_FALSE(charge.cooldown_window().contains(tick(kCooldownExpiry)));
  CHECK(charge.cooldown_window().expired(tick(kCooldownExpiry)));
  CHECK(charge.cooldown_window().expired(tick(kCooldownExpiry + kOneTick)));
}

TEST_CASE("A charge activation refuses a zero activation tick",
          "[unit][simulation][charge][validation]") {
  // Tick zero is the loaded initial state and no pulse is admitted on it, so an activation dated
  // zero is a caller that lost the tick rather than a charge fired at the start of the match.
  try {
    static_cast<void>(
        simulation::Charge::activate(simulation::TickSequence::zero(), kCooldownDuration));
    FAIL("a charge activated on the initial tick was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kChargeActivationInvalid);
    CHECK(error.code() == "SIMULATION.CHARGE_ACTIVATION_INVALID");
    CHECK(error.context() == "charge.activation_tick");
  }
}

TEST_CASE("A charge cooldown is strictly positive where a shield's may legally be zero",
          "[unit][simulation][charge][validation]") {
  // The correction this step exists to hold. Shield admission is gated twice -- the prior
  // protection *and* the prior cooldown must both have ended -- so an empty shield cooldown means
  // "the protection is the wait". A charge has no protection window and therefore one gate, so an
  // empty cooldown means "there is no wait", which at 400 Hz is four hundred bursts a second.
  try {
    static_cast<void>(simulation::Charge::activate(tick(kActivation), 0));
    FAIL("a charge with no cooldown at all was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kChargeActivationInvalid);
    CHECK(error.code() == "SIMULATION.CHARGE_ACTIVATION_INVALID");
    CHECK(error.context() == "charge.cooldown_duration_ticks");
  }

  // The shield's exemption is real and stays exactly as it was: this is a difference between the
  // two abilities, not a rule the charge tightened for both.
  const simulation::Shield without_cooldown = simulation::Shield::activate(
      tick(kActivation), kShieldDuration, kPerfectDuration, kNoCooldown, kParryStunDuration);
  CHECK(without_cooldown.cooldown_window().expired(tick(kActivation)));

  // One tick is a wait; zero is not. The bound is strict positivity and nothing larger, so the
  // shortest cooldown a configuration can round to is still admitted and still blocks the tick it
  // was activated on.
  const simulation::Charge briefest = simulation::Charge::activate(tick(kActivation), kOneTick);
  CHECK(briefest.cooldown_window().contains(tick(kActivation)));
  CHECK(briefest.cooldown_window().expired(tick(kActivation + kOneTick)));
}

TEST_CASE("A charge activation refuses an endpoint past the exact tick domain, never saturating",
          "[unit][simulation][charge][validation]") {
  const auto refuses = [](const std::uint64_t activation, const std::uint64_t cooldown_duration) {
    try {
      static_cast<void>(simulation::Charge::activate(tick(activation), cooldown_duration));
      return false;
    } catch (const simulation::SimulationValidationError& error) {
      return error.code() == "SIMULATION.TICK_WINDOW_EXPIRY_OVERFLOW";
    }
  };

  // The window is built through the same checked constructor every ability timer uses, so an
  // endpoint leaving the exact tick domain is a named refusal rather than a cooldown that quietly
  // became shorter than the one that was asked for.
  CHECK(refuses(simulation::TickSequence::kMaximumValue, kOneTick));
  CHECK(refuses(simulation::TickSequence::kMaximumValue - kOneTick, kOneTick + kOneTick));
  CHECK_FALSE(refuses(simulation::TickSequence::kMaximumValue - kOneTick, kOneTick));
}

TEST_CASE("charge cancellation shortens active time while preserving cooldown and captured stun",
          "[unit][simulation][charge]") {
  STATIC_REQUIRE(CancelableAbility<simulation::Charge>);
  const auto charge = simulation::Charge::activate(tick(kActivation), kCooldownDuration,
                                                   kShieldDuration, kParryStunDuration);
  const auto canceled = charge.canceled_at(tick(kActivation + kOneTick));
  CHECK(canceled.active_window().expiry_tick() == tick(kActivation + kOneTick));
  CHECK(canceled.cooldown_window() == charge.cooldown_window());
  CHECK(canceled.activation_tick() == charge.activation_tick());
  CHECK(canceled.hit_stun_duration_ticks() == kParryStunDuration);
  CHECK(canceled.canceled_at(tick(kActivation + kOneTick)) == canceled);
  CHECK(canceled.canceled_at(tick(kCooldownExpiry)) == canceled);
  CHECK(charge.canceled_at(tick(kActivation)).active_window().expired(tick(kActivation)));
  CHECK(activated().canceled_at(tick(kActivation)) == activated());
}

TEST_CASE("charge contact time is half open and can outlive cooldown",
          "[unit][simulation][charge]") {
  const auto charge = simulation::Charge::activate(tick(kActivation), kOneTick, kShieldDuration,
                                                   kParryStunDuration);
  CHECK(charge.active_window().contains(tick(kActivation)));
  CHECK(charge.active_window().contains(tick(kActivation + kShieldDuration - kOneTick)));
  CHECK_FALSE(charge.active_window().contains(tick(kActivation + kShieldDuration)));
  CHECK(charge.cooldown_window().expired(tick(kActivation + kOneTick)));
  CHECK(charge.active_window().contains(tick(kActivation + kOneTick)));
  CHECK(activated().active_window().expired(tick(kActivation)));
  CHECK(activated().hit_stun_duration_ticks() == 0);
}

TEST_CASE("charge active and hit stun durations cannot be empty",
          "[unit][simulation][charge][validation]") {
  for (const bool empty_active : {false, true}) {
    try {
      static_cast<void>(simulation::Charge::activate(tick(kActivation), kCooldownDuration,
                                                     empty_active ? 0 : kShieldDuration,
                                                     empty_active ? kParryStunDuration : 0));
      FAIL("an empty charge effect was accepted");
    } catch (const simulation::SimulationValidationError& error) {
      CHECK(error.validation_code() ==
            simulation::SimulationValidationCode::kChargeActivationInvalid);
      CHECK(error.context() ==
            (empty_active ? "charge.active_duration_ticks" : "charge.hit_stun_duration_ticks"));
    }
  }
  CHECK_THROWS_AS(
      simulation::Charge::activate(tick(simulation::TickSequence::kMaximumValue - kOneTick),
                                   kOneTick, kShieldDuration, kParryStunDuration),
      simulation::SimulationValidationError);
}

TEST_CASE("charge captured hit stun remains inside the exact published tick domain",
          "[unit][simulation][charge][validation]") {
  for (const auto duration : {kOneTick, simulation::TickSequence::kMaximumValue}) {
    CHECK(simulation::Charge::activate(tick(kActivation), kCooldownDuration, kShieldDuration,
                                       duration)
              .hit_stun_duration_ticks() == duration);
  }
  for (const auto duration : {simulation::TickSequence::kMaximumValue + kOneTick,
                              std::numeric_limits<std::uint64_t>::max()}) {
    CAPTURE(duration);
    try {
      static_cast<void>(simulation::Charge::activate(tick(kActivation), kCooldownDuration,
                                                     kShieldDuration, duration));
      FAIL("an unsafe captured charge duration was accepted");
    } catch (const simulation::SimulationValidationError& error) {
      CHECK(error.validation_code() ==
            simulation::SimulationValidationCode::kChargeActivationInvalid);
      CHECK(error.code() == "SIMULATION.CHARGE_ACTIVATION_INVALID");
      CHECK(error.context() == "charge.hit_stun_duration_ticks");
    }
  }
}

TEST_CASE("charge cancellation before activation reports the chronology error",
          "[unit][simulation][charge][validation]") {
  try {
    static_cast<void>(activated().canceled_at(tick(kActivation - kOneTick)));
    FAIL("charge cancellation before activation was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kChargeCancellationBeforeActivation);
    CHECK(error.code() == "SIMULATION.CHARGE_CANCELLATION_BEFORE_ACTIVATION");
    CHECK(error.context() == "charge.cancellation_tick");
  }
}

TEST_CASE("Two charges compare equal only when every stored value agrees",
          "[unit][simulation][charge]") {
  const simulation::Charge charge = activated();

  CHECK(charge == activated());
  CHECK(charge != simulation::Charge::activate(tick(kActivation + kOneTick), kCooldownDuration));
  CHECK(charge != simulation::Charge::activate(tick(kActivation), kCooldownDuration + kOneTick));
  // Equality is over the stored window, so two activations that happen to end on the same tick are
  // still different charges: the activation is published as the cooldown arc's denominator and a
  // reader that could not tell them apart would draw the wrong arc.
  CHECK(charge !=
        simulation::Charge::activate(tick(kActivation - kOneTick), kCooldownDuration + kOneTick));
}

TEST_CASE("charge equality includes the active window and captured stun duration",
          "[unit][simulation][charge]") {
  const auto charge = simulation::Charge::activate(tick(kActivation), kCooldownDuration,
                                                   kShieldDuration, kParryStunDuration);
  CHECK(charge != activated());
  CHECK(charge != simulation::Charge::activate(tick(kActivation), kCooldownDuration,
                                               kShieldDuration + kOneTick, kParryStunDuration));
  CHECK(charge != simulation::Charge::activate(tick(kActivation), kCooldownDuration,
                                               kShieldDuration, kParryStunDuration + kOneTick));
}
