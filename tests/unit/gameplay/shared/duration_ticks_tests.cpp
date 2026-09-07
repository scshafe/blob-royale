#include "shared/duration_ticks.hpp"

#include "gameplay_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string_view>

namespace gameplay = blob_royale::gameplay;

namespace {

[[nodiscard]] gameplay::GameplayValidationCode rejection_code_of(const double seconds) {
  try {
    static_cast<void>(gameplay::duration_ticks(seconds, "royale.zone_shrink_seconds"));
  } catch (const gameplay::GameplayValidationError& error) {
    return error.validation_code();
  }
  FAIL("the duration was accepted");
  return gameplay::GameplayValidationCode::kGameModeNameUnknown;
}

} // namespace

TEST_CASE("a duration converts to the nearest whole tick", "[unit][gameplay][duration]") {
  CHECK(gameplay::duration_ticks(0.0, "royale.zone_shrink_seconds") == 0);
  CHECK(gameplay::duration_ticks(1.0, "royale.zone_shrink_seconds") == 400);
  // Below and above the half-tick boundary. An *exact* tie is unreachable through a configured key
  // at all, because `(k + 0.5) / 400` is never a binary64 value: 400 is not a power of two, so no
  // authored decimal lands exactly on a half tick. The rule is still written as ties away from zero
  // because the conversion is stated over real seconds, not over what a parser can represent.
  CHECK(gameplay::duration_ticks(0.001, "royale.elimination_grace_seconds") == 0);
  CHECK(gameplay::duration_ticks(0.002, "royale.elimination_grace_seconds") == 1);
  CHECK(gameplay::duration_ticks(0.0075, "royale.elimination_grace_seconds") == 3);
  CHECK(gameplay::duration_ticks(0.03, "royale.elimination_grace_seconds") == 12);
}

TEST_CASE("one conversion serves every section that authors a duration",
          "[unit][gameplay][duration]") {
  // The same seconds give the same ticks whichever section authored them, which is the whole point
  // of there being one conversion: a hazard interval and a zone shrink cannot drift apart by a tick
  // because there is no second implementation for them to drift against.
  CHECK(gameplay::duration_ticks(6.0, "hazard.comet.spawn_interval_seconds") ==
        gameplay::duration_ticks(6.0, "royale.countdown_seconds"));
  CHECK(gameplay::duration_ticks(6.0, "hazard.comet.spawn_interval_seconds") == 2'400);
}

TEST_CASE("a rejection names the full context its caller supplied",
          "[unit][gameplay][duration][validation]") {
  // The parameter is the whole context rather than a bare key, because this helper is shared and
  // cannot know whose duration it is holding. A caller that passed only `spawn_interval_seconds`
  // would produce a diagnostic no operator could locate in the file.
  try {
    static_cast<void>(gameplay::duration_ticks(1.0e16, "hazard.comet.spawn_interval_seconds"));
    FAIL("an unrepresentable duration was accepted");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() == gameplay::GameplayValidationCode::kDurationTickOverflow);
    CHECK(error.code() == std::string_view{"GAMEPLAY.DURATION_TICK_OVERFLOW"});
    CHECK(error.context() == "hazard.comet.spawn_interval_seconds");
  }
}

TEST_CASE("a duration must be finite and not negative", "[unit][gameplay][duration][validation]") {
  // The codes are owner-neutral: this rule is the same rule for every section, so naming one of
  // them in the code would send a reader of a hazard rejection to the `[royale]` section.
  CHECK(rejection_code_of(std::numeric_limits<double>::quiet_NaN()) ==
        gameplay::GameplayValidationCode::kDurationNotFinite);
  CHECK(rejection_code_of(std::numeric_limits<double>::infinity()) ==
        gameplay::GameplayValidationCode::kDurationNotFinite);
  CHECK(rejection_code_of(-std::numeric_limits<double>::infinity()) ==
        gameplay::GameplayValidationCode::kDurationNotFinite);
  CHECK(rejection_code_of(-1.0) == gameplay::GameplayValidationCode::kDurationNegative);
  // Negative zero is not a negative duration: it converts to zero ticks like positive zero, so a
  // configuration authored as `-0` is accepted rather than refused on a sign bit.
  CHECK(gameplay::duration_ticks(-0.0, "royale.zone_shrink_seconds") == 0);
}
