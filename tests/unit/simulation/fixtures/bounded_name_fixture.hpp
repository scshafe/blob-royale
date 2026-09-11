#ifndef BLOB_ROYALE_TESTING_BOUNDED_NAME_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_BOUNDED_NAME_FIXTURE_HPP

#include "bounded_name_frozen_reference.hpp"
#include "seat_kind_name_policy.hpp"
#include "simulation_validation_error.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace blob_royale::testing::bounded_name_fixture {

// Named inputs are shared by the promoted types and the independent frozen reference.
// Byte-invalid names intentionally carry their full lengths, including the embedded NUL.
struct Input final {
  std::string_view scenario;
  std::string value;
  bool accepted;
};

inline constexpr std::string_view kShortName = "a";
inline constexpr std::string_view kDifferentName = "b";
inline constexpr std::string_view kOwnedName = "owned_name_2";

[[nodiscard]] inline std::string maximum_name() {
  return std::string(bounded_name_reference::kMaximumKindNameLength, 'a');
}

[[nodiscard]] inline std::vector<Input> inputs() {
  return {{"one lower-case letter", std::string(kShortName), true},
          {"mixed accepted suffix", "all_letters_az09__", true},
          {"maximum capacity", maximum_name(), true},
          {"empty authored value", "", false},
          {"uppercase first byte", "Abc", false},
          {"uppercase suffix", "aBc", false},
          {"digit first byte", "2abc", false},
          {"underscore first byte", "_abc", false},
          {"space suffix", "a b", false},
          {"line feed suffix", "a\nb", false},
          {"hyphen suffix", "a-b", false},
          {"embedded NUL", std::string("a\0b", 3), false},
          {"non-ASCII first byte",
           std::string("\xff"
                       "a",
                       2),
           false},
          {"non-ASCII suffix", std::string("a\x80", 2), false},
          {"UTF-8 suffix", std::string("a\xc3\xa9", 3), false},
          {"one beyond capacity", maximum_name() + "a", false}};
}

struct Rejection final {
  simulation::SimulationValidationCode validation_code;
  std::string code;
  std::string context;
  std::string detail;
  std::string message;

  friend bool operator==(const Rejection&, const Rejection&) = default;
};

// Literal diagnostic vocabulary, independent of either policy and of code-name dispatch. what()
// has always been a C string: detail retains an embedded NUL, while observing what() as a C string
// stops there. Compare both surfaces rather than discarding the full authored detail.
[[nodiscard]] inline Rejection seat_rejection(const std::string_view value) {
  const std::string detail = "seat controller kind " + std::string(value) +
                             " must be a non-empty snake_case identity within the published "
                             "kind-name length";
  const std::string message = "SIMULATION.SEAT_KIND_NAME_INVALID [seat.npc_kind]: " + detail;
  return {simulation::SimulationValidationCode::kSeatKindNameInvalid,
          "SIMULATION.SEAT_KIND_NAME_INVALID", "seat.npc_kind", detail,
          std::string(message.c_str())};
}

[[nodiscard]] inline Rejection contact_rejection(const std::string_view value) {
  const std::string detail = "contact rule name " + std::string(value) +
                             " must be a non-empty snake_case identity within the accepted length";
  const std::string message = "SIMULATION.CONTACT_RULE_NAME_INVALID [contact_rule.name]: " + detail;
  return {simulation::SimulationValidationCode::kContactRuleNameInvalid,
          "SIMULATION.CONTACT_RULE_NAME_INVALID", "contact_rule.name", detail,
          std::string(message.c_str())};
}

[[nodiscard]] inline Rejection road_rejection(const std::string_view value) {
  const std::string detail = "race road name " + std::string(value) +
                             " must be a non-empty snake_case identity within the published "
                             "kind-name length";
  const std::string message = "SIMULATION.RACE_ROAD_NAME_INVALID [race_mode_state.road]: " + detail;
  return {simulation::SimulationValidationCode::kRaceRoadNameInvalid,
          "SIMULATION.RACE_ROAD_NAME_INVALID", "race_mode_state.road", detail,
          std::string(message.c_str())};
}

// A test-only required-name policy proves the constructor variation before any new domain name
// is introduced. It delegates accepted characters/errors to a real candidate policy; it is not a
// road name, road diagnostic, or production declaration.
struct RequiredNamePolicy final {
  static constexpr std::size_t kCapacity = simulation::SeatKindNamePolicy::kCapacity;
  static constexpr bool kAllowEmptyDefault = false;

  static void validate(const std::string_view value) {
    simulation::SeatKindNamePolicy::validate(value);
  }
};

} // namespace blob_royale::testing::bounded_name_fixture

#endif
