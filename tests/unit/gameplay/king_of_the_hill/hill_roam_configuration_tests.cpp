#include "fixtures/hill_roam_configuration_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace gameplay = blob_royale::gameplay;
namespace fixture = blob_royale::testing::hill_roam_configuration_fixture;

TEST_CASE("hill roaming configuration defaults preserve marker tours and name the strict policy",
          "[unit][gameplay][king_of_the_hill][configuration][hill_roam]") {
  const auto configuration = fixture::Configuration::defaults();
  CHECK(configuration.motion_policy() == fixture::Policy::kMarkerTour);
  CHECK(configuration.hill_speed_minimum() == fixture::Configuration::kDefaultHillSpeedMinimum);
  CHECK(configuration.hill_speed_maximum() == fixture::Configuration::kDefaultHillSpeedMaximum);
  CHECK(configuration.hill_retarget_minimum_ticks() == fixture::kDefaultMinimumTicks);
  CHECK(configuration.hill_retarget_maximum_ticks() == fixture::kDefaultMaximumTicks);
  CHECK(fixture::Configuration::parse_motion_policy(fixture::kTour) ==
        fixture::Policy::kMarkerTour);
  CHECK(fixture::Configuration::parse_motion_policy(fixture::kRoam) ==
        fixture::Policy::kRandomRoam);
  CHECK_THROWS_AS(fixture::Configuration::parse_motion_policy(fixture::kUnknown),
                  gameplay::GameplayValidationError);
  auto section = fixture::Configuration::default_section();
  section.hill_motion = fixture::kInvalidPolicy;
  CHECK_THROWS_AS(fixture::Configuration::create(section), gameplay::GameplayValidationError);
}

TEST_CASE("hill roaming configuration rejects nonfinite unordered and unrepresentable ranges",
          "[unit][gameplay][king_of_the_hill][configuration][hill_roam][validation]") {
  for (const auto& invalid : fixture::invalid_scalars()) {
    INFO(invalid.key);
    auto section = fixture::Configuration::default_section();
    section.*(invalid.member) = invalid.value;
    try {
      static_cast<void>(fixture::Configuration::create(section));
      FAIL("invalid hill roaming scalar was accepted");
    } catch (const gameplay::GameplayValidationError& error) {
      CHECK(error.validation_code() == invalid.code);
      CHECK(error.context() == "king_of_the_hill." + std::string(invalid.key));
    }
  }
}

TEST_CASE("hill roaming configuration accepts exact scalar bounds and singleton intervals",
          "[unit][gameplay][king_of_the_hill][configuration][hill_roam][boundary]") {
  auto section = fixture::Configuration::default_section();
  section.hill_motion = fixture::Policy::kRandomRoam;
  section.hill_speed_minimum = fixture::Configuration::kMinimumHillSpeed;
  section.hill_speed_maximum = fixture::Configuration::kMaximumHillSpeed;
  section.hill_retarget_minimum_seconds = fixture::Configuration::kMaximumHillRetargetSeconds;
  section.hill_retarget_maximum_seconds = fixture::Configuration::kMaximumHillRetargetSeconds;
  const auto configuration = fixture::Configuration::create(section);
  CHECK(configuration.motion_policy() == fixture::Policy::kRandomRoam);
  CHECK(configuration.hill_speed_minimum() == section.hill_speed_minimum);
  CHECK(configuration.hill_speed_maximum() == section.hill_speed_maximum);
  CHECK(configuration.hill_retarget_minimum_ticks() == fixture::kMaximumTicks);
  CHECK(configuration.hill_retarget_maximum_ticks() == fixture::kMaximumTicks);
}

TEST_CASE("hill roaming rejects reversed authored seconds even when both round to one tick",
          "[unit][gameplay][king_of_the_hill][configuration][hill_roam][validation]") {
  auto section = fixture::Configuration::default_section();
  section.hill_retarget_minimum_seconds = fixture::kReversedRoundedMinimumSeconds;
  section.hill_retarget_maximum_seconds = fixture::kReversedRoundedMaximumSeconds;
  CHECK_THROWS_AS(fixture::Configuration::create(section), gameplay::GameplayValidationError);
}
