#include "shared/locomotion.hpp"

#include "fixtures/locomotion_fixture.hpp"

#include "gameplay_validation_error.hpp"
#include "movement_tuning.hpp"
#include "physics.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::locomotion_fixture;

namespace {

[[nodiscard]] simulation::Vector2 vector(const fixture::Point point) {
  return simulation::Vector2::create(point.x, point.y);
}

void check_bits(const simulation::Vector2& actual, const simulation::Vector2& expected) {
  CHECK(std::bit_cast<std::uint64_t>(actual.x()) == std::bit_cast<std::uint64_t>(expected.x()));
  CHECK(std::bit_cast<std::uint64_t>(actual.y()) == std::bit_cast<std::uint64_t>(expected.y()));
}

// Every call is expected to succeed. Keep exact reconstruction evidence in scope around the cap
// call: an ordinary sustained-input failure must not be changed into an expected rejection.
[[nodiscard]] simulation::Vector2
trajectory_step(const std::string_view name, const std::uint64_t tick,
                const simulation::Vector2& velocity, const simulation::Vector2& intent,
                const simulation::MovementTuning tuning, const double drag,
                bool& constrained_request_seen) {
  const auto delta = simulation::FixedDelta::canonical();
  const auto requested = gameplay::thrust_acceleration_from_intent(intent, tuning.acceleration());
  CAPTURE(name, tick, drag, tuning.acceleration(), tuning.normal_top_speed());
  INFO("velocity bits: x=0x" << std::hex << std::bit_cast<std::uint64_t>(velocity.x()) << " y=0x"
                             << std::bit_cast<std::uint64_t>(velocity.y())
                             << "; requested bits: x=0x"
                             << std::bit_cast<std::uint64_t>(requested.x()) << " y=0x"
                             << std::bit_cast<std::uint64_t>(requested.y()));
  const double bound =
      std::max(tuning.normal_top_speed() * tuning.normal_top_speed(), velocity.dot(velocity));
  auto raw_endpoint = velocity;
  REQUIRE_NOTHROW(raw_endpoint =
                      simulation::integrate_accelerated_velocity(velocity, requested, delta));
  constrained_request_seen = constrained_request_seen || raw_endpoint.dot(raw_endpoint) > bound;
  auto acceleration = requested;
  REQUIRE_NOTHROW(acceleration = gameplay::limit_normal_propulsion(
                      velocity, requested, tuning.normal_top_speed(), delta));
  auto endpoint = velocity;
  REQUIRE_NOTHROW(endpoint =
                      simulation::integrate_accelerated_velocity(velocity, acceleration, delta));
  REQUIRE(acceleration.dot(acceleration) <= requested.dot(requested));
  REQUIRE(endpoint.dot(endpoint) <= bound);
  auto dragged = endpoint;
  REQUIRE_NOTHROW(dragged = simulation::apply_velocity_drag(endpoint, drag, delta));
  REQUIRE(dragged.dot(dragged) <= endpoint.dot(endpoint));
  return dragged;
}

} // namespace

TEST_CASE("locomotion keeps every uncapped requested acceleration bit including external overspeed",
          "[unit][gameplay][locomotion]") {
  for (const auto& input : fixture::kUncappedCases) {
    CAPTURE(input.name);
    const auto velocity = vector(input.velocity);
    const auto requested = vector(input.acceleration);
    const auto actual = gameplay::limit_normal_propulsion(velocity, requested, input.ceiling,
                                                          simulation::FixedDelta::canonical());
    check_bits(actual, requested);
  }
}

TEST_CASE("locomotion constrains the finite canonical Euler endpoint without amplifying propulsion",
          "[unit][gameplay][locomotion]") {
  const auto delta = simulation::FixedDelta::canonical();
  for (const auto& input : fixture::kConstrainedCases) {
    DYNAMIC_SECTION(input.name) {
      const auto velocity = vector(input.velocity);
      const auto requested = vector(input.acceleration);
      const double bound = std::max(input.ceiling * input.ceiling, velocity.dot(velocity));
      const auto original_endpoint =
          simulation::integrate_accelerated_velocity(velocity, requested, delta);
      REQUIRE(original_endpoint.dot(original_endpoint) > bound);
      const auto actual =
          gameplay::limit_normal_propulsion(velocity, requested, input.ceiling, delta);
      const auto endpoint = simulation::integrate_accelerated_velocity(velocity, actual, delta);
      CHECK(endpoint.dot(endpoint) <= bound);
      CHECK(actual.dot(actual) <= requested.dot(requested));
      CHECK(actual != simulation::Vector2::create(0.0, 0.0));
      CHECK(endpoint != velocity);
      // Exercise the same stateless input twice: rounding attempts retain no hidden history.
      check_bits(gameplay::limit_normal_propulsion(velocity, requested, input.ceiling, delta),
                 actual);
    }
  }
}

TEST_CASE("locomotion permits sideways turning at and above the normal ceiling",
          "[unit][gameplay][locomotion]") {
  const auto delta = simulation::FixedDelta::canonical();
  const auto requested = simulation::Vector2::create(0.0, 400.0);
  for (const double speed : {600.0, 1'200.0}) {
    const auto velocity = simulation::Vector2::create(speed, 0.0);
    const auto actual = gameplay::limit_normal_propulsion(velocity, requested, 600.0, delta);
    const auto endpoint = simulation::integrate_accelerated_velocity(velocity, actual, delta);
    CHECK(actual.x() < 0.0);
    CHECK(actual.y() > 0.0);
    CHECK(endpoint.y() > 0.0);
    CHECK(endpoint.dot(endpoint) <= velocity.dot(velocity));
    if (speed > 600.0) {
      CHECK(endpoint.dot(endpoint) > 600.0 * 600.0);
    }
  }
}

TEST_CASE("locomotion's first rounded projection identity is an explicit zero not a radial proof",
          "[unit][gameplay][locomotion]") {
  const auto delta = simulation::FixedDelta::canonical();
  const auto velocity = simulation::Vector2::create(1.0, 1.0);
  const auto requested = simulation::Vector2::create(
      400.0, std::nextafter(400.0, std::numeric_limits<double>::infinity()));
  REQUIRE(requested.x() != requested.y());
  // The request is not collinear with (1,1), but the canonical endpoint has already rounded both
  // components to2. The FIRST written sqrt/divide/component projection then returns (1,1).
  REQUIRE(simulation::integrate_accelerated_velocity(velocity, requested, delta) ==
          simulation::Vector2::create(2.0, 2.0));
  const auto actual = gameplay::limit_normal_propulsion(velocity, requested, 1.0, delta);
  CHECK(actual == simulation::Vector2::create(0.0, 0.0));
  CHECK(simulation::integrate_accelerated_velocity(velocity, actual, delta) == velocity);
}

TEST_CASE("locomotion retains canonical physical-domain failures before projection",
          "[unit][gameplay][locomotion][validation]") {
  const auto velocity = simulation::Vector2::create(1'000'000'000'000.0, 0.0);
  const auto requested = simulation::Vector2::create(400.0, 0.0);
  try {
    static_cast<void>(gameplay::limit_normal_propulsion(velocity, requested, 600.0,
                                                        simulation::FixedDelta::canonical()));
    FAIL("an unrepresentable requested endpoint bypassed canonical integration");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kPhysicalScalarOutOfRange);
  }
}

TEST_CASE("locomotion uses the movement value's intrinsic ceiling validation",
          "[unit][gameplay][locomotion][validation]") {
  const auto zero = simulation::Vector2::create(0.0, 0.0);
  for (const double speed : {0.0, 10'001.0, std::numeric_limits<double>::infinity()}) {
    CHECK_THROWS_AS(
        gameplay::limit_normal_propulsion(zero, zero, speed, simulation::FixedDelta::canonical()),
        simulation::SimulationValidationError);
  }
}

TEST_CASE("locomotion rejects the derived later radial identity before inventing braking",
          "[unit][gameplay][locomotion][rounding]") {
  const auto delta = simulation::FixedDelta::canonical();
  const auto velocity = vector(fixture::kLaterIdentityVelocity);
  const auto requested = vector(fixture::kLaterIdentityAcceleration);
  const auto endpoint = simulation::integrate_accelerated_velocity(velocity, requested, delta);
  check_bits(endpoint, vector(fixture::kLaterIdentityEndpoint));
  REQUIRE(velocity.dot(velocity) == fixture::kLaterIdentityBoundSquared);
  REQUIRE(endpoint.dot(endpoint) == fixture::kLaterIdentityEndpointSquared);
  REQUIRE(std::sqrt(velocity.dot(velocity)) == fixture::kLaterIdentityBoundRoot);
  REQUIRE(std::sqrt(endpoint.dot(endpoint)) == 2.0 * fixture::kLaterIdentityBoundRoot);
  const double first_scale = std::sqrt(velocity.dot(velocity)) / std::sqrt(endpoint.dot(endpoint));
  REQUIRE(first_scale == 0x1p-1);
  const auto first_target = endpoint * first_scale;
  check_bits(first_target, vector(fixture::kLaterIdentityFirstTarget));
  REQUIRE(first_target != velocity);
  REQUIRE(first_target.dot(first_target) == fixture::kLaterIdentityFirstTargetSquared);
  REQUIRE(first_target.dot(first_target) > velocity.dot(velocity));

  const auto projected_acceleration = (first_target - velocity) / delta.seconds();
  double magnitude_scale = 1.0;
  REQUIRE(gameplay::kMaximumLocomotionRoundingStepCount + 1 == fixture::kLaterIdentityNormAttempts);
  for (std::uint64_t attempt = 0; attempt < fixture::kLaterIdentityNormAttempts; ++attempt) {
    CAPTURE(attempt);
    const auto candidate = projected_acceleration * magnitude_scale;
    REQUIRE(candidate.dot(candidate) <= requested.dot(requested));
    check_bits(simulation::integrate_accelerated_velocity(velocity, candidate, delta),
               first_target);
    magnitude_scale = std::nextafter(magnitude_scale, 0.0);
  }
  const double next_radial_scale = std::nextafter(first_scale, 0.0);
  REQUIRE(next_radial_scale == 0x1.fffffffffffffp-2);
  check_bits(endpoint * next_radial_scale, velocity);
  try {
    static_cast<void>(gameplay::limit_normal_propulsion(velocity, requested, 1.0, delta));
    FAIL("a later radial identity silently erased the selected nonidentity step");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() == gameplay::GameplayValidationCode::kLocomotionPrecisionLost);
    CHECK(error.context() == "locomotion.normal_propulsion");
    CHECK(error.detail() == "rounding correction erased a nonidentity projected step");
  }
}

TEST_CASE("locomotion sustained default held inputs succeed at the cap and under canonical drag",
          "[unit][gameplay][locomotion][trajectory]") {
  const auto tuning = simulation::MovementTuning::defaults();
  REQUIRE(tuning.acceleration() == fixture::kTrajectoryAcceleration);
  REQUIRE(tuning.normal_top_speed() == fixture::kTrajectoryCeiling);
  for (const auto& input : fixture::kHeldTrajectories) {
    for (const double drag : fixture::kTrajectoryDragValues) {
      DYNAMIC_SECTION(input.name << " drag=" << drag) {
        auto velocity = vector(input.initial_velocity);
        const auto intent = gameplay::normalized_thrust_intent(vector(input.direction));
        bool constrained_request_seen = false;
        for (std::uint64_t tick = 1; tick <= fixture::kHeldTrajectoryTicks; ++tick) {
          velocity = trajectory_step(input.name, tick, velocity, intent, tuning, drag,
                                     constrained_request_seen);
        }
        const double initial_squared =
            vector(input.initial_velocity).dot(vector(input.initial_velocity));
        if (drag == 0.0) {
          REQUIRE(constrained_request_seen);
          const double expected_speed =
              std::max(tuning.normal_top_speed(), std::sqrt(initial_squared));
          // This is only an end-horizon reach/hold check; every actual speed-admission predicate
          // above remains strict, without a tolerance band.
          CHECK(std::sqrt(velocity.dot(velocity)) >=
                expected_speed - fixture::kTrajectorySteadySpeedMargin);
        } else {
          // Defaults with drag2 settle at199wu/s for unit intent, not600. Include initially capped
          // and externally overspeed cases to exercise the cap before unchanged drag decays them.
          if (initial_squared >= tuning.normal_top_speed() * tuning.normal_top_speed()) {
            CHECK(constrained_request_seen);
          }
          const auto requested =
              gameplay::thrust_acceleration_from_intent(intent, tuning.acceleration());
          const double damping = 1.0 - (drag * simulation::FixedDelta::canonical().seconds());
          const auto steady_velocity = requested * (damping / drag);
          CHECK(std::abs(velocity.x() - steady_velocity.x()) <=
                fixture::kTrajectorySteadySpeedMargin);
          CHECK(std::abs(velocity.y() - steady_velocity.y()) <=
                fixture::kTrajectorySteadySpeedMargin);
        }
      }
    }
  }
}

TEST_CASE("locomotion sustained turning reversal and lowered held limits succeed without velocity "
          "clamping",
          "[unit][gameplay][locomotion][trajectory]") {
  for (const double drag : fixture::kTrajectoryDragValues) {
    DYNAMIC_SECTION("turn_reverse_lower drag=" << drag) {
      auto velocity = simulation::Vector2::create(0.0, 0.0);
      std::optional<simulation::Vector2> intent;
      std::uint64_t tick = 0;
      bool constrained_request_seen = false;
      for (const auto& leg : fixture::kChangingTrajectory) {
        CAPTURE(leg.name);
        if (leg.new_direction) {
          intent = gameplay::normalized_thrust_intent(vector(*leg.new_direction));
        }
        REQUIRE(intent.has_value());
        const auto tuning =
            simulation::MovementTuning::create(fixture::kTrajectoryAcceleration, leg.ceiling);
        const auto entry_velocity = velocity;
        for (std::uint64_t local_tick = 1; local_tick <= leg.ticks; ++local_tick) {
          ++tick;
          velocity = trajectory_step(leg.name, tick, velocity, *intent, tuning, drag,
                                     constrained_request_seen);
          if (!leg.new_direction && local_tick == 1) {
            REQUIRE(entry_velocity.dot(entry_velocity) > leg.ceiling * leg.ceiling);
            CHECK(velocity.dot(velocity) > leg.ceiling * leg.ceiling);
          }
        }
      }
      REQUIRE(constrained_request_seen);
      CHECK(velocity.y() < 0.0);
    }
  }
}

TEST_CASE("unit_direction normalizes every constructible direction, subunit ones included",
          "[unit][gameplay][locomotion][charge]") {
  // The whole reason charge cannot reuse `normalized_thrust_intent`: that function's scale is
  // exactly 1.0 at or below unit magnitude, so a client sending a half-length direction -- legal
  // under the per-component unit bound the wire applies -- would author its own burst strength.
  // This one divides in every case, so the direction decides only where.
  const auto subunit = simulation::Vector2::create(0.5, 0.0);
  CHECK(gameplay::normalized_thrust_intent(subunit) == subunit);
  const std::optional<simulation::Vector2> normalized = gameplay::unit_direction(subunit);
  REQUIRE(normalized.has_value());
  CHECK(*normalized == simulation::Vector2::create(1.0, 0.0));

  // A non-unit direction with an exact magnitude, so the expected components are exact too.
  const std::optional<simulation::Vector2> scaled =
      gameplay::unit_direction(simulation::Vector2::create(3.0, 4.0));
  REQUIRE(scaled.has_value());
  CHECK(*scaled == simulation::Vector2::create(0.6, 0.8));
  CHECK(std::sqrt((scaled->x() * scaled->x()) + (scaled->y() * scaled->y())) == 1.0);

  // An already-unit direction is returned unchanged rather than re-scaled by a rounded 1.0.
  for (const auto& unit :
       {simulation::Vector2::create(1.0, 0.0), simulation::Vector2::create(0.0, -1.0),
        simulation::Vector2::create(-1.0, 0.0)}) {
    const std::optional<simulation::Vector2> preserved = gameplay::unit_direction(unit);
    REQUIRE(preserved.has_value());
    check_bits(*preserved, unit);
  }

  // A direction at the far edge of the component domain still normalizes: the squared sum of two
  // 1e12 components is 2e24, nowhere near the overflow the domain exists to keep out.
  const std::optional<simulation::Vector2> extreme = gameplay::unit_direction(
      simulation::Vector2::create(simulation::kMaximumPhysicalComponentMagnitude,
                                  simulation::kMaximumPhysicalComponentMagnitude));
  REQUIRE(extreme.has_value());
  CHECK(extreme->x() == extreme->y());
  CHECK(std::abs(extreme->x() - std::sqrt(0.5)) <= std::numeric_limits<double>::epsilon());
}

TEST_CASE("unit_direction divides by the magnitude rather than multiplying by a reciprocal",
          "[unit][gameplay][locomotion][charge]") {
  // `(1, 7)` is a direction where the two orders genuinely disagree: the reciprocal product lands
  // one ulp above the quotient on y. The REQUIRE below is what makes this case evidence rather
  // than a restatement of the implementation -- without it, a reciprocal implementation would pass.
  const double magnitude = std::sqrt((1.0 * 1.0) + (7.0 * 7.0));
  const double reciprocal = 1.0 / magnitude;
  REQUIRE((7.0 * reciprocal) != (7.0 / magnitude));
  const std::optional<simulation::Vector2> normalized =
      gameplay::unit_direction(simulation::Vector2::create(1.0, 7.0));
  REQUIRE(normalized.has_value());
  check_bits(*normalized, simulation::Vector2::create(1.0 / magnitude, 7.0 / magnitude));
}

TEST_CASE("unit_direction refuses the whole non-constructible band and never throws",
          "[unit][gameplay][locomotion][charge][validation]") {
  // Zero is not the only refusal. `1e-200` passes the decoder and `InputBatch` -- it is inside the
  // per-component unit bound -- and its squared magnitude underflows to exactly zero, so a
  // reciprocal would be infinite and `Vector2::create` would throw. That is the case a client can
  // actually send, and the refusal is a silent nullopt so admission stays a no-op rather than a
  // thrown tick (`shared/ability_system.hpp`).
  constexpr double kUnderflowingComponent = 1e-200;
  for (const auto& refused :
       {simulation::Vector2::create(0.0, 0.0), simulation::Vector2::create(-0.0, 0.0),
        simulation::Vector2::create(kUnderflowingComponent, 0.0),
        simulation::Vector2::create(0.0, kUnderflowingComponent),
        simulation::Vector2::create(kUnderflowingComponent, kUnderflowingComponent),
        simulation::Vector2::create(-kUnderflowingComponent, kUnderflowingComponent)}) {
    CAPTURE(refused.x(), refused.y());
    std::optional<simulation::Vector2> answer = simulation::Vector2::create(1.0, 0.0);
    REQUIRE_NOTHROW(answer = gameplay::unit_direction(refused));
    CHECK_FALSE(answer.has_value());
  }

  // The band is the underflowed one, not "small": `1e-150` squares to a normal number, so it
  // normalizes exactly. A refusal band drawn by magnitude rather than by representability would
  // have swallowed this direction too.
  const std::optional<simulation::Vector2> tiny =
      gameplay::unit_direction(simulation::Vector2::create(1e-150, 0.0));
  REQUIRE(tiny.has_value());
  CHECK(*tiny == simulation::Vector2::create(1.0, 0.0));
}
