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
inline constexpr std::uint64_t kMinimumControllerId = 1;
inline constexpr std::uint64_t kMaximumControllerId = kMaximumProtocolSafeInteger;
inline constexpr std::uint64_t kMinimumTeamId = 1;
inline constexpr std::uint64_t kMaximumTeamId = kMaximumProtocolSafeInteger;
inline constexpr double kMaximumPhysicalComponentMagnitude = 1'000'000'000'000.0;
// One entity is one seat in the world. This bounds the entity roster and every per-kind component
// store, and an entity is not a player: a wall, a projectile, a pickup, and royale's zone each take
// a seat and none of them is a player, so the bound says entities (engine review finding 11).
inline constexpr std::size_t kMaximumEntityCount = 4'096;
// The protocol v1 snapshot player limit, which `src/protocol/protocol_constants.hpp` pins to this
// value with a static_assert. It is a *publication* bound over the entities carrying both a
// PhysicsBody and a Controllable, which is what a player is, and it is a different question from
// how many seats the world has: protocol v2 caps a snapshot at 1,024 while the simulation still
// permits kMaximumEntityCount entities. A player is an entity, so this can never exceed the seat
// count; nothing but a snapshot publication reads it.
inline constexpr std::size_t kMaximumPlayerCount = 4'096;
static_assert(kMaximumPlayerCount <= kMaximumEntityCount,
              "a player is an entity, so the published player limit cannot exceed the seat count");
// One tick's submitted commands, before canonicalization collapses them to at most one of each
// kind per addressed identity. The runtime's bounded mailbox is the upstream boundary that keeps
// a batch below this; this is the simulation's own fail-closed ceiling on an unbounded input.
inline constexpr std::size_t kMaximumInputBatchCommandCount = 65'536;
// No tick can bring more entities into existence than the world has seats, so a reservation wider
// than the roster is an allocator defect rather than a large tick.
inline constexpr std::uint64_t kMaximumEntityIdReservationCount =
    static_cast<std::uint64_t>(kMaximumEntityCount);
// One tick's WorldEvent list: sixteen events per world seat. The dominant producer is the contact
// phase, which emits at most one event per contacting pair, and an equal-radius disc in a
// non-overlapping arrangement touches at most six coplanar neighbours, so sixteen leaves room for
// every other producing phase to name an entity once. Overflow is a hard simulation failure rather
// than a silent drop, because a dropped event converts a failure into a differently wrong tick
// (`docs/architecture/0004-gameplay-architecture.md` § "World events").
inline constexpr std::size_t kMaximumWorldEventCount = 16 * kMaximumEntityCount;
// A thrust direction component is a unit-interval intent, not a physical quantity: the mode's
// steering system scales it by its own declared maximum.
inline constexpr double kMaximumThrustDirectionComponentMagnitude = 1.0;
// One contact rule row's declared name, which is also what a ContactEvent publishes.
inline constexpr std::size_t kMaximumContactRuleNameLength = 64;
// One map's authored content. Static bodies and markers each take a world seat once a mode seats
// them, so neither may exceed the roster the world can hold; the metadata bounds keep a map file a
// declaration rather than an unbounded blob.
inline constexpr std::size_t kMaximumMapNameLength = 64;
inline constexpr std::size_t kMaximumMapStaticBodyCount = kMaximumEntityCount;
inline constexpr std::size_t kMaximumMapMarkerCount = kMaximumEntityCount;
inline constexpr std::size_t kMaximumMapMarkerKindLength = 64;
inline constexpr std::size_t kMaximumMapMetadataEntryCount = 64;
inline constexpr std::size_t kMaximumMapMetadataKeyLength = 64;
inline constexpr std::size_t kMaximumMapMetadataValueLength = 256;
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
    (kMaximumEntityCount * (kMaximumEntityCount - 1)) / 2;
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
