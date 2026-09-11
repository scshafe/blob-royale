#ifndef BLOB_ROYALE_TESTING_BOUNDED_NAME_FROZEN_REFERENCE_HPP
#define BLOB_ROYALE_TESTING_BOUNDED_NAME_FROZEN_REFERENCE_HPP

#include "simulation_validation_error.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace blob_royale::testing::bounded_name_reference {

// Frozen from f837061: ContactRuleName, SeatKindName, their grammar, and both capacity values.
// Keep independent of bounded_name.hpp, the new policies, production identity predicates, and
// production capacity constants. These remain the old behavior oracle after reader delegation;
// production-vs-production equality alone must never become the promotion proof.
inline constexpr std::size_t kMaximumContactRuleNameLength = 64;
inline constexpr std::size_t kMaximumKindNameLength = 64;

[[nodiscard]] constexpr bool is_snake_case_identity(const std::string_view value) noexcept {
  if (value.empty() || value.front() < 'a' || value.front() > 'z') {
    return false;
  }
  for (const char character : value) {
    const bool accepted = (character >= 'a' && character <= 'z') ||
                          (character >= '0' && character <= '9') || character == '_';
    if (!accepted) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] constexpr bool is_wire_kind_name(const std::string_view value) noexcept {
  return value.size() <= kMaximumKindNameLength && is_snake_case_identity(value);
}

class ContactRuleName final {
public:
  static constexpr std::size_t kCapacity = kMaximumContactRuleNameLength;

  [[nodiscard]] static ContactRuleName create(const std::string_view value) {
    if (value.empty() || value.size() > kCapacity || !is_snake_case_identity(value)) {
      throw simulation::SimulationValidationError(
          simulation::SimulationValidationCode::kContactRuleNameInvalid, "contact_rule.name",
          "contact rule name " + std::string(value) +
              " must be a non-empty snake_case identity within the accepted length");
    }

    ContactRuleName name;
    std::copy(value.cbegin(), value.cend(), name.characters_.begin());
    name.length_ = value.size();
    return name;
  }

  ContactRuleName() = default;
  ContactRuleName(const ContactRuleName&) = default;
  ContactRuleName(ContactRuleName&&) noexcept = default;
  ContactRuleName& operator=(const ContactRuleName&) = default;
  ContactRuleName& operator=(ContactRuleName&&) noexcept = default;
  ~ContactRuleName() = default;

  [[nodiscard]] std::string_view value() const& noexcept {
    return std::string_view(characters_.data(), length_);
  }
  [[nodiscard]] std::string_view value() const&& = delete;

  [[nodiscard]] bool empty() const noexcept { return length_ == 0; }

  [[nodiscard]] friend bool operator==(const ContactRuleName& left, const std::string_view right) {
    return left.value() == right;
  }

  friend bool operator==(const ContactRuleName&, const ContactRuleName&) = default;

private:
  std::array<char, kCapacity> characters_{};
  std::size_t length_{};
};

class SeatKindName final {
public:
  static constexpr std::size_t kCapacity = kMaximumKindNameLength;

  [[nodiscard]] static SeatKindName create(const std::string_view value) {
    if (!is_wire_kind_name(value)) {
      throw simulation::SimulationValidationError(
          simulation::SimulationValidationCode::kSeatKindNameInvalid, "seat.npc_kind",
          "seat controller kind " + std::string(value) +
              " must be a non-empty snake_case identity within the published kind-name length");
    }

    SeatKindName name;
    std::copy(value.cbegin(), value.cend(), name.characters_.begin());
    name.length_ = value.size();
    return name;
  }

  SeatKindName() = default;
  SeatKindName(const SeatKindName&) = default;
  SeatKindName(SeatKindName&&) noexcept = default;
  SeatKindName& operator=(const SeatKindName&) = default;
  SeatKindName& operator=(SeatKindName&&) noexcept = default;
  ~SeatKindName() = default;

  [[nodiscard]] std::string_view value() const& noexcept {
    return std::string_view(characters_.data(), length_);
  }
  [[nodiscard]] std::string_view value() const&& = delete;

  [[nodiscard]] bool empty() const noexcept { return length_ == 0; }

  [[nodiscard]] friend bool operator==(const SeatKindName& left, const std::string_view right) {
    return left.value() == right;
  }

  friend bool operator==(const SeatKindName&, const SeatKindName&) = default;

private:
  std::array<char, kCapacity> characters_{};
  std::size_t length_{};
};

} // namespace blob_royale::testing::bounded_name_reference

#endif
