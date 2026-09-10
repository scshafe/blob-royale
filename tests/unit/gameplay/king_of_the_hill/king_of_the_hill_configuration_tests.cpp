#include "king_of_the_hill/king_of_the_hill_configuration.hpp"

#include "gameplay_validation_error.hpp"
#include "simulation_limits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;

namespace {

using Section = gameplay::KingOfTheHillConfiguration::Section;

[[nodiscard]] Section defaults() { return gameplay::KingOfTheHillConfiguration::default_section(); }

struct Rejection final {
  gameplay::GameplayValidationCode code;
  std::string context;
};

[[nodiscard]] Rejection rejection_of(const Section& section) {
  try {
    static_cast<void>(gameplay::KingOfTheHillConfiguration::create(section));
  } catch (const gameplay::GameplayValidationError& error) {
    return Rejection{error.validation_code(), error.context()};
  }
  FAIL("the section was accepted");
  return Rejection{gameplay::GameplayValidationCode::kGameModeNameUnknown, {}};
}

} // namespace

TEST_CASE("the proposed [king_of_the_hill] section converts to the accepted tick counts",
          "[unit][gameplay][king_of_the_hill][configuration]") {
  const gameplay::KingOfTheHillConfiguration configuration =
      gameplay::KingOfTheHillConfiguration::defaults();

  // The figures ADR 0007's table states for the proposed values, at 400 ticks per second.
  CHECK(configuration.thrust_maximum() == 400.0);
  CHECK(configuration.hill_radius() == 90.0);
  CHECK(configuration.hill_dwell_ticks() == 4'800);
  CHECK(configuration.hill_travel_ticks() == 1'600);
  CHECK(configuration.point_interval_ticks() == 400);
  CHECK(configuration.points_to_win() == 30);
  CHECK_FALSE(configuration.contested_hill_scores());
  CHECK(configuration.time_limit_ticks() == 96'000);
  CHECK(configuration.respawn_delay_ticks() == 800);
  CHECK(configuration.countdown_ticks() == 2'000);
  CHECK(configuration.restart_delay_ticks() == 3'200);
  CHECK(configuration == gameplay::KingOfTheHillConfiguration::create(defaults()));
}

TEST_CASE("every rejected [king_of_the_hill] value names the key it was authored under",
          "[unit][gameplay][king_of_the_hill][configuration][validation]") {
  Section radius = defaults();
  radius.hill_radius_world_units = 0.0;
  CHECK(rejection_of(radius).code ==
        gameplay::GameplayValidationCode::kKingOfTheHillScalarOutOfRange);
  CHECK(rejection_of(radius).context == "king_of_the_hill.hill_radius_world_units");

  Section radius_nan = defaults();
  radius_nan.hill_radius_world_units = std::numeric_limits<double>::quiet_NaN();
  CHECK(rejection_of(radius_nan).code ==
        gameplay::GameplayValidationCode::kKingOfTheHillScalarNotFinite);

  Section dwell = defaults();
  dwell.hill_dwell_seconds = -1.0;
  CHECK(rejection_of(dwell).code == gameplay::GameplayValidationCode::kDurationNegative);
  CHECK(rejection_of(dwell).context == "king_of_the_hill.hill_dwell_seconds");

  Section interval = defaults();
  interval.point_interval_seconds = std::numeric_limits<double>::infinity();
  CHECK(rejection_of(interval).code == gameplay::GameplayValidationCode::kDurationNotFinite);
  CHECK(rejection_of(interval).context == "king_of_the_hill.point_interval_seconds");

  Section time_limit = defaults();
  time_limit.time_limit_seconds = 0.0;
  CHECK(rejection_of(time_limit).code ==
        gameplay::GameplayValidationCode::kKingOfTheHillScalarOutOfRange);
  CHECK(rejection_of(time_limit).context == "king_of_the_hill.time_limit_seconds");

  Section respawn = defaults();
  respawn.respawn_delay_seconds = -0.5;
  CHECK(rejection_of(respawn).context == "king_of_the_hill.respawn_delay_seconds");

  Section thrust = defaults();
  thrust.thrust_max_world_units_per_second_squared = -1.0;
  // Through the steering system's own rule, so the mode cannot accept what the system refuses.
  CHECK(rejection_of(thrust).code == gameplay::GameplayValidationCode::kThrustMaximumOutOfRange);
}

TEST_CASE("a tour whose every stop is instantaneous is refused; a hop is not",
          "[unit][gameplay][king_of_the_hill][configuration][validation]") {
  // Dwell plus travel must convert to at least one tick, or the hill's tour has no position to
  // hold at any stop. Travel alone at zero is the hop the ADR names, and dwell alone at zero is a
  // hill that never stops gliding; both are legal.
  Section still = defaults();
  still.hill_dwell_seconds = 0.0;
  still.hill_travel_seconds = 0.0;
  CHECK(rejection_of(still).code ==
        gameplay::GameplayValidationCode::kKingOfTheHillTourWithoutDuration);
  CHECK(rejection_of(still).context == "king_of_the_hill.hill_travel_seconds");

  Section hop = defaults();
  hop.hill_travel_seconds = 0.0;
  CHECK(gameplay::KingOfTheHillConfiguration::create(hop).hill_travel_ticks() == 0);

  Section glide = defaults();
  glide.hill_dwell_seconds = 0.0;
  CHECK(gameplay::KingOfTheHillConfiguration::create(glide).hill_dwell_ticks() == 0);

  // Under half a tick rounds to zero and is refused for the same reason.
  Section under_a_tick = defaults();
  under_a_tick.hill_dwell_seconds = 0.001;
  under_a_tick.hill_travel_seconds = 0.001;
  CHECK(rejection_of(under_a_tick).code ==
        gameplay::GameplayValidationCode::kKingOfTheHillTourWithoutDuration);
}

TEST_CASE("points_to_win must be at least one",
          "[unit][gameplay][king_of_the_hill][configuration]") {
  Section none = defaults();
  none.points_to_win = 0;
  CHECK(rejection_of(none).code == gameplay::GameplayValidationCode::kKingOfTheHillPointsToWinZero);
  CHECK(rejection_of(none).context == "king_of_the_hill.points_to_win");

  Section one = defaults();
  one.points_to_win = 1;
  CHECK(gameplay::KingOfTheHillConfiguration::create(one).points_to_win() == 1);
}

TEST_CASE("hill radius and points accept their exact publication ceilings",
          "[unit][gameplay][king_of_the_hill][configuration][boundary]") {
  Section section = defaults();
  section.hill_radius_world_units = simulation::kMaximumPhysicalComponentMagnitude;
  section.points_to_win = simulation::kMaximumProtocolSafeInteger;
  const gameplay::KingOfTheHillConfiguration configuration =
      gameplay::KingOfTheHillConfiguration::create(section);
  CHECK(configuration.hill_radius() == simulation::kMaximumPhysicalComponentMagnitude);
  CHECK(configuration.points_to_win() == simulation::kMaximumProtocolSafeInteger);
}

TEST_CASE("hill radius above its publication ceiling fails at its configuration key",
          "[unit][gameplay][king_of_the_hill][configuration][validation][boundary]") {
  Section section = defaults();
  section.hill_radius_world_units = simulation::kMaximumPhysicalComponentMagnitude + 1.0;
  const Rejection rejection = rejection_of(section);
  CHECK(rejection.code == gameplay::GameplayValidationCode::kKingOfTheHillScalarOutOfRange);
  CHECK(rejection.context == "king_of_the_hill.hill_radius_world_units");
}

TEST_CASE("hill points above the safe integer ceiling fail at their configuration key",
          "[unit][gameplay][king_of_the_hill][configuration][validation][boundary]") {
  Section section = defaults();
  section.points_to_win = simulation::kMaximumProtocolSafeInteger + 1;
  const Rejection rejection = rejection_of(section);
  CHECK(rejection.code == gameplay::GameplayValidationCode::kKingOfTheHillScalarOutOfRange);
  CHECK(rejection.context == "king_of_the_hill.points_to_win");
}

TEST_CASE("a zero point interval and a zero respawn delay are accepted rather than rejected",
          "[unit][gameplay][king_of_the_hill][configuration]") {
  // Zero is a legal duration everywhere the increment precedes the test: a point on the first
  // inside tick, and a knocked-out player offered a seat on the very next tick.
  Section section = defaults();
  section.point_interval_seconds = 0.0;
  section.respawn_delay_seconds = 0.0;
  section.countdown_seconds = 0.0;
  section.restart_delay_seconds = 0.0;
  const gameplay::KingOfTheHillConfiguration configuration =
      gameplay::KingOfTheHillConfiguration::create(section);
  CHECK(configuration.point_interval_ticks() == 0);
  CHECK(configuration.respawn_delay_ticks() == 0);
  CHECK(configuration.countdown_ticks() == 0);
  CHECK(configuration.restart_delay_ticks() == 0);
}
