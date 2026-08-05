#ifndef BLOB_ROYALE_SIMULATION_SIMULATION_TOLERANCE_HPP
#define BLOB_ROYALE_SIMULATION_SIMULATION_TOLERANCE_HPP

#include "simulation_limits.hpp"

#include <algorithm>
#include <cmath>

namespace blob_royale::simulation {

// canonical: simulation_tolerance -- the exact absolute-plus-relative comparison from ADR 0003.
[[nodiscard]] inline double comparison_tolerance(const double first, const double second,
                                                 const double absolute_tolerance) noexcept {
  return absolute_tolerance + (kRelativeTolerance * std::max(std::abs(first), std::abs(second)));
}

[[nodiscard]] inline bool approximately_equal(const double first, const double second,
                                              const double absolute_tolerance) noexcept {
  return std::abs(first - second) <= comparison_tolerance(first, second, absolute_tolerance);
}

[[nodiscard]] inline bool
less_than_or_approximately_equal(const double first, const double second,
                                 const double absolute_tolerance) noexcept {
  return first < second || approximately_equal(first, second, absolute_tolerance);
}

[[nodiscard]] inline bool
greater_than_or_approximately_equal(const double first, const double second,
                                    const double absolute_tolerance) noexcept {
  return first > second || approximately_equal(first, second, absolute_tolerance);
}

} // namespace blob_royale::simulation

#endif
