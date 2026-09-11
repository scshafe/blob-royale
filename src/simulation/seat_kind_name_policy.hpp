#ifndef BLOB_ROYALE_SIMULATION_SEAT_KIND_NAME_POLICY_HPP
#define BLOB_ROYALE_SIMULATION_SEAT_KIND_NAME_POLICY_HPP

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "snake_case_identity.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace blob_royale::simulation {

// canonical: seat_kind_name_policy -- wire kind validation for bounded name storage.
// The empty default is the NpcSeat sentinel, not an accepted controller kind. Registry membership
// remains the caller's responsibility. The validator preserves the seat domain's original grammar
// and diagnostics independently of shared storage.
// related: bounded_name.hpp -- the shared owned value implementation.
struct SeatKindNamePolicy final {
  static constexpr std::size_t kCapacity = kMaximumKindNameLength;
  static constexpr bool kAllowEmptyDefault = true;

  static void validate(const std::string_view value) {
    if (!is_wire_kind_name(value)) {
      throw SimulationValidationError(
          SimulationValidationCode::kSeatKindNameInvalid, "seat.npc_kind",
          "seat controller kind " + std::string(value) +
              " must be a non-empty snake_case identity within the published kind-name length");
    }
  }
};

} // namespace blob_royale::simulation

#endif
