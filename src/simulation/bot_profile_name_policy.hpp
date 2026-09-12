#ifndef BLOB_ROYALE_SIMULATION_BOT_PROFILE_NAME_POLICY_HPP
#define BLOB_ROYALE_SIMULATION_BOT_PROFILE_NAME_POLICY_HPP

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "snake_case_identity.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace blob_royale::simulation {

struct BotProfileNamePolicy final {
  static constexpr std::size_t kCapacity = kMaximumKindNameLength;
  static constexpr bool kAllowEmptyDefault = false;

  static void validate(const std::string_view value) {
    if (!is_wire_kind_name(value)) {
      throw SimulationValidationError(
          SimulationValidationCode::kBotProfileNameInvalid, "npc.profile_name",
          "profile name " + std::string(value) +
              " must be a non-empty lower snake case name of at most 64 bytes");
    }
  }
};

} // namespace blob_royale::simulation

#endif
