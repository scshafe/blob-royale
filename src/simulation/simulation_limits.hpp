#ifndef BLOB_ROYALE_SIMULATION_SIMULATION_LIMITS_HPP
#define BLOB_ROYALE_SIMULATION_SIMULATION_LIMITS_HPP

#include <cstddef>
#include <cstdint>
#include <limits>

namespace blob_royale::simulation {

static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53 &&
                  std::numeric_limits<double>::max_exponent == 1024,
              "blob_simulation requires IEEE-754 binary64 double values");

// canonical: simulation_limits -- shared by domain values and input boundaries.
inline constexpr std::uint64_t kMaximumProtocolSafeInteger = 9'007'199'254'740'991ULL;
inline constexpr std::uint64_t kMinimumEntityId = 1;
inline constexpr std::uint64_t kMaximumEntityId = kMaximumProtocolSafeInteger;
inline constexpr double kMaximumPhysicalComponentMagnitude = 1'000'000'000'000.0;
inline constexpr std::size_t kMaximumPlayerCount = 4'096;
inline constexpr std::uint64_t kSimulationTicksPerSecond = 400;
inline constexpr std::int64_t kFixedDeltaNanoseconds = 2'500'000;
inline constexpr double kFixedDeltaSeconds = 1.0 / static_cast<double>(kSimulationTicksPerSecond);
inline constexpr double kPositionTolerance = 1e-9;
inline constexpr double kVelocityTolerance = 1e-9;
inline constexpr double kAccelerationTolerance = 1e-9;
inline constexpr double kScalarTolerance = 1e-9;
inline constexpr double kRelativeTolerance = 1e-12;
inline constexpr double kMaximumWorldDimension = 1'000'000'000.0;
inline constexpr std::size_t kMaximumSpatialGridCellCount = 1'048'576;
inline constexpr std::size_t kMaximumSpatialGridMembershipCount = 16'777'216;
inline constexpr std::size_t kMaximumSpatialGridCandidatePairCount =
    (kMaximumPlayerCount * (kMaximumPlayerCount - 1)) / 2;
// Permits repeated boundary/corner observations while bounding adversarial large-disc traversal.
inline constexpr std::size_t kMaximumSpatialGridCandidatePairObservationCount =
    kMaximumSpatialGridCandidatePairCount * 8;
inline constexpr std::size_t kMaximumSpatialGridCandidatePairAllocationByteCount = 134'217'728;
inline constexpr std::size_t kMaximumSpatialGridCandidatePairBitsetByteCount =
    (kMaximumSpatialGridCandidatePairCount + 7) / 8;

static_assert(kFixedDeltaNanoseconds * static_cast<std::int64_t>(kSimulationTicksPerSecond) ==
              1'000'000'000);

} // namespace blob_royale::simulation

#endif
