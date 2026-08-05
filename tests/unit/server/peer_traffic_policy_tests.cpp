#include "peer_traffic_policy.hpp"
#include "server_limits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace server = blob_royale::server;

TEST_CASE("PeerTrafficPolicy applies the exact HTTP token bucket", "[unit][server][rate]") {
  server::PeerTrafficPolicy policy;
  const auto opened_at = server::PeerTrafficPolicy::Clock::time_point{};

  for (std::size_t request = 0;
       request < static_cast<std::size_t>(server::ServerLimits::kHttpRequestBucketCapacity);
       ++request) {
    CHECK(policy.consume_http_request("127.0.0.1", opened_at).allowed);
  }
  const server::AdmissionResult denied = policy.consume_http_request("127.0.0.1", opened_at);
  CHECK_FALSE(denied.allowed);
  CHECK(denied.denial == server::AdmissionDenial::kRateLimit);
  CHECK(denied.retry_after_ms == 500);

  CHECK(
      policy.consume_http_request("127.0.0.1", opened_at + std::chrono::milliseconds{500}).allowed);
}

TEST_CASE("PeerTrafficPolicy keeps HTTP and upgrade buckets independent", "[unit][server][rate]") {
  server::PeerTrafficPolicy policy;
  const auto now = server::PeerTrafficPolicy::Clock::time_point{};

  for (std::size_t attempt = 0;
       attempt < static_cast<std::size_t>(server::ServerLimits::kWebSocketUpgradeBucketCapacity);
       ++attempt) {
    CHECK(policy.consume_websocket_upgrade("127.0.0.1", now).allowed);
  }
  CHECK_FALSE(policy.consume_websocket_upgrade("127.0.0.1", now).allowed);
  CHECK(policy.consume_http_request("127.0.0.1", now).allowed);
  CHECK(policy.consume_websocket_upgrade("127.0.0.1", now + std::chrono::seconds{5}).allowed);
}

TEST_CASE("WebSocket admission lease releases capacity on destruction",
          "[unit][server][rate][ownership]") {
  server::PeerTrafficPolicy policy;
  const auto now = server::PeerTrafficPolicy::Clock::time_point{};
  server::TcpReservationResult tcp_reservation = policy.reserve_tcp_connection("127.0.0.1", now);
  REQUIRE(tcp_reservation.admission.allowed);

  {
    server::WebSocketReservationResult reservation = policy.reserve_websocket("127.0.0.1", now);
    REQUIRE(reservation.admission.allowed);
    REQUIRE(reservation.lease.has_value());
    CHECK(policy.active_websocket_count() == 1);
  }

  CHECK(policy.active_websocket_count() == 0);
}

TEST_CASE("TCP admission leases cannot decrement another same-peer connection",
          "[unit][server][rate][ownership]") {
  server::PeerTrafficPolicy policy;
  const auto now = server::PeerTrafficPolicy::Clock::time_point{};
  server::TcpReservationResult first = policy.reserve_tcp_connection("127.0.0.1", now);
  server::TcpReservationResult second = policy.reserve_tcp_connection("127.0.0.1", now);
  REQUIRE(first.lease.has_value());
  REQUIRE(second.lease.has_value());
  REQUIRE(policy.active_tcp_connection_count() == 2);

  server::TcpAdmissionLease moved_first = std::move(*first.lease);
  CHECK_FALSE(first.lease->active());
  moved_first.release();
  moved_first.release();

  CHECK(policy.active_tcp_connection_count() == 1);
  second.lease->release();
  CHECK(policy.active_tcp_connection_count() == 0);
}

TEST_CASE("PeerTrafficPolicy enforces per-peer WebSocket capacity", "[unit][server][rate]") {
  server::PeerTrafficPolicy policy;
  const auto now = server::PeerTrafficPolicy::Clock::time_point{};
  std::vector<server::WebSocketAdmissionLease> leases;
  for (std::size_t session = 0;
       session < server::ServerLimits::kConcurrentWebSocketPerPeerMaximumCount; ++session) {
    server::WebSocketReservationResult reservation = policy.reserve_websocket("127.0.0.1", now);
    REQUIRE(reservation.admission.allowed);
    leases.push_back(std::move(*reservation.lease));
  }

  const server::WebSocketReservationResult denied = policy.reserve_websocket("127.0.0.1", now);
  CHECK_FALSE(denied.admission.allowed);
  CHECK(denied.admission.denial == server::AdmissionDenial::kConnectionLimit);
  CHECK(denied.admission.connection_limit ==
        server::ServerLimits::kConcurrentWebSocketPerPeerMaximumCount);
}

TEST_CASE("PeerTrafficPolicy enforces the global TCP capacity", "[unit][server][rate]") {
  server::PeerTrafficPolicy policy;
  const auto now = server::PeerTrafficPolicy::Clock::time_point{};
  std::vector<server::TcpAdmissionLease> retained_leases;
  for (std::size_t connection = 0;
       connection < server::ServerLimits::kConcurrentTcpConnectionMaximumCount; ++connection) {
    server::TcpReservationResult reservation =
        policy.reserve_tcp_connection("peer-" + std::to_string(connection), now);
    REQUIRE(reservation.admission.allowed);
    retained_leases.push_back(std::move(*reservation.lease));
  }

  const server::TcpReservationResult denied = policy.reserve_tcp_connection("newest-peer", now);
  CHECK_FALSE(denied.admission.allowed);
  CHECK(denied.admission.denial == server::AdmissionDenial::kConnectionLimit);
  CHECK(denied.admission.connection_limit ==
        server::ServerLimits::kConcurrentTcpConnectionMaximumCount);
}

TEST_CASE("PeerTrafficPolicy prunes only disconnected ten-minute-idle peers",
          "[unit][server][rate]") {
  server::PeerTrafficPolicy policy;
  const auto opened_at = server::PeerTrafficPolicy::Clock::time_point{};
  server::TcpReservationResult reservation = policy.reserve_tcp_connection("127.0.0.1", opened_at);
  REQUIRE(reservation.admission.allowed);
  const auto release_not_before = server::PeerTrafficPolicy::Clock::now();
  reservation.lease->release();
  const auto release_not_after = server::PeerTrafficPolicy::Clock::now();

  policy.prune_idle(release_not_before + server::ServerLimits::kPeerRateStateIdleRetention -
                    std::chrono::nanoseconds{1});
  CHECK(policy.tracked_peer_count() == 1);
  policy.prune_idle(release_not_after + server::ServerLimits::kPeerRateStateIdleRetention);
  CHECK(policy.tracked_peer_count() == 0);
}

TEST_CASE("PeerTrafficPolicy collapses the complete loopback trust zone to one principal",
          "[unit][server][rate][trust-boundary]") {
  server::PeerTrafficPolicy policy;
  const auto now = server::PeerTrafficPolicy::Clock::time_point{};
  std::vector<server::WebSocketAdmissionLease> leases;
  leases.reserve(server::ServerLimits::kConcurrentWebSocketPerPeerMaximumCount);

  for (std::size_t session = 0;
       session < server::ServerLimits::kConcurrentWebSocketPerPeerMaximumCount; ++session) {
    const std::string_view address = session % 3 == 0   ? "127.0.0.1"
                                     : session % 3 == 1 ? "127.42.7.9"
                                                        : "::1";
    server::WebSocketReservationResult reservation = policy.reserve_websocket(address, now);
    REQUIRE(reservation.admission.allowed);
    if (session + 1 == server::ServerLimits::kConcurrentWebSocketPerPeerMaximumCount) {
      const server::WebSocketReservationResult denied =
          policy.reserve_websocket("127.255.255.254", now);
      CHECK_FALSE(denied.admission.allowed);
      CHECK(denied.admission.connection_limit ==
            server::ServerLimits::kConcurrentWebSocketPerPeerMaximumCount);
    }
    leases.push_back(std::move(*reservation.lease));
  }

  CHECK(policy.tracked_peer_count() == 1);
}

TEST_CASE("PeerTrafficPolicy deterministically evicts oldest inactive identities at its cap",
          "[unit][server][rate][churn]") {
  server::PeerTrafficPolicy policy;
  const auto now = server::PeerTrafficPolicy::Clock::time_point{};

  for (std::size_t identity = 0; identity < server::ServerLimits::kTrackedPeerMaximumCount;
       ++identity) {
    const std::string principal = "peer-" + std::to_string(identity + 1);
    CHECK(policy.consume_http_request(principal, now).allowed);
  }
  REQUIRE(policy.tracked_peer_count() == server::ServerLimits::kTrackedPeerMaximumCount);

  // Equal timestamps use lexical key order, so peer-1 is the deterministic first eviction.
  CHECK(policy.consume_http_request("zz-new-peer", now).allowed);
  CHECK(policy.tracked_peer_count() == server::ServerLimits::kTrackedPeerMaximumCount);

  std::size_t accepted_after_readmission = 0;
  while (policy.consume_http_request("peer-1", now).allowed) {
    ++accepted_after_readmission;
  }
  CHECK(accepted_after_readmission ==
        static_cast<std::size_t>(server::ServerLimits::kHttpRequestBucketCapacity));
  CHECK(policy.tracked_peer_count() == server::ServerLimits::kTrackedPeerMaximumCount);
}

TEST_CASE("PeerTrafficPolicy denies a new identity when every bounded entry is active",
          "[unit][server][rate][churn]") {
  server::PeerTrafficPolicy policy;
  const auto now = server::PeerTrafficPolicy::Clock::time_point{};
  std::vector<server::TcpAdmissionLease> leases;
  leases.reserve(server::ServerLimits::kTrackedPeerMaximumCount);

  for (std::size_t identity = 0; identity < server::ServerLimits::kTrackedPeerMaximumCount;
       ++identity) {
    server::TcpReservationResult reservation =
        policy.reserve_tcp_connection("active-" + std::to_string(identity), now);
    REQUIRE(reservation.admission.allowed);
    leases.push_back(std::move(*reservation.lease));
  }

  const server::AdmissionResult denied = policy.consume_http_request("untracked-peer", now);
  CHECK_FALSE(denied.allowed);
  CHECK(denied.denial == server::AdmissionDenial::kConnectionLimit);
  CHECK(denied.connection_limit == server::ServerLimits::kTrackedPeerMaximumCount);
  CHECK(policy.tracked_peer_count() == server::ServerLimits::kTrackedPeerMaximumCount);
}

TEST_CASE("ControlFrameRatePolicy permits one-per-second refill after its burst",
          "[unit][server][rate]") {
  const auto opened_at = server::ControlFrameRatePolicy::Clock::time_point{};
  server::ControlFrameRatePolicy policy(opened_at);
  for (std::size_t frame = 0; frame < server::ServerLimits::kControlFrameBurstMaximumCount;
       ++frame) {
    CHECK(policy.consume(opened_at));
  }
  CHECK_FALSE(policy.consume(opened_at));
  CHECK_FALSE(policy.consume(opened_at + std::chrono::milliseconds{999}));
  CHECK(policy.consume(opened_at + std::chrono::seconds{1}));
}
