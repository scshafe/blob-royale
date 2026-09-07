#ifndef BLOB_ROYALE_SIMULATION_CONTACT_RULE_NAME_HPP
#define BLOB_ROYALE_SIMULATION_CONTACT_RULE_NAME_HPP

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace blob_royale::simulation {

// canonical: contact_rule_name -- the owned, bounded identity of one contact rule row.
//
// This is a **value, not a view**. A ContactEvent carries the name of the row that produced it and
// lives in the tick's event list, while the row itself is owned by a mode's table; a borrowed
// `std::string_view` would therefore be correct only as long as every rule name outlived every
// match, which a mode that builds a row from a temporary `std::string` silently breaks. A dangling
// name surfaces as garbage in a diagnostic rather than as a failure, so the name is copied into
// fixed capacity instead.
//
// Fixed capacity rather than `std::string` because the event list is bounded, tick-local, and
// cleared at commit: an owning allocation per emitted contact would put an allocator on the
// kernel's hot path for a value that is at most kMaximumContactRuleNameLength characters.
//
// The unused tail is zero-filled so the defaulted comparison is a comparison of names rather than
// of whatever the storage happened to hold.
class ContactRuleName final {
public:
  static constexpr std::size_t kCapacity = kMaximumContactRuleNameLength;

  // Creates a validated name or throws SimulationValidationError. A name must be a non-empty
  // snake_case identity within the capacity, which is the same vocabulary map marker kinds and
  // system names use.
  [[nodiscard]] static ContactRuleName create(const std::string_view value) {
    if (value.empty() || value.size() > kCapacity || !is_snake_case_identity(value)) {
      throw SimulationValidationError(
          SimulationValidationCode::kContactRuleNameInvalid, "contact_rule.name",
          "contact rule name " + std::string(value) +
              " must be a non-empty snake_case identity within the accepted length");
    }

    ContactRuleName name;
    std::copy(value.cbegin(), value.cend(), name.characters_.begin());
    name.length_ = value.size();
    return name;
  }

  // The unnamed row, which no validated rule can hold. It exists so a ContactEvent is an aggregate
  // with a defined default, and it compares unequal to every real name.
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
  [[nodiscard]] static constexpr bool
  is_snake_case_identity(const std::string_view value) noexcept {
    if (value.front() < 'a' || value.front() > 'z') {
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

  std::array<char, kCapacity> characters_{};
  std::size_t length_{};
};

} // namespace blob_royale::simulation

#endif
