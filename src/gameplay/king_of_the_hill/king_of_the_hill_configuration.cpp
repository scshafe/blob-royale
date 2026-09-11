#include "king_of_the_hill/king_of_the_hill_configuration.hpp"

#include "gameplay_validation_error.hpp"
#include "shared/duration_ticks.hpp"
#include "simulation_limits.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace blob_royale::gameplay {
namespace {

constexpr std::string_view kSectionName = "king_of_the_hill";

[[nodiscard]] std::string context_of(const std::string_view key) {
  return std::string(kSectionName) + "." + std::string(key);
}

void require_finite(const double value, const std::string_view key) {
  if (!std::isfinite(value)) {
    throw GameplayValidationError(GameplayValidationCode::kKingOfTheHillScalarNotFinite,
                                  context_of(key),
                                  "every [king_of_the_hill] value must be a finite number");
  }
}

void require_finite_and_positive(const double value, const std::string_view key) {
  require_finite(value, key);
  if (value <= 0.0) {
    throw GameplayValidationError(GameplayValidationCode::kKingOfTheHillScalarOutOfRange,
                                  context_of(key),
                                  std::to_string(value) + " must be strictly greater than zero");
  }
}

} // namespace

KingOfTheHillConfiguration KingOfTheHillConfiguration::create(const Section& section) {
  require_finite_and_positive(section.hill_radius_world_units, "hill_radius_world_units");
  // The hill component and rules block must remain encodable for every accepted configuration.
  if (section.hill_radius_world_units > simulation::kMaximumPhysicalComponentMagnitude) {
    throw GameplayValidationError(
        GameplayValidationCode::kKingOfTheHillScalarOutOfRange,
        context_of("hill_radius_world_units"),
        "hill_radius_world_units exceeds the published world scalar bound");
  }
  // Converted into named locals in the order the section declares them rather than inside the
  // constructor call, because the order in which function arguments are evaluated is unspecified in
  // C++: a section with two bad durations would otherwise name whichever key the compiler happened
  // to reach first. Each call names the full context because `duration_ticks` is shared with every
  // other section that authors a duration (`shared/duration_ticks.hpp`).
  const std::uint64_t hill_dwell_ticks =
      duration_ticks(section.hill_dwell_seconds, context_of("hill_dwell_seconds"));
  const std::uint64_t hill_travel_ticks =
      duration_ticks(section.hill_travel_seconds, context_of("hill_travel_seconds"));
  if (hill_dwell_ticks + hill_travel_ticks == 0) {
    throw GameplayValidationError(
        GameplayValidationCode::kKingOfTheHillTourWithoutDuration,
        context_of("hill_travel_seconds"),
        "hill_dwell_seconds plus hill_travel_seconds must convert to at least one tick, or the "
        "hill would have no position to hold at any stop of its tour");
  }
  const std::uint64_t point_interval_ticks =
      duration_ticks(section.point_interval_seconds, context_of("point_interval_seconds"));
  if (section.points_to_win == 0) {
    throw GameplayValidationError(GameplayValidationCode::kKingOfTheHillPointsToWinZero,
                                  context_of("points_to_win"),
                                  "points_to_win must be at least one, or the match is decided "
                                  "before anyone has held the hill");
  }
  if (section.points_to_win > simulation::kMaximumProtocolSafeInteger) {
    throw GameplayValidationError(GameplayValidationCode::kKingOfTheHillScalarOutOfRange,
                                  context_of("points_to_win"),
                                  "points_to_win exceeds the published safe integer bound");
  }
  require_finite_and_positive(section.time_limit_seconds, "time_limit_seconds");
  const std::uint64_t time_limit_ticks =
      duration_ticks(section.time_limit_seconds, context_of("time_limit_seconds"));
  const std::uint64_t respawn_delay_ticks =
      duration_ticks(section.respawn_delay_seconds, context_of("respawn_delay_seconds"));
  const std::uint64_t countdown_ticks =
      duration_ticks(section.countdown_seconds, context_of("countdown_seconds"));
  const std::uint64_t restart_delay_ticks =
      duration_ticks(section.restart_delay_seconds, context_of("restart_delay_seconds"));
  return KingOfTheHillConfiguration(section.hill_radius_world_units, hill_dwell_ticks,
                                    hill_travel_ticks, point_interval_ticks, section.points_to_win,
                                    section.contested_hill_scores, time_limit_ticks,
                                    respawn_delay_ticks, countdown_ticks, restart_delay_ticks);
}

KingOfTheHillConfiguration::Section KingOfTheHillConfiguration::default_section() noexcept {
  return Section{kDefaultHillRadiusWorldUnits, kDefaultHillDwellSeconds,
                 kDefaultHillTravelSeconds,    kDefaultPointIntervalSeconds,
                 kDefaultPointsToWin,          kDefaultContestedHillScores,
                 kDefaultTimeLimitSeconds,     kDefaultRespawnDelaySeconds,
                 kDefaultCountdownSeconds,     kDefaultRestartDelaySeconds};
}

KingOfTheHillConfiguration KingOfTheHillConfiguration::defaults() {
  return create(default_section());
}

KingOfTheHillConfiguration::KingOfTheHillConfiguration(
    const double hill_radius, const std::uint64_t hill_dwell_ticks,
    const std::uint64_t hill_travel_ticks, const std::uint64_t point_interval_ticks,
    const std::uint64_t points_to_win, const bool contested_hill_scores,
    const std::uint64_t time_limit_ticks, const std::uint64_t respawn_delay_ticks,
    const std::uint64_t countdown_ticks, const std::uint64_t restart_delay_ticks) noexcept
    : hill_radius_(hill_radius), hill_dwell_ticks_(hill_dwell_ticks),
      hill_travel_ticks_(hill_travel_ticks), point_interval_ticks_(point_interval_ticks),
      points_to_win_(points_to_win), contested_hill_scores_(contested_hill_scores),
      time_limit_ticks_(time_limit_ticks), respawn_delay_ticks_(respawn_delay_ticks),
      countdown_ticks_(countdown_ticks), restart_delay_ticks_(restart_delay_ticks) {}

} // namespace blob_royale::gameplay
