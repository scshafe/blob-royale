#include "royale/royale_configuration.hpp"

#include "gameplay_validation_error.hpp"
#include "shared/thrust_steering_system.hpp"
#include "simulation_limits.hpp"
#include "tick_sequence.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace blob_royale::gameplay {
namespace {

namespace simulation = blob_royale::simulation;

void require_finite_and_not_negative(const double value, const std::string_view configuration_key) {
  if (!std::isfinite(value)) {
    throw GameplayValidationError(GameplayValidationCode::kRoyaleScalarNotFinite,
                                  "royale." + std::string(configuration_key),
                                  "every [royale] value must be a finite number");
  }
  if (value < 0.0) {
    throw GameplayValidationError(GameplayValidationCode::kRoyaleScalarOutOfRange,
                                  "royale." + std::string(configuration_key),
                                  std::to_string(value) + " must be greater than or equal to zero");
  }
}

} // namespace

std::uint64_t duration_ticks(const double seconds, const std::string_view configuration_key) {
  require_finite_and_not_negative(seconds, configuration_key);
  // The written form of the ADR: the product, then the nearest integer with ties away from zero,
  // which is exactly what `std::round` computes. Both operations are exactly specified, so two
  // conforming toolchains derive the same tick count from the same authored duration.
  const double rounded =
      std::round(seconds * static_cast<double>(simulation::kSimulationTicksPerSecond));
  if (rounded > static_cast<double>(simulation::TickSequence::kMaximumValue)) {
    throw GameplayValidationError(GameplayValidationCode::kRoyaleDurationTickOverflow,
                                  "royale." + std::string(configuration_key),
                                  std::to_string(seconds) + " s is " + std::to_string(rounded) +
                                      " ticks, which does not fit the tick counter");
  }
  return static_cast<std::uint64_t>(rounded);
}

RoyaleConfiguration RoyaleConfiguration::create(const Section& section) {
  // Validated through the steering system's own named rule rather than a second copy of it here, so
  // a mode cannot declare a thrust maximum the system it builds would refuse.
  require_valid_thrust_maximum(section.thrust_max_world_units_per_second_squared);
  require_finite_and_not_negative(section.zone_minimum_radius_world_units,
                                  "zone_minimum_radius_world_units");
  if (section.lobby_minimum_players < 1) {
    throw GameplayValidationError(GameplayValidationCode::kRoyaleScalarOutOfRange,
                                  "royale.lobby_minimum_players",
                                  "a match needs at least one player to start, so "
                                  "lobby_minimum_players must be greater than or equal to one");
  }
  // Converted into named locals in the order the section declares them rather than inside the
  // constructor call, because the order in which function arguments are evaluated is unspecified in
  // C++: a section with two bad durations would otherwise name whichever key the compiler happened
  // to reach first, and a rejection that names a different key on a different toolchain is a
  // diagnostic that cannot be reproduced from the message.
  const std::uint64_t zone_shrink_ticks =
      duration_ticks(section.zone_shrink_seconds, "zone_shrink_seconds");
  const std::uint64_t elimination_grace_ticks =
      duration_ticks(section.elimination_grace_seconds, "elimination_grace_seconds");
  const std::uint64_t countdown_ticks =
      duration_ticks(section.countdown_seconds, "countdown_seconds");
  const std::uint64_t restart_delay_ticks =
      duration_ticks(section.restart_delay_seconds, "restart_delay_seconds");
  return RoyaleConfiguration(section.thrust_max_world_units_per_second_squared,
                             section.zone_minimum_radius_world_units, zone_shrink_ticks,
                             elimination_grace_ticks, section.lobby_minimum_players,
                             countdown_ticks, restart_delay_ticks);
}

RoyaleConfiguration::Section RoyaleConfiguration::default_section() noexcept {
  return Section{kDefaultThrustMaximumWorldUnitsPerSecondSquared,
                 kDefaultZoneMinimumRadiusWorldUnits,
                 kDefaultZoneShrinkSeconds,
                 kDefaultEliminationGraceSeconds,
                 kDefaultLobbyMinimumPlayers,
                 kDefaultCountdownSeconds,
                 kDefaultRestartDelaySeconds};
}

RoyaleConfiguration RoyaleConfiguration::defaults() { return create(default_section()); }

RoyaleConfiguration::RoyaleConfiguration(const double thrust_maximum,
                                         const double zone_minimum_radius,
                                         const std::uint64_t zone_shrink_ticks,
                                         const std::uint64_t elimination_grace_ticks,
                                         const std::uint64_t lobby_minimum_players,
                                         const std::uint64_t countdown_ticks,
                                         const std::uint64_t restart_delay_ticks) noexcept
    : thrust_maximum_(thrust_maximum), zone_minimum_radius_(zone_minimum_radius),
      zone_shrink_ticks_(zone_shrink_ticks), elimination_grace_ticks_(elimination_grace_ticks),
      lobby_minimum_players_(lobby_minimum_players), countdown_ticks_(countdown_ticks),
      restart_delay_ticks_(restart_delay_ticks) {}

} // namespace blob_royale::gameplay
