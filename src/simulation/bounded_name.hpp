#ifndef BLOB_ROYALE_SIMULATION_BOUNDED_NAME_HPP
#define BLOB_ROYALE_SIMULATION_BOUNDED_NAME_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace blob_royale::simulation {

// canonical: bounded_name -- owned fixed-capacity name storage and value semantics.
//
// @extension-point: one distinct policy per domain name supplies kCapacity, kAllowEmptyDefault,
// and validate(string_view). Validation must reject every empty, oversized, or otherwise invalid
// input with that domain's error before storage is constructed. Policies own grammar and errors;
// this value owns storage, construction, views, and comparisons. Different policies remain
// different types even when they accept the same characters.
//
// Empty defaults exist only for domains whose containing aggregates require an unnamed sentinel.
// A required identity disables that constructor without changing the validated factory. Fixed
// arrays keep copied worlds and emitted events allocation-free, preserve moved-from names, and
// zero-fill the unused tail so defaulted equality never depends on earlier, longer names.
// related: contact_rule_name_policy.hpp -- contact-row validation and the empty event sentinel.
// related: seat_kind_name_policy.hpp -- wire kind validation and the empty seat sentinel.
// related: race_road_name_policy.hpp -- required road identity without an empty default.
template <typename Policy> class BoundedName final {
public:
  static constexpr std::size_t kCapacity = Policy::kCapacity;

  [[nodiscard]] static BoundedName create(const std::string_view value) {
    Policy::validate(value);
    return BoundedName(value);
  }

  BoundedName()
    requires(Policy::kAllowEmptyDefault)
  = default;
  BoundedName(const BoundedName&) = default;
  BoundedName(BoundedName&&) noexcept = default;
  BoundedName& operator=(const BoundedName&) = default;
  BoundedName& operator=(BoundedName&&) noexcept = default;
  ~BoundedName() = default;

  [[nodiscard]] std::string_view value() const& noexcept {
    return std::string_view(characters_.data(), length_);
  }
  [[nodiscard]] std::string_view value() const&& = delete;

  [[nodiscard]] bool empty() const noexcept { return length_ == 0; }

  [[nodiscard]] friend bool operator==(const BoundedName& left, const std::string_view right) {
    return left.value() == right;
  }

  friend bool operator==(const BoundedName&, const BoundedName&) = default;

private:
  // The factory is the only caller and has already applied the policy's capacity check.
  explicit BoundedName(const std::string_view value) noexcept : length_(value.size()) {
    std::copy(value.cbegin(), value.cend(), characters_.begin());
  }

  std::array<char, kCapacity> characters_{};
  std::size_t length_{};
};

} // namespace blob_royale::simulation

#endif
