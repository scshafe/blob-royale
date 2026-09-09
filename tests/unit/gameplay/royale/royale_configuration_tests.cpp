#include "royale/royale_configuration.hpp"

#include "gameplay_validation_error.hpp"
#include "seat_roster.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] gameplay::RoyaleConfiguration::Section
section_with_zone_shrink(const double seconds) {
  gameplay::RoyaleConfiguration::Section section = gameplay::RoyaleConfiguration::default_section();
  section.zone_shrink_seconds = seconds;
  return section;
}

[[nodiscard]] gameplay::GameplayValidationCode
rejection_code_of(const gameplay::RoyaleConfiguration::Section& section) {
  try {
    static_cast<void>(gameplay::RoyaleConfiguration::create(section));
  } catch (const gameplay::GameplayValidationError& error) {
    return error.validation_code();
  }
  FAIL("the section was accepted");
  return gameplay::GameplayValidationCode::kGameModeNameUnknown;
}

} // namespace

TEST_CASE("the proposed [royale] section converts to the accepted tick counts",
          "[unit][gameplay][royale][configuration]") {
  const gameplay::RoyaleConfiguration configuration = gameplay::RoyaleConfiguration::defaults();

  // The four figures `docs/architecture/0005-royale-mode.md` § "Mode configuration" states for the
  // proposed values, which is the one place a misread duration would be a silent balance bug.
  CHECK(configuration.countdown_ticks() == 2'000);
  CHECK(configuration.zone_shrink_ticks() == 36'000);
  CHECK(configuration.elimination_grace_ticks() == 1'200);
  CHECK(configuration.restart_delay_ticks() == 3'200);

  CHECK(configuration.thrust_maximum() == 400.0);
  CHECK(configuration.zone_minimum_radius() == 60.0);
  CHECK(configuration.lobby_seat_count() == 4);
}

TEST_CASE("a rejected [royale] duration still names the key it was authored under",
          "[unit][gameplay][royale][configuration][validation]") {
  // The conversion moved to `shared/duration_ticks.hpp` when hazards became its second customer,
  // and this is the regression that says the move changed nothing an operator reads: the context is
  // still the `royale.<key>` spelling this section has always reported, because the mode passes the
  // full context now that the shared helper cannot know whose duration it is holding. Only the code
  // changed, from `GAMEPLAY.ROYALE_DURATION_TICK_OVERFLOW` to the owner-neutral name, because a
  // hazard interval failing this same rule must not report a royale code.
  try {
    static_cast<void>(gameplay::RoyaleConfiguration::create(section_with_zone_shrink(1.0e16)));
    FAIL("an unrepresentable duration was accepted");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() == gameplay::GameplayValidationCode::kDurationTickOverflow);
    CHECK(error.code() == std::string_view{"GAMEPLAY.DURATION_TICK_OVERFLOW"});
    CHECK(error.context() == "royale.zone_shrink_seconds");
  }
}

TEST_CASE("every [royale] value must be finite and not negative",
          "[unit][gameplay][royale][configuration][validation]") {
  // The four duration keys are refused by the shared conversion and the rest by this section's own
  // rule, which is why the codes below differ by key rather than by section.
  CHECK(rejection_code_of(section_with_zone_shrink(std::numeric_limits<double>::quiet_NaN())) ==
        gameplay::GameplayValidationCode::kDurationNotFinite);
  CHECK(rejection_code_of(section_with_zone_shrink(std::numeric_limits<double>::infinity())) ==
        gameplay::GameplayValidationCode::kDurationNotFinite);
  CHECK(rejection_code_of(section_with_zone_shrink(-1.0)) ==
        gameplay::GameplayValidationCode::kDurationNegative);

  gameplay::RoyaleConfiguration::Section negative_radius =
      gameplay::RoyaleConfiguration::default_section();
  negative_radius.zone_minimum_radius_world_units = -1.0;
  CHECK(rejection_code_of(negative_radius) ==
        gameplay::GameplayValidationCode::kRoyaleScalarOutOfRange);

  // A lobby of no seats is a lobby nobody can sit in, so zero is a rejection rather than a mode
  // that holds every match in `lobby` forever.
  gameplay::RoyaleConfiguration::Section empty_lobby =
      gameplay::RoyaleConfiguration::default_section();
  empty_lobby.lobby_seat_count = 0;
  CHECK(rejection_code_of(empty_lobby) ==
        gameplay::GameplayValidationCode::kRoyaleScalarOutOfRange);

  // And the ceiling, which is the engine's: the roster is copied into the working world every tick,
  // so a lobby the tick cannot afford is refused at startup rather than allocated per tick.
  gameplay::RoyaleConfiguration::Section oversized_lobby =
      gameplay::RoyaleConfiguration::default_section();
  oversized_lobby.lobby_seat_count = simulation::SeatRoster::kMaximumSeatCount + 1;
  CHECK(rejection_code_of(oversized_lobby) ==
        gameplay::GameplayValidationCode::kRoyaleScalarOutOfRange);
}

TEST_CASE("a thrust maximum is validated through the steering system's own rule",
          "[unit][gameplay][royale][configuration][validation]") {
  // Not a second copy of the rule here: a mode cannot declare a thrust maximum the system it builds
  // would refuse, so the code is the steering system's rather than a `GAMEPLAY.ROYALE_*` one.
  gameplay::RoyaleConfiguration::Section negative_thrust =
      gameplay::RoyaleConfiguration::default_section();
  negative_thrust.thrust_max_world_units_per_second_squared = -1.0;
  CHECK(rejection_code_of(negative_thrust) ==
        gameplay::GameplayValidationCode::kThrustMaximumOutOfRange);

  gameplay::RoyaleConfiguration::Section infinite_thrust =
      gameplay::RoyaleConfiguration::default_section();
  infinite_thrust.thrust_max_world_units_per_second_squared =
      std::numeric_limits<double>::infinity();
  CHECK(rejection_code_of(infinite_thrust) ==
        gameplay::GameplayValidationCode::kThrustMaximumNotFinite);
}

TEST_CASE("a zero-duration configuration is accepted rather than rejected",
          "[unit][gameplay][royale][configuration]") {
  // Every accepted configuration has a defined result, including the degenerate all-zero one that
  // the engine's one-transition-per-tick bound keeps terminating.
  gameplay::RoyaleConfiguration::Section degenerate =
      gameplay::RoyaleConfiguration::default_section();
  degenerate.zone_shrink_seconds = 0.0;
  degenerate.elimination_grace_seconds = 0.0;
  degenerate.countdown_seconds = 0.0;
  degenerate.restart_delay_seconds = 0.0;
  degenerate.lobby_seat_count = 1;

  const gameplay::RoyaleConfiguration configuration =
      gameplay::RoyaleConfiguration::create(degenerate);
  CHECK(configuration.zone_shrink_ticks() == 0);
  CHECK(configuration.elimination_grace_ticks() == 0);
  CHECK(configuration.countdown_ticks() == 0);
  CHECK(configuration.restart_delay_ticks() == 0);
  CHECK(configuration.lobby_seat_count() == 1);
}
