#ifndef BLOB_ROYALE_SIMULATION_FIXED_DELTA_HPP
#define BLOB_ROYALE_SIMULATION_FIXED_DELTA_HPP

#include "simulation_limits.hpp"

#include <chrono>
#include <cstdint>

namespace blob_royale::simulation {

// canonical: fixed_delta -- the only simulation timestep accepted by the deterministic core.
class FixedDelta final {
public:
  static constexpr std::int64_t kNanoseconds = kFixedDeltaNanoseconds;
  static constexpr std::uint64_t kTicksPerSecond = kSimulationTicksPerSecond;

  // Returns the exact 2,500,000 ns simulation quantum. Arbitrary deltas cannot be constructed.
  [[nodiscard]] static constexpr FixedDelta canonical() noexcept { return FixedDelta{}; }

  FixedDelta(const FixedDelta&) = default;
  FixedDelta(FixedDelta&&) noexcept = default;
  FixedDelta& operator=(const FixedDelta&) = default;
  FixedDelta& operator=(FixedDelta&&) noexcept = default;
  ~FixedDelta() = default;

  [[nodiscard]] constexpr std::chrono::nanoseconds duration() const noexcept {
    return std::chrono::nanoseconds{kNanoseconds};
  }

  // Returns 1/400 s using the operation order fixed by ADR 0003.
  [[nodiscard]] constexpr double seconds() const noexcept { return kFixedDeltaSeconds; }

  friend constexpr bool operator==(FixedDelta, FixedDelta) noexcept = default;

private:
  constexpr FixedDelta() noexcept = default;
};

} // namespace blob_royale::simulation

#endif
