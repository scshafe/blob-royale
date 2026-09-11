#ifndef BLOB_ROYALE_TESTING_HILL_ROAM_CONFIGURATION_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_HILL_ROAM_CONFIGURATION_FIXTURE_HPP

#include "gameplay_validation_error.hpp"
#include "king_of_the_hill/king_of_the_hill_configuration.hpp"

#include <limits>
#include <string_view>
#include <vector>

namespace blob_royale::testing::hill_roam_configuration_fixture {

using Configuration = gameplay::KingOfTheHillConfiguration;
using Section = Configuration::Section;
using Policy = Configuration::HillMotionPolicy;
inline constexpr std::string_view kTour = "marker_tour";
inline constexpr std::string_view kRoam = "random_roam";
inline constexpr std::string_view kUnknown = "bounce";
inline constexpr auto kInvalidPolicy = static_cast<Policy>(255);
inline constexpr std::uint64_t kDefaultMinimumTicks = 140;
inline constexpr std::uint64_t kDefaultMaximumTicks = 480;
inline constexpr std::uint64_t kMaximumTicks = 1'440'000;
inline constexpr double kReversedRoundedMinimumSeconds = 0.0035;
inline constexpr double kReversedRoundedMaximumSeconds = 0.003;

struct InvalidScalar final {
  double Section::*member;
  double value;
  std::string_view key;
  gameplay::GameplayValidationCode code;
};

[[nodiscard]] inline std::vector<InvalidScalar> invalid_scalars() {
  using Code = gameplay::GameplayValidationCode;
  return {{&Section::hill_speed_minimum, 0.0, "hill_speed_minimum",
           Code::kKingOfTheHillScalarOutOfRange},
          {&Section::hill_speed_minimum, Configuration::kMinimumHillSpeed / 2.0,
           "hill_speed_minimum", Code::kKingOfTheHillScalarOutOfRange},
          {&Section::hill_speed_minimum, std::numeric_limits<double>::quiet_NaN(),
           "hill_speed_minimum", Code::kKingOfTheHillScalarNotFinite},
          {&Section::hill_speed_maximum, std::numeric_limits<double>::infinity(),
           "hill_speed_maximum", Code::kKingOfTheHillScalarNotFinite},
          {&Section::hill_speed_maximum, Configuration::kMaximumHillSpeed + 1.0,
           "hill_speed_maximum", Code::kKingOfTheHillScalarOutOfRange},
          {&Section::hill_speed_maximum, Configuration::kDefaultHillSpeedMinimum / 2.0,
           "hill_speed_maximum", Code::kKingOfTheHillScalarOutOfRange},
          {&Section::hill_retarget_minimum_seconds, 0.0, "hill_retarget_minimum_seconds",
           Code::kKingOfTheHillScalarOutOfRange},
          {&Section::hill_retarget_minimum_seconds, 0.001, "hill_retarget_minimum_seconds",
           Code::kKingOfTheHillScalarOutOfRange},
          {&Section::hill_retarget_maximum_seconds,
           Configuration::kDefaultHillRetargetMinimumSeconds / 2.0, "hill_retarget_maximum_seconds",
           Code::kKingOfTheHillScalarOutOfRange},
          {&Section::hill_retarget_maximum_seconds,
           Configuration::kMaximumHillRetargetSeconds + 1.0, "hill_retarget_maximum_seconds",
           Code::kKingOfTheHillScalarOutOfRange},
          {&Section::hill_retarget_maximum_seconds, std::numeric_limits<double>::quiet_NaN(),
           "hill_retarget_maximum_seconds", Code::kKingOfTheHillScalarNotFinite}};
}

} // namespace blob_royale::testing::hill_roam_configuration_fixture

#endif
