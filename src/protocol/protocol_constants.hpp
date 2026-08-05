#ifndef BLOB_ROYALE_PROTOCOL_PROTOCOL_CONSTANTS_HPP
#define BLOB_ROYALE_PROTOCOL_PROTOCOL_CONSTANTS_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace blob_royale::protocol {

// canonical: protocol_v1_constants -- exact limits and identities from the accepted schemas.
inline constexpr std::string_view kProtocolVersion = "1.0";
inline constexpr std::string_view kConfigurationResponseSchemaId =
    "blob-royale://protocol/v1/configuration-response";
inline constexpr std::string_view kLivenessResponseSchemaId =
    "blob-royale://protocol/v1/liveness-response";
inline constexpr std::string_view kReadinessResponseSchemaId =
    "blob-royale://protocol/v1/readiness-response";
inline constexpr std::string_view kErrorResponseSchemaId =
    "blob-royale://protocol/v1/error-response";
inline constexpr std::string_view kSnapshotMessageSchemaId =
    "blob-royale://protocol/v1/snapshot-message";

inline constexpr std::uint64_t kMaximumSafeInteger = 9'007'199'254'740'991ULL;
inline constexpr double kMaximumFiniteWorldScalar = 1'000'000'000'000.0;
inline constexpr double kMaximumPublicWorldDimension = 1'000'000'000.0;
inline constexpr std::uint64_t kRequiredSimulationTicksPerSecond = 400;
inline constexpr double kRequiredFixedDeltaSeconds = 0.0025;
inline constexpr std::uint64_t kMinimumSnapshotsPerSecond = 1;
inline constexpr std::uint64_t kMaximumSnapshotsPerSecond = 60;
inline constexpr std::size_t kSnapshotPlayerLimit = 4'096;
inline constexpr std::size_t kSnapshotFrameMaximumByteCount = 2'097'152;
inline constexpr std::size_t kHttpJsonResponseMaximumByteCount = 65'536;
inline constexpr std::size_t kRequestIdMaximumCharacterCount = 64;
inline constexpr std::size_t kHttpErrorMessageMaximumCharacterCount = 256;
inline constexpr std::size_t kHttpErrorReasonMaximumCharacterCount = 128;
inline constexpr std::uint64_t kErrorDetailMaximumLimit = 2'097'152;
inline constexpr std::uint64_t kMaximumRetryAfterMilliseconds = 60'000;

} // namespace blob_royale::protocol

#endif
