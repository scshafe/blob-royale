#include "race/race_configuration.hpp"

#include "gameplay_validation_error.hpp"
#include "shared/duration_ticks.hpp"
#include "shared/thrust_steering_system.hpp"
#include "simulation_limits.hpp"
#include "snake_case_identity.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace blob_royale::gameplay {
namespace {

[[nodiscard]] std::string context_of(const std::string_view key) {
  return "race." + std::string(key);
}

void require_finite_and_positive(const double value, const std::string_view key) {
  if (!std::isfinite(value)) {
    throw GameplayValidationError(GameplayValidationCode::kRaceScalarNotFinite, context_of(key),
                                  "every numeric [race] value must be a finite number");
  }
  if (value <= 0.0) {
    throw GameplayValidationError(GameplayValidationCode::kRaceScalarOutOfRange, context_of(key),
                                  std::to_string(value) + " must be strictly greater than zero");
  }
}

} // namespace

RaceConfiguration RaceConfiguration::create(const Section& section) {
  require_valid_thrust_maximum(section.thrust_max_world_units_per_second_squared);
  if (!simulation::is_wire_kind_name(section.road)) {
    throw GameplayValidationError(GameplayValidationCode::kRaceRoadNameInvalid, context_of("road"),
                                  "road must be a snake_case terrain corridor identity within "
                                  "the accepted kind-name length");
  }
  // Gate radius remains published on every frame; the bound road supplies its width separately.
  require_finite_and_positive(section.checkpoint_radius_world_units,
                              "checkpoint_radius_world_units");
  if (section.checkpoint_radius_world_units > simulation::kMaximumPhysicalComponentMagnitude) {
    throw GameplayValidationError(
        GameplayValidationCode::kRaceScalarOutOfRange, context_of("checkpoint_radius_world_units"),
        "checkpoint_radius_world_units exceeds the published world scalar bound");
  }
  // Named locals preserve validation order across compilers; argument evaluation order would not.
  const std::uint64_t respawn_delay_ticks =
      duration_ticks(section.respawn_delay_seconds, context_of("respawn_delay_seconds"));
  const std::uint64_t finish_window_ticks =
      duration_ticks(section.finish_window_seconds, context_of("finish_window_seconds"));
  require_finite_and_positive(section.time_limit_seconds, "time_limit_seconds");
  const std::uint64_t time_limit_ticks =
      duration_ticks(section.time_limit_seconds, context_of("time_limit_seconds"));
  const std::uint64_t countdown_ticks =
      duration_ticks(section.countdown_seconds, context_of("countdown_seconds"));
  const std::uint64_t restart_delay_ticks =
      duration_ticks(section.restart_delay_seconds, context_of("restart_delay_seconds"));
  return RaceConfiguration(section.thrust_max_world_units_per_second_squared, section.road,
                           section.checkpoint_radius_world_units, respawn_delay_ticks,
                           finish_window_ticks, time_limit_ticks, countdown_ticks,
                           restart_delay_ticks);
}

RaceConfiguration::Section RaceConfiguration::default_section() {
  return Section{kDefaultThrustMaximumWorldUnitsPerSecondSquared,
                 std::string{kDefaultRoad},
                 kDefaultCheckpointRadiusWorldUnits,
                 kDefaultRespawnDelaySeconds,
                 kDefaultFinishWindowSeconds,
                 kDefaultTimeLimitSeconds,
                 kDefaultCountdownSeconds,
                 kDefaultRestartDelaySeconds};
}

RaceConfiguration RaceConfiguration::defaults() { return create(default_section()); }

RaceConfiguration::RaceConfiguration(const double thrust_maximum, std::string road,
                                     const double checkpoint_radius,
                                     const std::uint64_t respawn_delay_ticks,
                                     const std::uint64_t finish_window_ticks,
                                     const std::uint64_t time_limit_ticks,
                                     const std::uint64_t countdown_ticks,
                                     const std::uint64_t restart_delay_ticks)
    : thrust_maximum_(thrust_maximum), road_(std::move(road)),
      checkpoint_radius_(checkpoint_radius), respawn_delay_ticks_(respawn_delay_ticks),
      finish_window_ticks_(finish_window_ticks), time_limit_ticks_(time_limit_ticks),
      countdown_ticks_(countdown_ticks), restart_delay_ticks_(restart_delay_ticks) {}

} // namespace blob_royale::gameplay
