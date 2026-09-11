#ifndef BLOB_ROYALE_TESTS_UNIT_APPLICATION_FIXTURES_HILL_MOTION_CONFIGURATION_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_APPLICATION_FIXTURES_HILL_MOTION_CONFIGURATION_FIXTURE_HPP

#include "../application_input_test_fixture.hpp"

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace blob_royale::application::hill_motion_fixture {

struct Replacement final {
  std::string_view field;
  std::string_view replacement;
  std::string_view context;
};

inline constexpr std::string_view kMarkerTourField = "hill_motion=marker_tour";
inline constexpr Replacement kRandomRoam{kMarkerTourField, "hill_motion=random_roam",
                                         "king_of_the_hill.hill_motion"};
inline constexpr Replacement kEmptyPolicy{kMarkerTourField,
                                          "hill_motion=", "king_of_the_hill.hill_motion"};
inline constexpr Replacement kUnknownPolicy{kMarkerTourField, "hill_motion=unregistered",
                                            "king_of_the_hill.hill_motion"};
inline constexpr Replacement kMalformedSpeed{"hill_speed_minimum=20", "hill_speed_minimum=20oops",
                                             "king_of_the_hill.hill_speed_minimum"};
inline constexpr Replacement kNonFiniteSpeed{"hill_speed_minimum=20", "hill_speed_minimum=nan",
                                             "king_of_the_hill.hill_speed_minimum"};
inline constexpr std::array kRequiredFields{
    "hill_motion=marker_tour\n", "hill_speed_minimum=20\n", "hill_speed_maximum=70\n",
    "hill_retarget_minimum_seconds=0.35\n", "hill_retarget_maximum_seconds=1.2\n"};
inline constexpr std::array kInvalidPolicies{"random", "Random_roam", "marker-tour"};
inline constexpr std::array kInvalidRanges{
    Replacement{"hill_speed_minimum=20", "hill_speed_minimum=0",
                "king_of_the_hill.hill_speed_minimum"},
    Replacement{"hill_speed_maximum=70", "hill_speed_maximum=1000001",
                "king_of_the_hill.hill_speed_maximum"},
    Replacement{"hill_speed_maximum=70", "hill_speed_maximum=19",
                "king_of_the_hill.hill_speed_maximum"},
    Replacement{"hill_retarget_minimum_seconds=0.35", "hill_retarget_minimum_seconds=0",
                "king_of_the_hill.hill_retarget_minimum_seconds"},
    Replacement{"hill_retarget_maximum_seconds=1.2", "hill_retarget_maximum_seconds=0.1",
                "king_of_the_hill.hill_retarget_maximum_seconds"},
    Replacement{"hill_retarget_maximum_seconds=1.2", "hill_retarget_maximum_seconds=3601",
                "king_of_the_hill.hill_retarget_maximum_seconds"}};

[[nodiscard]] inline std::string configuration_with(const Replacement input) {
  return test_fixture::replace_once(std::string{test_fixture::kValidConfiguration}, input.field,
                                    input.replacement);
}

[[nodiscard]] inline std::string policy_configuration(const std::string_view policy) {
  return test_fixture::replace_once(std::string{test_fixture::kValidConfiguration},
                                    kMarkerTourField, "hill_motion=" + std::string{policy});
}

[[nodiscard]] inline std::string ordered_invalid_configuration() {
  return test_fixture::replace_once(configuration_with(kUnknownPolicy), kMalformedSpeed.field,
                                    kMalformedSpeed.replacement);
}

} // namespace blob_royale::application::hill_motion_fixture

#endif
