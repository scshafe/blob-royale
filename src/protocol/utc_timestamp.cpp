#include "utc_timestamp.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace blob_royale::protocol {
namespace {

[[nodiscard]] bool is_ascii_digit(const char character) noexcept {
  return character >= '0' && character <= '9';
}

[[nodiscard]] unsigned parse_two_digits(const std::string_view value,
                                        const std::size_t offset) noexcept {
  return static_cast<unsigned>((value[offset] - '0') * 10 + (value[offset + 1] - '0'));
}

[[nodiscard]] unsigned parse_four_digits(const std::string_view value,
                                         const std::size_t offset) noexcept {
  return static_cast<unsigned>((value[offset] - '0') * 1'000 + (value[offset + 1] - '0') * 100 +
                               (value[offset + 2] - '0') * 10 + (value[offset + 3] - '0'));
}

[[nodiscard]] bool is_leap_year(const unsigned year) noexcept {
  return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

[[nodiscard]] unsigned days_in_month(const unsigned year, const unsigned month) noexcept {
  static constexpr std::array<unsigned, 12> kDaysByMonth{31, 28, 31, 30, 31, 30,
                                                         31, 31, 30, 31, 30, 31};
  if (month == 2U && is_leap_year(year)) {
    return 29U;
  }
  return kDaysByMonth[month - 1U];
}

} // namespace

bool is_accepted_utc_timestamp(const std::string_view value) noexcept {
  if (value.size() < 20 || value.size() > 32 || value.back() != 'Z') {
    return false;
  }
  constexpr std::array<std::size_t, 14> kDigitPositions{0, 1,  2,  3,  5,  6,  8,
                                                        9, 11, 12, 14, 15, 17, 18};
  for (const std::size_t position : kDigitPositions) {
    if (!is_ascii_digit(value[position])) {
      return false;
    }
  }
  if (value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' ||
      value[16] != ':') {
    return false;
  }
  if (value.size() > 20) {
    if (value.size() < 22 || value[19] != '.') {
      return false;
    }
    for (std::size_t position = 20; position + 1 < value.size(); ++position) {
      if (!is_ascii_digit(value[position])) {
        return false;
      }
    }
  } else if (value[19] != 'Z') {
    return false;
  }

  const unsigned year = parse_four_digits(value, 0);
  const unsigned month = parse_two_digits(value, 5);
  const unsigned day = parse_two_digits(value, 8);
  const unsigned hour = parse_two_digits(value, 11);
  const unsigned minute = parse_two_digits(value, 14);
  const unsigned second = parse_two_digits(value, 17);
  if (year == 0 || month == 0 || month > 12 || day == 0 || day > days_in_month(year, month) ||
      hour > 23 || minute > 59 || second > 60) {
    return false;
  }
  return true;
}

} // namespace blob_royale::protocol
