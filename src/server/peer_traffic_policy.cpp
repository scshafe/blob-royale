#include "peer_traffic_policy.hpp"

#include <boost/asio/ip/address.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace blob_royale::server {
namespace {

inline constexpr std::string_view kLoopbackAccountingPrincipal = "@loopback";

[[nodiscard]] std::string_view accounting_principal(const std::string_view peer_address) noexcept {
  boost::system::error_code parse_error;
  const boost::asio::ip::address address = boost::asio::ip::make_address(peer_address, parse_error);
  return !parse_error && address.is_loopback() ? kLoopbackAccountingPrincipal : peer_address;
}

[[nodiscard]] std::uint64_t retry_after_milliseconds(const double missing_tokens,
                                                     const double refill_per_second) noexcept {
  const double milliseconds = std::ceil((missing_tokens / refill_per_second) * 1'000.0);
  return static_cast<std::uint64_t>(
      std::clamp(milliseconds, 1.0, static_cast<double>(protocol::kMaximumRetryAfterMilliseconds)));
}

} // namespace

std::size_t TransparentStringHash::operator()(const std::string_view value) const noexcept {
  return std::hash<std::string_view>{}(value);
}

bool TransparentStringEqual::operator()(const std::string_view left,
                                        const std::string_view right) const noexcept {
  return left == right;
}

TcpAdmissionLease::TcpAdmissionLease(PeerTrafficPolicy& traffic_policy,
                                     std::string accounting_principal) noexcept
    : traffic_policy_(&traffic_policy), accounting_principal_(std::move(accounting_principal)) {}

TcpAdmissionLease::TcpAdmissionLease(TcpAdmissionLease&& other) noexcept
    : traffic_policy_(std::exchange(other.traffic_policy_, nullptr)),
      accounting_principal_(std::move(other.accounting_principal_)) {}

TcpAdmissionLease& TcpAdmissionLease::operator=(TcpAdmissionLease&& other) noexcept {
  if (this != &other) {
    release();
    traffic_policy_ = std::exchange(other.traffic_policy_, nullptr);
    accounting_principal_ = std::move(other.accounting_principal_);
  }
  return *this;
}

TcpAdmissionLease::~TcpAdmissionLease() { release(); }

void TcpAdmissionLease::release() noexcept {
  if (traffic_policy_ == nullptr) {
    return;
  }
  traffic_policy_->close_tcp_connection(accounting_principal_, PeerTrafficPolicy::Clock::now());
  traffic_policy_ = nullptr;
}

WebSocketAdmissionLease::WebSocketAdmissionLease(PeerTrafficPolicy& traffic_policy,
                                                 std::string accounting_principal) noexcept
    : traffic_policy_(&traffic_policy), accounting_principal_(std::move(accounting_principal)) {}

WebSocketAdmissionLease::WebSocketAdmissionLease(WebSocketAdmissionLease&& other) noexcept
    : traffic_policy_(std::exchange(other.traffic_policy_, nullptr)),
      accounting_principal_(std::move(other.accounting_principal_)) {}

WebSocketAdmissionLease&
WebSocketAdmissionLease::operator=(WebSocketAdmissionLease&& other) noexcept {
  if (this != &other) {
    release();
    traffic_policy_ = std::exchange(other.traffic_policy_, nullptr);
    accounting_principal_ = std::move(other.accounting_principal_);
  }
  return *this;
}

WebSocketAdmissionLease::~WebSocketAdmissionLease() { release(); }

void WebSocketAdmissionLease::release() noexcept {
  if (traffic_policy_ == nullptr) {
    return;
  }
  traffic_policy_->close_websocket(accounting_principal_, PeerTrafficPolicy::Clock::now());
  traffic_policy_ = nullptr;
}

AdmissionResult PeerTrafficPolicy::TokenBucket::consume(const Clock::time_point now) {
  if (now > last_refill) {
    const double elapsed_seconds = std::chrono::duration<double>(now - last_refill).count();
    available_tokens = std::min(capacity, available_tokens + (elapsed_seconds * refill_per_second));
    last_refill = now;
  }
  if (available_tokens >= 1.0) {
    available_tokens -= 1.0;
    return AdmissionResult::accepted();
  }
  return {false, AdmissionDenial::kRateLimit,
          retry_after_milliseconds(1.0 - available_tokens, refill_per_second), 0};
}

PeerTrafficPolicy::PeerState::PeerState(const Clock::time_point now)
    : http_requests{ServerLimits::kHttpRequestBucketCapacity,
                    ServerLimits::kHttpRequestBucketCapacity,
                    ServerLimits::kHttpRequestRefillPerSecond, now},
      websocket_upgrades{ServerLimits::kWebSocketUpgradeBucketCapacity,
                         ServerLimits::kWebSocketUpgradeBucketCapacity,
                         ServerLimits::kWebSocketUpgradeRefillPerSecond, now},
      last_activity(now) {}

PeerTrafficPolicy::PeerState*
PeerTrafficPolicy::find_or_admit_peer(const std::string_view principal,
                                      const Clock::time_point now) {
  if (const auto existing = peers_.find(principal); existing != peers_.end()) {
    existing->second.last_activity = now;
    return &existing->second;
  }
  if (peers_.size() >= ServerLimits::kTrackedPeerMaximumCount) {
    const auto eviction = oldest_inactive_peer();
    if (eviction == peers_.end()) {
      return nullptr;
    }
    peers_.erase(eviction);
  }
  const auto [entry, inserted] = peers_.try_emplace(std::string{principal}, now);
  static_cast<void>(inserted);
  return &entry->second;
}

PeerTrafficPolicy::PeerMap::iterator PeerTrafficPolicy::oldest_inactive_peer() noexcept {
  auto candidate = peers_.end();
  for (auto entry = peers_.begin(); entry != peers_.end(); ++entry) {
    const PeerState& state = entry->second;
    if (state.active_tcp_connections != 0 || state.active_websockets != 0) {
      continue;
    }
    if (candidate == peers_.end() || state.last_activity < candidate->second.last_activity ||
        (state.last_activity == candidate->second.last_activity &&
         entry->first < candidate->first)) {
      candidate = entry;
    }
  }
  return candidate;
}

TcpReservationResult PeerTrafficPolicy::reserve_tcp_connection(const std::string_view peer_address,
                                                               const Clock::time_point now) {
  prune_idle(now);
  if (active_tcp_connection_count_ >= ServerLimits::kConcurrentTcpConnectionMaximumCount) {
    return {{false, AdmissionDenial::kConnectionLimit, 1'000,
             ServerLimits::kConcurrentTcpConnectionMaximumCount},
            std::nullopt};
  }
  const std::string_view principal = accounting_principal(peer_address);
  PeerState* const state = find_or_admit_peer(principal, now);
  if (state == nullptr) {
    return {
        {false, AdmissionDenial::kConnectionLimit, 1'000, ServerLimits::kTrackedPeerMaximumCount},
        std::nullopt};
  }
  ++state->active_tcp_connections;
  ++active_tcp_connection_count_;
  return {AdmissionResult::accepted(), TcpAdmissionLease{*this, std::string{principal}}};
}

void PeerTrafficPolicy::close_tcp_connection(const std::string_view peer_address,
                                             const Clock::time_point now) noexcept {
  const auto entry = peers_.find(accounting_principal(peer_address));
  if (entry == peers_.end()) {
    return;
  }
  PeerState& state = entry->second;
  state.last_activity = now;
  if (state.active_tcp_connections > 0) {
    --state.active_tcp_connections;
    --active_tcp_connection_count_;
  }
}

AdmissionResult PeerTrafficPolicy::consume_http_request(const std::string_view peer_address,
                                                        const Clock::time_point now) {
  prune_idle(now);
  PeerState* const state = find_or_admit_peer(accounting_principal(peer_address), now);
  if (state == nullptr) {
    return {false, AdmissionDenial::kConnectionLimit, 1'000,
            ServerLimits::kTrackedPeerMaximumCount};
  }
  return state->http_requests.consume(now);
}

AdmissionResult PeerTrafficPolicy::consume_websocket_upgrade(const std::string_view peer_address,
                                                             const Clock::time_point now) {
  prune_idle(now);
  PeerState* const state = find_or_admit_peer(accounting_principal(peer_address), now);
  if (state == nullptr) {
    return {false, AdmissionDenial::kConnectionLimit, 1'000,
            ServerLimits::kTrackedPeerMaximumCount};
  }
  return state->websocket_upgrades.consume(now);
}

WebSocketReservationResult PeerTrafficPolicy::reserve_websocket(const std::string_view peer_address,
                                                                const Clock::time_point now) {
  prune_idle(now);
  const std::string_view principal = accounting_principal(peer_address);
  PeerState* const state = find_or_admit_peer(principal, now);
  if (state == nullptr) {
    return {
        {false, AdmissionDenial::kConnectionLimit, 1'000, ServerLimits::kTrackedPeerMaximumCount},
        std::nullopt};
  }
  if (active_websocket_count_ >= ServerLimits::kConcurrentWebSocketMaximumCount) {
    return {{false, AdmissionDenial::kConnectionLimit, 1'000,
             ServerLimits::kConcurrentWebSocketMaximumCount},
            std::nullopt};
  }
  if (state->active_websockets >= ServerLimits::kConcurrentWebSocketPerPeerMaximumCount) {
    return {{false, AdmissionDenial::kConnectionLimit, 1'000,
             ServerLimits::kConcurrentWebSocketPerPeerMaximumCount},
            std::nullopt};
  }
  ++state->active_websockets;
  ++active_websocket_count_;
  return {AdmissionResult::accepted(), WebSocketAdmissionLease{*this, std::string{principal}}};
}

void PeerTrafficPolicy::close_websocket(const std::string_view peer_address,
                                        const Clock::time_point now) noexcept {
  const auto entry = peers_.find(accounting_principal(peer_address));
  if (entry == peers_.end()) {
    return;
  }
  PeerState& state = entry->second;
  state.last_activity = now;
  if (state.active_websockets > 0) {
    --state.active_websockets;
    --active_websocket_count_;
  }
}

void PeerTrafficPolicy::prune_idle(const Clock::time_point now) {
  std::erase_if(peers_, [now](const auto& entry) {
    const PeerState& state = entry.second;
    return state.active_tcp_connections == 0 && state.active_websockets == 0 &&
           now >= state.last_activity &&
           now - state.last_activity >= ServerLimits::kPeerRateStateIdleRetention;
  });
}

SessionTokenBucket::SessionTokenBucket(const double capacity, const double refill_per_second,
                                       const Clock::time_point opened_at) noexcept
    : capacity_(capacity), refill_per_second_(refill_per_second), available_tokens_(capacity),
      last_refill_(opened_at) {}

bool SessionTokenBucket::consume(const Clock::time_point now) noexcept {
  if (now > last_refill_) {
    const double elapsed_seconds = std::chrono::duration<double>(now - last_refill_).count();
    available_tokens_ =
        std::min(capacity_, available_tokens_ + (elapsed_seconds * refill_per_second_));
    last_refill_ = now;
  }
  if (available_tokens_ < 1.0) {
    return false;
  }
  available_tokens_ -= 1.0;
  return true;
}

ControlFrameRatePolicy::ControlFrameRatePolicy(const Clock::time_point opened_at) noexcept
    : bucket_(static_cast<double>(ServerLimits::kControlFrameBurstMaximumCount),
              ServerLimits::kControlFrameRefillPerSecond, opened_at) {}

CommandRatePolicy::CommandRatePolicy(const Clock::time_point opened_at) noexcept
    : bucket_(ServerLimits::kSessionCommandBucketCapacity,
              ServerLimits::kSessionCommandRefillPerSecond, opened_at) {}

} // namespace blob_royale::server
