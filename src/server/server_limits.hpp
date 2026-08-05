#ifndef BLOB_ROYALE_SERVER_SERVER_LIMITS_HPP
#define BLOB_ROYALE_SERVER_SERVER_LIMITS_HPP

#include "protocol_constants.hpp"

#include <chrono>
#include <cstddef>

namespace blob_royale::server {

// canonical: server_limits -- the complete fixed resource policy for protocol v1.
// These values are deliberately not runtime-tunable: changing one changes the accepted
// transport contract and must be reviewed together with docs/protocol/v1.md.
struct ServerLimits final {
  static constexpr std::size_t kRequestTargetMaximumByteCount = 2'048;
  static constexpr std::size_t kHeaderSectionMaximumByteCount = 16'384;
  static constexpr std::size_t kHeaderFieldMaximumCount = 64;
  static constexpr std::size_t kRequestBodyMaximumByteCount = 0;
  static constexpr std::size_t kPipelinedResponseMaximumCount = 8;
  static constexpr std::size_t kRequestsPerConnectionMaximumCount = 100;

  static constexpr std::size_t kConcurrentTcpConnectionMaximumCount = 128;
  static constexpr std::size_t kConcurrentWebSocketMaximumCount = 32;
  static constexpr std::size_t kConcurrentWebSocketPerPeerMaximumCount = 8;
  static constexpr std::size_t kTrackedPeerMaximumCount = 128;

  static constexpr double kHttpRequestBucketCapacity = 20.0;
  static constexpr double kHttpRequestRefillPerSecond = 2.0;
  static constexpr double kWebSocketUpgradeBucketCapacity = 4.0;
  static constexpr double kWebSocketUpgradeRefillPerSecond = 0.2;

  static constexpr std::size_t kInboundWebSocketMessageMaximumByteCount = 1'024;
  static constexpr std::size_t kControlFrameBurstMaximumCount = 5;
  static constexpr double kControlFrameRefillPerSecond = 1.0;

  // Snapshot encoding reserves the protocol's complete maximum before starting the encoder.
  // Eight maximum encoded-payload reservations may be active globally, and the byte-rate bucket
  // recovers 32 maximum frames per second. Unused bytes are refunded after bounded encoding.
  static constexpr std::size_t kSnapshotEgressReservationByteCount =
      protocol::kSnapshotFrameMaximumByteCount;
  static constexpr std::size_t kSnapshotEgressActiveMaximumByteCount =
      8 * kSnapshotEgressReservationByteCount;
  static constexpr std::size_t kSnapshotEgressTokenBucketCapacityByteCount =
      kSnapshotEgressActiveMaximumByteCount;
  static constexpr std::size_t kSnapshotEgressRefillByteCountPerSecond =
      32 * kSnapshotEgressReservationByteCount;

  static constexpr auto kRequestHeaderDeadline = std::chrono::seconds{10};
  static constexpr auto kWebSocketWriteDeadline = std::chrono::seconds{5};
  static constexpr auto kWebSocketIdlePingInterval = std::chrono::seconds{15};
  static constexpr auto kWebSocketPongDeadline = std::chrono::seconds{10};
  static constexpr auto kPeerRateStateIdleRetention = std::chrono::minutes{10};
  static constexpr auto kServerShutdownDeadline = std::chrono::seconds{5};

  static constexpr std::size_t kConfigurationAllowlistMaximumEntryCount = 32;
  static constexpr std::size_t kConfigurationAllowlistEntryMaximumByteCount = 255;
  static constexpr std::size_t kConfigurationAllowlistMaximumAggregateByteCount = 4'096;

  static_assert(protocol::kHttpJsonResponseMaximumByteCount == 65'536);
  static_assert(protocol::kSnapshotFrameMaximumByteCount == 2'097'152);
  static_assert(protocol::kSnapshotPlayerLimit == 4'096);
};

} // namespace blob_royale::server

#endif
