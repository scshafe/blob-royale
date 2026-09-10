#include "race/race_configuration.hpp"

#include "gameplay_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <string>
#include <string_view>

namespace gameplay = blob_royale::gameplay;

namespace {

using Section = gameplay::RaceConfiguration::Section;

struct Rejection final {
  gameplay::GameplayValidationCode code;
  std::string context;
};

[[nodiscard]] Rejection rejection_of(const Section& section) {
  try {
    static_cast<void>(gameplay::RaceConfiguration::create(section));
  } catch (const gameplay::GameplayValidationError& error) {
    return Rejection{error.validation_code(), error.context()};
  }
  FAIL("the section was accepted");
  return Rejection{gameplay::GameplayValidationCode::kGameModeNameUnknown, {}};
}

struct Field final {
  double Section::*member;
  std::string_view key;
};

constexpr std::array<Field, 3> kPositiveScalars = {{
    {&Section::track_half_width_world_units, "track_half_width_world_units"},
    {&Section::checkpoint_radius_world_units, "checkpoint_radius_world_units"},
    {&Section::time_limit_seconds, "time_limit_seconds"},
}};

constexpr std::array<Field, 4> kNonnegativeDurations = {{
    {&Section::respawn_delay_seconds, "respawn_delay_seconds"},
    {&Section::finish_window_seconds, "finish_window_seconds"},
    {&Section::countdown_seconds, "countdown_seconds"},
    {&Section::restart_delay_seconds, "restart_delay_seconds"},
}};

} // namespace

TEST_CASE("the proposed [race] section converts to the accepted tick counts",
          "[unit][gameplay][race][configuration]") {
  const gameplay::RaceConfiguration configuration = gameplay::RaceConfiguration::defaults();
  CHECK(configuration.thrust_maximum() == 400.0);
  CHECK(configuration.track_half_width() == 70.0);
  CHECK(configuration.checkpoint_radius() == 40.0);
  CHECK(configuration.respawn_delay_ticks() == 800);
  CHECK(configuration.finish_window_ticks() == 8'000);
  CHECK(configuration.time_limit_ticks() == 96'000);
  CHECK(configuration.countdown_ticks() == 2'000);
  CHECK(configuration.restart_delay_ticks() == 3'200);
  CHECK(configuration ==
        gameplay::RaceConfiguration::create(gameplay::RaceConfiguration::default_section()));
}

TEST_CASE("race dimensions and the time limit reject zero and negative values with their keys",
          "[unit][gameplay][race][configuration][validation]") {
  for (const Field& field : kPositiveScalars) {
    for (const double invalid : {0.0, -1.0}) {
      CAPTURE(field.key, invalid);
      Section section = gameplay::RaceConfiguration::default_section();
      section.*field.member = invalid;
      const Rejection rejection = rejection_of(section);
      CHECK(rejection.code == gameplay::GameplayValidationCode::kRaceScalarOutOfRange);
      CHECK(rejection.context == "race." + std::string(field.key));
    }
  }
}

TEST_CASE("race dimensions and the time limit reject every nonfinite value",
          "[unit][gameplay][race][configuration][validation]") {
  constexpr std::array<double, 3> nonfinite = {std::numeric_limits<double>::quiet_NaN(),
                                               std::numeric_limits<double>::infinity(),
                                               -std::numeric_limits<double>::infinity()};
  for (const Field& field : kPositiveScalars) {
    for (const double invalid : nonfinite) {
      CAPTURE(field.key, invalid);
      Section section = gameplay::RaceConfiguration::default_section();
      section.*field.member = invalid;
      const Rejection rejection = rejection_of(section);
      CHECK(rejection.code == gameplay::GameplayValidationCode::kRaceScalarNotFinite);
      CHECK(rejection.context == "race." + std::string(field.key));
    }
  }
}

TEST_CASE("the race checkpoint radius may equal the half-width but never exceed it",
          "[unit][gameplay][race][configuration][validation]") {
  Section section = gameplay::RaceConfiguration::default_section();
  section.checkpoint_radius_world_units = section.track_half_width_world_units;
  CHECK(gameplay::RaceConfiguration::create(section).checkpoint_radius() == 70.0);
  section.checkpoint_radius_world_units += 0.001;
  const Rejection rejection = rejection_of(section);
  CHECK(rejection.code == gameplay::GameplayValidationCode::kRaceScalarOutOfRange);
  CHECK(rejection.context == "race.checkpoint_radius_world_units");
}

TEST_CASE("every race nonnegative duration uses shared duration rejection and names its key",
          "[unit][gameplay][race][configuration][validation]") {
  for (const Field& field : kNonnegativeDurations) {
    CAPTURE(field.key);
    Section section = gameplay::RaceConfiguration::default_section();
    section.*field.member = -1.0;
    CHECK(rejection_of(section).code == gameplay::GameplayValidationCode::kDurationNegative);
    CHECK(rejection_of(section).context == "race." + std::string(field.key));
    section.*field.member = std::numeric_limits<double>::infinity();
    CHECK(rejection_of(section).code == gameplay::GameplayValidationCode::kDurationNotFinite);
    CHECK(rejection_of(section).context == "race." + std::string(field.key));
    section.*field.member = std::numeric_limits<double>::max();
    CHECK(rejection_of(section).code == gameplay::GameplayValidationCode::kDurationTickOverflow);
  }
}

TEST_CASE("race zero delays and a zero finish window are accepted",
          "[unit][gameplay][race][configuration]") {
  Section section = gameplay::RaceConfiguration::default_section();
  for (const Field& field : kNonnegativeDurations) {
    section.*field.member = 0.0;
  }
  const gameplay::RaceConfiguration configuration = gameplay::RaceConfiguration::create(section);
  CHECK(configuration.respawn_delay_ticks() == 0);
  CHECK(configuration.finish_window_ticks() == 0);
  CHECK(configuration.countdown_ticks() == 0);
  CHECK(configuration.restart_delay_ticks() == 0);
}

TEST_CASE("race thrust reuses the steering system validation rule",
          "[unit][gameplay][race][configuration][validation]") {
  Section section = gameplay::RaceConfiguration::default_section();
  section.thrust_max_world_units_per_second_squared = -1.0;
  CHECK(rejection_of(section).code == gameplay::GameplayValidationCode::kThrustMaximumOutOfRange);
  section.thrust_max_world_units_per_second_squared = std::numeric_limits<double>::quiet_NaN();
  CHECK(rejection_of(section).code == gameplay::GameplayValidationCode::kThrustMaximumNotFinite);
  section.thrust_max_world_units_per_second_squared = 0.0;
  CHECK(gameplay::RaceConfiguration::create(section).thrust_maximum() == 0.0);
}

TEST_CASE("race validation reports the first invalid key in the authored order",
          "[unit][gameplay][race][configuration][validation]") {
  Section section = gameplay::RaceConfiguration::default_section();
  section.respawn_delay_seconds = -1.0;
  section.finish_window_seconds = -1.0;
  CHECK(rejection_of(section).context == "race.respawn_delay_seconds");
}
