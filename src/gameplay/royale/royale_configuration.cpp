#include "royale/royale_configuration.hpp"

#include "gameplay_validation_error.hpp"
#include "seat_roster.hpp"
#include "shared/duration_ticks.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace blob_royale::gameplay {
namespace {

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

RoyaleConfiguration RoyaleConfiguration::create(const Section& section) {
  require_finite_and_not_negative(section.zone_minimum_radius_world_units,
                                  "zone_minimum_radius_world_units");
  // Converted into named locals in the order the section declares them rather than inside the
  // constructor call, because the order in which function arguments are evaluated is unspecified in
  // C++: a section with two bad durations would otherwise name whichever key the compiler happened
  // to reach first, and a rejection that names a different key on a different toolchain is a
  // diagnostic that cannot be reproduced from the message.
  //
  // Each call names the full `royale.<key>` context because `duration_ticks` is shared with every
  // other section that authors a duration and cannot know which one it is converting for
  // (`shared/duration_ticks.hpp`). The contexts are the ones this section has always reported.
  const std::uint64_t zone_shrink_ticks =
      duration_ticks(section.zone_shrink_seconds, "royale.zone_shrink_seconds");
  const std::uint64_t elimination_grace_ticks =
      duration_ticks(section.elimination_grace_seconds, "royale.elimination_grace_seconds");
  const std::uint64_t countdown_ticks =
      duration_ticks(section.countdown_seconds, "royale.countdown_seconds");
  const std::uint64_t restart_delay_ticks =
      duration_ticks(section.restart_delay_seconds, "royale.restart_delay_seconds");
  return RoyaleConfiguration(section.zone_minimum_radius_world_units, zone_shrink_ticks,
                             elimination_grace_ticks, countdown_ticks, restart_delay_ticks);
}

RoyaleConfiguration::Section RoyaleConfiguration::default_section() noexcept {
  return Section{kDefaultZoneMinimumRadiusWorldUnits, kDefaultZoneShrinkSeconds,
                 kDefaultEliminationGraceSeconds, kDefaultCountdownSeconds,
                 kDefaultRestartDelaySeconds};
}

RoyaleConfiguration RoyaleConfiguration::defaults() { return create(default_section()); }

RoyaleConfiguration::RoyaleConfiguration(const double zone_minimum_radius,
                                         const std::uint64_t zone_shrink_ticks,
                                         const std::uint64_t elimination_grace_ticks,
                                         const std::uint64_t countdown_ticks,
                                         const std::uint64_t restart_delay_ticks) noexcept
    : zone_minimum_radius_(zone_minimum_radius), zone_shrink_ticks_(zone_shrink_ticks),
      elimination_grace_ticks_(elimination_grace_ticks), countdown_ticks_(countdown_ticks),
      restart_delay_ticks_(restart_delay_ticks) {}

} // namespace blob_royale::gameplay
