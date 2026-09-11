#ifndef BLOB_ROYALE_SIMULATION_RACE_ROAD_NAME_POLICY_HPP
#define BLOB_ROYALE_SIMULATION_RACE_ROAD_NAME_POLICY_HPP

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "snake_case_identity.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace blob_royale::simulation {

// canonical: race_road_name_policy -- required race-road identity validation for bounded storage.
// A race block must name its selected corridor; no empty default can stand in for that binding.
// Grammar and capacity match terrain's published names. Existence in the actual terrain is checked
// by race initialization and consuming boundaries, not by this context-free name factory.
// related: bounded_name.hpp -- the shared owned value implementation.
struct RaceRoadNamePolicy final {
  static constexpr std::size_t kCapacity = kMaximumKindNameLength;
  static constexpr bool kAllowEmptyDefault = false;

  static void validate(const std::string_view value) {
    if (!is_wire_kind_name(value)) {
      throw SimulationValidationError(
          SimulationValidationCode::kRaceRoadNameInvalid, "race_mode_state.road",
          "race road name " + std::string(value) +
              " must be a non-empty snake_case identity within the published kind-name length");
    }
  }
};

} // namespace blob_royale::simulation

#endif
