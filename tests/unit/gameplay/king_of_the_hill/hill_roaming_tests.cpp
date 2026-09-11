#include "king_of_the_hill/hill_roaming.hpp"

#include "fixtures/hill_roaming_fixture.hpp"

#include "gameplay_validation_error.hpp"
#include "physics.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::hill_roaming_fixture;

namespace {

[[nodiscard]] simulation::Vector2 vector(const fixture::Point point) {
  return simulation::Vector2::create(point.x, point.y);
}

void check_bits(const simulation::Vector2& actual, const simulation::Vector2& expected) {
  CHECK(std::bit_cast<std::uint64_t>(actual.x()) == std::bit_cast<std::uint64_t>(expected.x()));
  CHECK(std::bit_cast<std::uint64_t>(actual.y()) == std::bit_cast<std::uint64_t>(expected.y()));
}

} // namespace

TEST_CASE("hill roam cancels whole overshooting axes and outward exact arrivals without bouncing",
          "[unit][gameplay][king_of_the_hill][hill_roaming]") {
  for (const auto& input : fixture::kBoundaryCases) {
    DYNAMIC_SECTION(input.name) {
      const auto bounds = simulation::ArenaBounds::create(input.bounds.x, input.bounds.y);
      const auto actual = gameplay::advance_hill_roam(vector(input.center), vector(input.velocity),
                                                      bounds, simulation::FixedDelta::canonical());
      check_bits(actual.center, vector(input.expected_center));
      check_bits(actual.velocity, vector(input.expected_velocity));
      CHECK(bounds.contains(actual.center));
    }
  }
}

TEST_CASE("hill roam preserves canonical unbounded multiply then integrate bits away from edges",
          "[unit][gameplay][king_of_the_hill][hill_roaming][determinism]") {
  const auto delta = simulation::FixedDelta::canonical();
  const auto bounds =
      simulation::ArenaBounds::create(fixture::kLargeBounds.x, fixture::kLargeBounds.y);
  for (const auto& input : fixture::kInteriorCases) {
    CAPTURE(input.name);
    const auto center = vector(input.center);
    const auto velocity = vector(input.velocity);
    const auto expected_motion = simulation::resolve_unbounded_motion(velocity, delta);
    const auto expected_center =
        simulation::integrate_position(center, expected_motion.displacement());
    const auto actual = gameplay::advance_hill_roam(center, velocity, bounds, delta);
    check_bits(actual.center, expected_center);
    check_bits(actual.velocity, velocity);
  }
}

TEST_CASE("hill roam rejects outside starting centers instead of silently relocating them",
          "[unit][gameplay][king_of_the_hill][hill_roaming][validation]") {
  const auto bounds =
      simulation::ArenaBounds::create(fixture::kValidationBounds.x, fixture::kValidationBounds.y);
  for (const auto center : fixture::kOutsideCenters) {
    CAPTURE(center.x, center.y);
    try {
      static_cast<void>(gameplay::advance_hill_roam(vector(center), vector(fixture::kZero), bounds,
                                                    simulation::FixedDelta::canonical()));
      FAIL("hill boundary cancellation accepted an outside committed center");
    } catch (const gameplay::GameplayValidationError& error) {
      CHECK(error.validation_code() ==
            gameplay::GameplayValidationCode::kKingOfTheHillScalarOutOfRange);
      CHECK(error.context() == "hill_roaming.center");
    }
  }
}

TEST_CASE(
    "hill roam sampling fixes heading quadrant speed interval draw order and bounded realization",
    "[unit][gameplay][king_of_the_hill][hill_roaming][determinism]") {
  for (const auto& input : fixture::kSamplingCases) {
    const auto configuration = fixture::configuration(input);
    for (const auto seed : fixture::kSeeds) {
      CAPTURE(input.name, seed);
      auto random = simulation::DeterministicRandom::create(seed);
      auto expected_random = random;
      auto replay_random = random;
      for (std::size_t index = 0; index < fixture::kSamplesPerSeed; ++index) {
        CAPTURE(index);
        const auto draws_before = random.draw_count();
        const double parameter = expected_random.next_unit_interval();
        const auto quadrant = expected_random.next_below(4);
        const double speed_fraction = expected_random.next_unit_interval();
        const double selected_speed =
            configuration.hill_speed_minimum() +
            ((configuration.hill_speed_maximum() - configuration.hill_speed_minimum()) *
             speed_fraction);
        const auto expected_interval =
            configuration.hill_retarget_minimum_ticks() +
            expected_random.next_below(configuration.hill_retarget_maximum_ticks() -
                                       configuration.hill_retarget_minimum_ticks() + 1);

        const auto actual = gameplay::sample_hill_roam(random, configuration);
        CHECK(actual == gameplay::sample_hill_roam(replay_random, configuration));
        CHECK(random == expected_random);
        CHECK(random.draw_count() >= draws_before + fixture::kMinimumCallsPerSelection);
        CHECK(actual.retarget_after_ticks == expected_interval);
        CHECK(actual.retarget_after_ticks >= configuration.hill_retarget_minimum_ticks());
        CHECK(actual.retarget_after_ticks <= configuration.hill_retarget_maximum_ticks());
        CHECK(selected_speed >= configuration.hill_speed_minimum());
        CHECK(selected_speed <= configuration.hill_speed_maximum());
        const double realized_speed = std::sqrt(actual.velocity.dot(actual.velocity));
        CHECK(realized_speed > 0.0);
        CHECK(std::isfinite(realized_speed));
        CHECK(std::abs(realized_speed - selected_speed) <=
              selected_speed * fixture::kRelativeSpeedRoundoff);

        const bool secondary_zero = parameter == 0.0;
        if (quadrant == 0) {
          CHECK(actual.velocity.x() > 0.0);
          CHECK((actual.velocity.y() == 0.0) == secondary_zero);
          CHECK(actual.velocity.y() >= 0.0);
        } else if (quadrant == 1) {
          CHECK(actual.velocity.y() > 0.0);
          CHECK((actual.velocity.x() == 0.0) == secondary_zero);
          CHECK(actual.velocity.x() <= 0.0);
        } else if (quadrant == 2) {
          CHECK(actual.velocity.x() < 0.0);
          CHECK((actual.velocity.y() == 0.0) == secondary_zero);
          CHECK(actual.velocity.y() <= 0.0);
        } else {
          CHECK(actual.velocity.y() < 0.0);
          CHECK((actual.velocity.x() == 0.0) == secondary_zero);
          CHECK(actual.velocity.x() >= 0.0);
        }
      }
    }
  }
}

TEST_CASE("hill roam equal speed and interval ranges still consume all four draws",
          "[unit][gameplay][king_of_the_hill][hill_roaming][determinism]") {
  for (const auto& input : {fixture::kMinimumSingleton, fixture::kMaximumSingleton}) {
    const auto configuration = fixture::configuration(input);
    for (const auto seed : fixture::kSeeds) {
      CAPTURE(input.name, seed);
      auto random = simulation::DeterministicRandom::create(seed);
      for (std::size_t index = 0; index < fixture::kSamplesPerSeed; ++index) {
        static_cast<void>(gameplay::sample_hill_roam(random, configuration));
        CHECK(random.draw_count() == (index + 1) * fixture::kMinimumCallsPerSelection);
      }
    }
  }
}

TEST_CASE("hill roam seed zero retains the written rational normalization and speed arithmetic",
          "[unit][gameplay][king_of_the_hill][hill_roaming][determinism]") {
  const auto configuration = fixture::configuration(fixture::kDefaultSampling);
  auto random = simulation::DeterministicRandom::create(fixture::kSeeds.front());
  const double parameter = static_cast<double>(fixture::kSeedZeroFirstDraw >> 11U) * 0x1p-53;
  const double speed_fraction = static_cast<double>(fixture::kSeedZeroThirdDraw >> 11U) * 0x1p-53;
  const double parameter_squared = parameter * parameter;
  const double denominator = 1.0 + parameter_squared;
  const double first = (1.0 - parameter_squared) / denominator;
  const double second = (2.0 * parameter) / denominator;
  const double length = std::sqrt((first * first) + (second * second));
  const double speed =
      fixture::kDefaultSampling.speed_minimum +
      ((fixture::kDefaultSampling.speed_maximum - fixture::kDefaultSampling.speed_minimum) *
       speed_fraction);
  const auto expected =
      simulation::Vector2::create((first / length) * speed, (second / length) * speed);
  check_bits(gameplay::sample_hill_roam(random, configuration).velocity, expected);
}

TEST_CASE("hill roam zero rational parameter selects a nonzero cardinal velocity without retry",
          "[unit][gameplay][king_of_the_hill][hill_roaming][determinism]") {
  const auto configuration = fixture::configuration(fixture::kMinimumSingleton);
  auto random = simulation::DeterministicRandom::create(fixture::kCardinalHeadingSeed);
  auto expected_random = random;
  REQUIRE(expected_random.next_unit_interval() == 0.0);
  REQUIRE(expected_random.next_below(4) == 3);
  const auto actual = gameplay::sample_hill_roam(random, configuration);
  check_bits(actual.velocity,
             simulation::Vector2::create(0.0, -fixture::kMinimumSingleton.speed_minimum));
  CHECK(random.draw_count() == fixture::kMinimumCallsPerSelection);
}

TEST_CASE(
    "hill roam retains canonical interval rejection draws instead of promising four raw draws",
    "[unit][gameplay][king_of_the_hill][hill_roaming][determinism]") {
  const auto configuration = fixture::configuration(fixture::kDefaultSampling);
  auto random = simulation::DeterministicRandom::create(fixture::kIntervalRejectionSeed);
  auto expected_random = random;
  static_cast<void>(expected_random.next_unit_interval());
  static_cast<void>(expected_random.next_below(4));
  static_cast<void>(expected_random.next_unit_interval());
  auto raw_draw_probe = expected_random;
  REQUIRE(raw_draw_probe.next_bits() == 0);
  const auto expected_offset =
      expected_random.next_below(configuration.hill_retarget_maximum_ticks() -
                                 configuration.hill_retarget_minimum_ticks() + 1);
  const auto actual = gameplay::sample_hill_roam(random, configuration);
  CHECK(actual.retarget_after_ticks ==
        configuration.hill_retarget_minimum_ticks() + expected_offset);
  CHECK(random == expected_random);
  CHECK(random.draw_count() == fixture::kIntervalRejectionDrawCount);
}

TEST_CASE(
    "hill roam minimum selected speed still changes an interior center near maximum map extent",
    "[unit][gameplay][king_of_the_hill][hill_roaming]") {
  const auto configuration = fixture::configuration(fixture::kMinimumSingleton);
  const auto bounds =
      simulation::ArenaBounds::create(fixture::kLargeBounds.x, fixture::kLargeBounds.y);
  const auto center = vector(fixture::kNearMaximumInteriorCenter);
  for (const auto seed : fixture::kSeeds) {
    CAPTURE(seed);
    auto random = simulation::DeterministicRandom::create(seed);
    for (std::size_t index = 0; index < fixture::kSamplesPerSeed; ++index) {
      const auto selected = gameplay::sample_hill_roam(random, configuration);
      const auto actual = gameplay::advance_hill_roam(center, selected.velocity, bounds,
                                                      simulation::FixedDelta::canonical());
      CHECK(actual.center != center);
      check_bits(actual.velocity, selected.velocity);
      CHECK(bounds.contains(actual.center));
    }
  }
}
