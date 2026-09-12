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
// Intrinsic first-version movement tuning bounds, not a clamp on externally imparted velocity
// or a certification of continuous-motion capacity. Zero acceleration remains a legal setting.
inline constexpr double kMinimumMovementAcceleration = 0.0;
inline constexpr double kMaximumMovementAcceleration = 10'000.0;
inline constexpr double kMinimumNormalTopSpeed = 1.0;
inline constexpr double kMaximumNormalTopSpeed = 10'000.0;
inline constexpr double kDefaultMovementAcceleration = 400.0;
inline constexpr double kDefaultNormalTopSpeed = 600.0;
// One entity is one **entity slot** in the world. This bounds the entity roster and every per-kind
// component store, and an entity is not a player: a wall, a projectile, a pickup, and royale's zone
// each take a slot and none of them is a player, so the bound says entities (engine review finding
// 11).
//
// The word used to be "seat", and it changed because the lobby took that word for the thing players
// call a seat -- a place in a pre-match roster (`seat_roster.hpp`). One noun for two unrelated
// bounds, one of which players read on a screen, is the ambiguity worth spending a rename on; the
// lobby keeps "seat" because that is what a person sitting down calls it.
inline constexpr std::size_t kMaximumEntityCount = 4'096;
// The protocol v1 snapshot player limit, which `src/protocol/protocol_constants.hpp` pins to this
// value with a static_assert. It is a *publication* bound over the entities carrying both a
// PhysicsBody and a Controllable, which is what a player is, and it is a different question from
// how many entity slots the world has: protocol v2 caps a snapshot at 1,024 while the simulation
// still permits kMaximumEntityCount entities. A player is an entity, so this can never exceed the
// entity-slot count; nothing but a snapshot publication reads it.
inline constexpr std::size_t kMaximumPlayerCount = 4'096;
static_assert(kMaximumPlayerCount <= kMaximumEntityCount,
              "a player is an entity, so the published player limit cannot exceed the entity slot "
              "count");
// canonical: maximum_lobby_seat_count -- how many seats one pre-match lobby may declare.
//
// A *lobby* bound, not a roster bound: it caps the ordered seat list `MatchState` carries and
// copies into the working world every tick, and it is what makes a seat count arriving from a
// client a bounded input rather than an unbounded allocation. Sixty-four is far above any playable
// competitive field and far below `kMaximumEntityCount`, which is deliberate -- the tighter and
// more meaningful ceiling is the map's own spawn-marker count, applied by the mode that knows what
// a spawn marker is (`src/gameplay/royale/royale_mode.hpp`), and this exists so a map with
// thousands of markers still cannot declare a lobby the tick has to copy.
inline constexpr std::size_t kMaximumLobbySeatCount = 64;
static_assert(kMaximumLobbySeatCount <= kMaximumPlayerCount,
              "every seated player is a published player, so a lobby cannot exceed the player "
              "publication bound");
// One tick's submitted commands, before canonicalization collapses them to at most one of each
// kind per addressed identity. The runtime's bounded mailbox is the upstream boundary that keeps
// a batch below this; this is the simulation's own fail-closed ceiling on an unbounded input.
inline constexpr std::size_t kMaximumInputBatchCommandCount = 65'536;
// No tick can bring more entities into existence than the world has seats, so a reservation wider
// than the roster is an allocator defect rather than a large tick.
inline constexpr std::uint64_t kMaximumEntityIdReservationCount =
    static_cast<std::uint64_t>(kMaximumEntityCount);
// canonical: system_created_entity_headroom -- how many entities one tick's systems may create.
//
// Every tick's reservation is `spawn_count + this`, so this is the whole budget shared by every
// system that calls `GameWorld::create_entity()`: royale's `zone_shrink` takes it on the first
// running tick, and `hazard_spawn` takes what is left on every other one.
//
// **It lives here because three unrelated callers must agree on it and only this library is
// reachable from all three**: `runtime::EntityIdAllocator` sizes the production reservation,
// `tests/fixtures/replay_fixture.hpp` reproduces that sizing for a replay that has no runtime, and
// `tests/unit/gameplay/gameplay_test_fixture.hpp` does the same for a hand-built world. Each
// previously declared its own literal `1` with a comment saying the three must agree, which is a
// request rather than an enforcement -- the same defect the published kind-name grammar had before
// it moved into this library.
//
// **Raising it is not a local change.** The allocator advances its monotonic cursor by
// `spawn_count + this` on *every* tick, so a wider headroom renumbers every simulation-created
// entity id from the second tick onward, and the recorded replay logs under
// `tests/fixtures/replays/` name explicit ids that would all have to be regenerated. A mechanic
// that needs more than one entity per tick spreads its creations across ticks instead.
inline constexpr std::uint64_t kSystemCreatedEntityHeadroom = 1;
static_assert(kSystemCreatedEntityHeadroom >= 1,
              "every tick must receive a non-empty block: royale creates its zone entity from the "
              "reservation on its first running tick");
static_assert(kSystemCreatedEntityHeadroom <= kMaximumEntityIdReservationCount,
              "the headroom alone must still be a legal reservation width");
// One tick's WorldEvent list: sixteen events per entity slot. The dominant producer is the contact
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
// The published kind-name length of `docs/protocol/schema/v2/common.schema.json`
// § `$defs/kind_name`, shared by every configured kind name: a mode, a controller kind, a hazard
// kind. Paired with `snake_case_identity.hpp`'s grammar by `is_wire_kind_name`.
inline constexpr std::size_t kMaximumKindNameLength = 64;
inline constexpr std::size_t kMaximumUnprofiledNpcKindCount = 64;
inline constexpr std::size_t kMaximumNpcProfileCount = 16;
inline constexpr std::size_t kMaximumNpcCatalogueChoiceCount =
    kMaximumUnprofiledNpcKindCount + kMaximumNpcProfileCount;

inline constexpr std::size_t kMaximumContactRuleNameLength = 64;
// One map's authored content. Static bodies and markers each take an entity slot once a mode seats
// them, so neither may exceed the roster the world can hold; the metadata bounds keep a map file a
// declaration rather than an unbounded blob.
inline constexpr std::size_t kMaximumMapNameLength = 64;
inline constexpr std::size_t kMaximumMapStaticBodyCount = kMaximumEntityCount;
inline constexpr std::size_t kMaximumMapMarkerCount = kMaximumEntityCount;
inline constexpr std::size_t kMaximumMapMarkerKindLength = 64;
inline constexpr std::size_t kMaximumMapMetadataEntryCount = 64;
inline constexpr std::size_t kMaximumMapMetadataKeyLength = 64;
inline constexpr std::size_t kMaximumMapMetadataValueLength = 256;
// Authored terrain and derived Boolean-boundary work are separately bounded. These provisional
// engineering limits bound startup/query memory, not a native performance certification.
inline constexpr std::size_t kMaximumTerrainCorridorCount = 8;
inline constexpr std::size_t kMaximumTerrainSegmentCount = 32;
inline constexpr std::size_t kMaximumTerrainPointCount = 40;
inline constexpr std::size_t kMaximumTerrainHoleCount = 32;
inline constexpr std::size_t kMaximumTerrainBoundaryElementCount = 8'192;
inline constexpr std::size_t kMaximumTerrainArrangementElementCount = 60'000;
// Maximum coordinatewise directed nextafter corrections after analytic
// nearest-boundary selection. Exhaustion raises TERRAIN_GEOMETRY_PRECISION_LOST.
inline constexpr std::size_t kMaximumTerrainWitnessRoundingStepCount = 4;
// Pure continuous-motion prototype ceilings. Step 5 selects supported native capacity; these
// deterministic work/storage guards are not performance certification or live-kernel adoption.
inline constexpr std::size_t kMaximumMotionBodyCount = 256;
inline constexpr std::size_t kMaximumMotionCandidatePairCount =
    (kMaximumMotionBodyCount * (kMaximumMotionBodyCount - 1)) / 2;
inline constexpr std::size_t kMaximumMotionPairExaminationCount = 1'000'000;
inline constexpr std::size_t kMaximumMotionRootQueryCount = 250'000;
inline constexpr std::size_t kMaximumMotionEventCount = 2'048;
inline constexpr std::size_t kMaximumMotionTriggerQueryCount = 250'000;
inline constexpr std::size_t kMaximumMotionPathSegmentCount =
    kMaximumMotionBodyCount + (2 * kMaximumMotionEventCount);
inline constexpr std::size_t kMaximumMotionEffectCount = 8'192;
inline constexpr std::size_t kMaximumMotionTriggerDeclarationCount = 512;
inline constexpr std::size_t kMaximumMotionTriggerCursorValue = kMaximumMapMarkerCount;
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
