#ifndef BLOB_ROYALE_SERVER_SERVER_LIMITS_HPP
#define BLOB_ROYALE_SERVER_SERVER_LIMITS_HPP

#include "protocol_constants.hpp"

#include "simulation_limits.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace blob_royale::server {

// canonical: server_limits -- the complete fixed resource policy for both served protocol
// versions. Every bound below is protocol v1's and unchanged; the v2 rows are additions, because
// v2 relaxes none of them (`docs/protocol/v2.md` § "Limits").
// These values are deliberately not runtime-tunable: changing one changes the accepted
// transport contract and must be reviewed together with docs/protocol/v1.md and v2.md.
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

  // Protocol v2's per-session client command budget (`docs/protocol/v2.md` § "Limits"). It is per
  // session rather than per principal, so one tab cannot starve another tab of the same player,
  // and it is a separate ledger from the control-frame budget above, so a peer cannot spend one on
  // the other. A token is charged before any JSON parsing, which is what bounds inbound parser
  // work with the bucket rather than with the parser.
  static constexpr double kSessionCommandBucketCapacity = 30.0;
  static constexpr double kSessionCommandRefillPerSecond = 20.0;

  // Committed ticks a v2 session waits before repeating a spawn request that has gone unanswered.
  //
  // **The same rule an in-process bot follows, derived from the same tick rate rather than shared
  // as a constant.** `controllers::kSpawnRequestRetryTicks` states the reasoning in full: asking
  // once strands a session whose spawn was refused, and asking every presentation frame gives one
  // controller two bodies, because a request is in flight for at least one tick. The value cannot
  // be imported, because `blob_server` deliberately does not depend on `blob_controllers` -- the
  // networked session fills the controller role without the library that names it -- so both
  // derive it from `simulation::kSimulationTicksPerSecond` and the static assertion below pins the
  // arithmetic.
  static constexpr std::uint64_t kSessionSpawnRequestRetryTicks =
      simulation::kSimulationTicksPerSecond / 10;
  static_assert(kSessionSpawnRequestRetryTicks >= 2,
                "a spawn request is in flight for at least one tick, so a retry interval below two "
                "committed ticks would ask again while the engine was already seating the body");

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
