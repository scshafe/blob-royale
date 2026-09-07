#include "shared/duration_ticks.hpp"

#include "gameplay_validation_error.hpp"
#include "simulation_limits.hpp"
#include "tick_sequence.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace blob_royale::gameplay {
namespace {

namespace simulation = blob_royale::simulation;

} // namespace

std::uint64_t duration_ticks(const double seconds, const std::string_view configuration_context) {
  if (!std::isfinite(seconds)) {
    throw GameplayValidationError(GameplayValidationCode::kDurationNotFinite,
                                  std::string(configuration_context),
                                  "a configured duration must be a finite number of seconds");
  }
  if (seconds < 0.0) {
    throw GameplayValidationError(
        GameplayValidationCode::kDurationNegative, std::string(configuration_context),
        std::to_string(seconds) + " s must be greater than or equal to zero");
  }
  // The written form of the ADR: the product, then the nearest integer with ties away from zero,
  // which is exactly what `std::round` computes. Both operations are exactly specified, so two
  // conforming toolchains derive the same tick count from the same authored duration.
  const double rounded =
      std::round(seconds * static_cast<double>(simulation::kSimulationTicksPerSecond));
  if (rounded > static_cast<double>(simulation::TickSequence::kMaximumValue)) {
    throw GameplayValidationError(GameplayValidationCode::kDurationTickOverflow,
                                  std::string(configuration_context),
                                  std::to_string(seconds) + " s is " + std::to_string(rounded) +
                                      " ticks, which does not fit the tick counter");
  }
  return static_cast<std::uint64_t>(rounded);
}

} // namespace blob_royale::gameplay
