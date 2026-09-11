#ifndef BLOB_ROYALE_TESTING_MOVEMENT_TUNING_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_MOVEMENT_TUNING_FIXTURE_HPP

#include "simulation_validation_error.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <string_view>

namespace blob_royale::testing::movement_tuning_fixture {

struct ValidPair final {
  double acceleration;
  double normal_top_speed;
};

inline constexpr std::array kValidPairs{ValidPair{0.0, 1.0}, ValidPair{-0.0, 600.0},
                                        ValidPair{400.0, 600.0}, ValidPair{4'000.0, 10'000.0},
                                        ValidPair{10'000.0, 10'000.0}};

struct RejectedPair final {
  std::string_view name;
  double acceleration;
  double normal_top_speed;
  simulation::SimulationValidationCode code;
  std::string_view context;
};

[[nodiscard]] inline auto rejected_pairs() {
  constexpr auto not_finite = simulation::SimulationValidationCode::kMovementTuningNotFinite;
  constexpr auto out_of_range = simulation::SimulationValidationCode::kMovementTuningOutOfRange;
  constexpr std::string_view acceleration = "movement.acceleration_world_units_per_second_squared";
  constexpr std::string_view speed = "movement.normal_top_speed_world_units_per_second";
  constexpr double infinity = std::numeric_limits<double>::infinity();
  constexpr double nan = std::numeric_limits<double>::quiet_NaN();
  return std::array{
      RejectedPair{"acceleration_nan", nan, 600.0, not_finite, acceleration},
      RejectedPair{"acceleration_infinite", infinity, 600.0, not_finite, acceleration},
      RejectedPair{"acceleration_negative_infinite", -infinity, 600.0, not_finite, acceleration},
      RejectedPair{"acceleration_below_zero", std::nextafter(0.0, -infinity), 600.0, out_of_range,
                   acceleration},
      RejectedPair{"acceleration_above_maximum", std::nextafter(10'000.0, infinity), 600.0,
                   out_of_range, acceleration},
      RejectedPair{"speed_nan", 400.0, nan, not_finite, speed},
      RejectedPair{"speed_infinite", 400.0, infinity, not_finite, speed},
      RejectedPair{"speed_negative_infinite", 400.0, -infinity, not_finite, speed},
      RejectedPair{"speed_zero", 400.0, 0.0, out_of_range, speed},
      RejectedPair{"speed_negative_zero", 400.0, -0.0, out_of_range, speed},
      RejectedPair{"speed_below_minimum", 400.0, std::nextafter(1.0, 0.0), out_of_range, speed},
      RejectedPair{"speed_above_maximum", 400.0, std::nextafter(10'000.0, infinity), out_of_range,
                   speed}};
}

} // namespace blob_royale::testing::movement_tuning_fixture

#endif
