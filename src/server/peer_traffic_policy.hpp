#ifndef BLOB_ROYALE_SERVER_PEER_TRAFFIC_POLICY_HPP
#define BLOB_ROYALE_SERVER_PEER_TRAFFIC_POLICY_HPP

#include "server_limits.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace blob_royale::server {

class PeerTrafficPolicy;

struct TransparentStringHash final {
  using is_transparent = void;

  [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept;
};

struct TransparentStringEqual final {
  using is_transparent = void;

  [[nodiscard]] bool operator()(std::string_view left, std::string_view right) const noexcept;
};

enum class AdmissionDenial {
  kNone,
  kRateLimit,
  kConnectionLimit,
};

struct AdmissionResult final {
  bool allowed;
  AdmissionDenial denial;
  std::uint64_t retry_after_ms;
  std::size_t connection_limit;

  [[nodiscard]] static constexpr AdmissionResult accepted() noexcept {
    return {true, AdmissionDenial::kNone, 0, 0};
  }
};

// Move-only RAII ownership of one accepted TCP connection capacity slot.
class TcpAdmissionLease final {
public:
  TcpAdmissionLease(const TcpAdmissionLease&) = delete;
  TcpAdmissionLease(TcpAdmissionLease&& other) noexcept;
  TcpAdmissionLease& operator=(const TcpAdmissionLease&) = delete;
  TcpAdmissionLease& operator=(TcpAdmissionLease&& other) noexcept;
  ~TcpAdmissionLease();

  void release() noexcept;
  [[nodiscard]] bool active() const noexcept { return traffic_policy_ != nullptr; }

private:
  friend class PeerTrafficPolicy;
  TcpAdmissionLease(PeerTrafficPolicy& traffic_policy, std::string accounting_principal) noexcept;

  PeerTrafficPolicy* traffic_policy_;
  std::string accounting_principal_;
};

struct TcpReservationResult final {
  AdmissionResult admission;
  std::optional<TcpAdmissionLease> lease;
};

// Move-only RAII ownership of one accepted global/per-peer WebSocket capacity slot.
class WebSocketAdmissionLease final {
public:
  WebSocketAdmissionLease(const WebSocketAdmissionLease&) = delete;
  WebSocketAdmissionLease(WebSocketAdmissionLease&& other) noexcept;
  WebSocketAdmissionLease& operator=(const WebSocketAdmissionLease&) = delete;
  WebSocketAdmissionLease& operator=(WebSocketAdmissionLease&& other) noexcept;
  ~WebSocketAdmissionLease();

  void release() noexcept;
  [[nodiscard]] bool active() const noexcept { return traffic_policy_ != nullptr; }

private:
  friend class PeerTrafficPolicy;
  WebSocketAdmissionLease(PeerTrafficPolicy& traffic_policy,
                          std::string accounting_principal) noexcept;

  PeerTrafficPolicy* traffic_policy_;
  std::string accounting_principal_;
};

struct WebSocketReservationResult final {
  AdmissionResult admission;
  std::optional<WebSocketAdmissionLease> lease;
};

// canonical: peer_traffic_policy -- one event-loop-confined origin resource and rate ledger.
// Socket peer addresses are the only identities. Forwarding headers never enter this API.
class PeerTrafficPolicy final {
public:
  using Clock = std::chrono::steady_clock;

  PeerTrafficPolicy() = default;
  PeerTrafficPolicy(const PeerTrafficPolicy&) = delete;
  PeerTrafficPolicy(PeerTrafficPolicy&&) = delete;
  PeerTrafficPolicy& operator=(const PeerTrafficPolicy&) = delete;
  PeerTrafficPolicy& operator=(PeerTrafficPolicy&&) = delete;
  ~PeerTrafficPolicy() = default;

  // Reserves one accepted TCP socket or reports the fixed global bound.
  [[nodiscard]] TcpReservationResult reserve_tcp_connection(std::string_view peer_address,
                                                            Clock::time_point now);

  // Releases exactly one previously accepted TCP socket. Repeated cleanup is a safe no-op.
  void close_tcp_connection(std::string_view peer_address, Clock::time_point now) noexcept;

  // Consumes one request token before routing, including requests that will fail validation.
  [[nodiscard]] AdmissionResult consume_http_request(std::string_view peer_address,
                                                     Clock::time_point now);

  // Consumes one token for every syntactically parsed upgrade attempt, including rejections.
  [[nodiscard]] AdmissionResult consume_websocket_upgrade(std::string_view peer_address,
                                                          Clock::time_point now);

  // Reserves global/per-peer WebSocket capacity only after every handshake rule passes.
  [[nodiscard]] WebSocketReservationResult reserve_websocket(std::string_view peer_address,
                                                             Clock::time_point now);

  // Releases exactly one previously reserved WebSocket session. Repeated cleanup is a safe no-op.
  void close_websocket(std::string_view peer_address, Clock::time_point now) noexcept;

  // Removes only disconnected peer state that has remained idle for the normative ten minutes.
  void prune_idle(Clock::time_point now);

  [[nodiscard]] std::size_t active_tcp_connection_count() const noexcept {
    return active_tcp_connection_count_;
  }
  [[nodiscard]] std::size_t active_websocket_count() const noexcept {
    return active_websocket_count_;
  }
  [[nodiscard]] std::size_t tracked_peer_count() const noexcept { return peers_.size(); }

private:
  struct TokenBucket final {
    double available_tokens;
    double capacity;
    double refill_per_second;
    Clock::time_point last_refill;

    [[nodiscard]] AdmissionResult consume(Clock::time_point now);
  };

  struct PeerState final {
    explicit PeerState(Clock::time_point now);

    TokenBucket http_requests;
    TokenBucket websocket_upgrades;
    std::size_t active_tcp_connections{0};
    std::size_t active_websockets{0};
    Clock::time_point last_activity;
  };

  using PeerMap =
      std::unordered_map<std::string, PeerState, TransparentStringHash, TransparentStringEqual>;

  [[nodiscard]] PeerState* find_or_admit_peer(std::string_view principal, Clock::time_point now);
  [[nodiscard]] PeerMap::iterator oldest_inactive_peer() noexcept;

  PeerMap peers_;
  std::size_t active_tcp_connection_count_{0};
  std::size_t active_websocket_count_{0};
};

// One session-local limiter for ping, pong, and other permitted control traffic.
class ControlFrameRatePolicy final {
public:
  using Clock = std::chrono::steady_clock;

  explicit ControlFrameRatePolicy(Clock::time_point opened_at) noexcept;

  // Returns true for at most the accepted burst/refill cadence and never goes negative.
  [[nodiscard]] bool consume(Clock::time_point now) noexcept;

private:
  double available_tokens_;
  Clock::time_point last_refill_;
};

} // namespace blob_royale::server

#endif
