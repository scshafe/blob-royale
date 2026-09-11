#ifndef BLOB_ROYALE_SIMULATION_CONTACT_RULE_NAME_POLICY_HPP
#define BLOB_ROYALE_SIMULATION_CONTACT_RULE_NAME_POLICY_HPP

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "snake_case_identity.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace blob_royale::simulation {

// canonical: contact_rule_name_policy -- contact identity validation for bounded name storage.
// The empty default is the ContactEvent sentinel, not an accepted authored name. The validator
// preserves the contact domain's original grammar and diagnostics independently of shared storage.
// related: bounded_name.hpp -- the shared owned value implementation.
struct ContactRuleNamePolicy final {
  static constexpr std::size_t kCapacity = kMaximumContactRuleNameLength;
  static constexpr bool kAllowEmptyDefault = true;

  static void validate(const std::string_view value) {
    if (value.empty() || value.size() > kCapacity || !is_snake_case_identity(value)) {
      throw SimulationValidationError(
          SimulationValidationCode::kContactRuleNameInvalid, "contact_rule.name",
          "contact rule name " + std::string(value) +
              " must be a non-empty snake_case identity within the accepted length");
    }
  }
};

} // namespace blob_royale::simulation

#endif
