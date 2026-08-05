#include "snapshot_egress_budget.hpp"

#include "game_server_error.hpp"
#include "protocol_constants.hpp"
#include "server_limits.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <utility>

namespace blob_royale::server {
namespace {

inline constexpr std::size_t kNanosecondsPerSecond = 1'000'000'000;
static_assert(ServerLimits::kSnapshotEgressReservationByteCount ==
              protocol::kSnapshotFrameMaximumByteCount);

} // namespace

SnapshotEgressLease::SnapshotEgressLease(SnapshotEgressBudget& budget,
                                         const std::size_t reserved_byte_count) noexcept
    : budget_(&budget), owned_byte_count_(reserved_byte_count) {}

SnapshotEgressLease::SnapshotEgressLease(SnapshotEgressLease&& other) noexcept
    : budget_(std::exchange(other.budget_, nullptr)),
      owned_byte_count_(std::exchange(other.owned_byte_count_, 0)),
      committed_(std::exchange(other.committed_, false)) {}

SnapshotEgressLease& SnapshotEgressLease::operator=(SnapshotEgressLease&& other) noexcept {
  if (this != &other) {
    release();
    budget_ = std::exchange(other.budget_, nullptr);
    owned_byte_count_ = std::exchange(other.owned_byte_count_, 0);
    committed_ = std::exchange(other.committed_, false);
  }
  return *this;
}

SnapshotEgressLease::~SnapshotEgressLease() { release(); }

void SnapshotEgressLease::commit_encoded_bytes(const std::size_t encoded_byte_count) {
  if (budget_ == nullptr || committed_) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed, "snapshot_egress.commit",
                          "egress reservation must be active and uncommitted"};
  }
  budget_->commit_encoding(owned_byte_count_, encoded_byte_count);
  owned_byte_count_ = encoded_byte_count;
  committed_ = true;
}

void SnapshotEgressLease::release() noexcept {
  if (budget_ == nullptr) {
    return;
  }
  budget_->release(owned_byte_count_, committed_);
  budget_ = nullptr;
  owned_byte_count_ = 0;
  committed_ = false;
}

SnapshotEgressBudget::SnapshotEgressBudget(const Clock::time_point created_at) noexcept
    : last_refill_(created_at),
      available_token_byte_count_(ServerLimits::kSnapshotEgressTokenBucketCapacityByteCount) {}

std::optional<SnapshotEgressLease> SnapshotEgressBudget::try_reserve(const ClientId client_id,
                                                                     const Clock::time_point now) {
  if (client_id == 0) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed, "snapshot_egress.client_id",
                          "egress admission requires a registered session identity"};
  }
  refill(now);
  constexpr std::size_t kReservation = ServerLimits::kSnapshotEgressReservationByteCount;
  if (waiter_count_ > 0 && waiters_.front() != client_id) {
    enqueue_waiter(client_id);
    return std::nullopt;
  }
  if (available_token_byte_count_ < kReservation ||
      active_byte_count_ > ServerLimits::kSnapshotEgressActiveMaximumByteCount - kReservation ||
      active_lease_count_ >= ServerLimits::kConcurrentWebSocketMaximumCount) {
    enqueue_waiter(client_id);
    return std::nullopt;
  }
  if (waiter_count_ > 0) {
    remove_waiter_at(0);
  }
  available_token_byte_count_ -= kReservation;
  active_byte_count_ += kReservation;
  ++active_lease_count_;
  return SnapshotEgressLease{*this, kReservation};
}

void SnapshotEgressBudget::cancel_waiter(const ClientId client_id) noexcept {
  for (std::size_t index = 0; index < waiter_count_; ++index) {
    if (waiters_[index] == client_id) {
      remove_waiter_at(index);
      return;
    }
  }
}

void SnapshotEgressBudget::enqueue_waiter(const ClientId client_id) {
  for (std::size_t index = 0; index < waiter_count_; ++index) {
    if (waiters_[index] == client_id) {
      return;
    }
  }
  if (waiter_count_ == waiters_.size()) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed, "snapshot_egress.waiters",
                          "waiter count exceeded the WebSocket session bound"};
  }
  waiters_[waiter_count_++] = client_id;
}

void SnapshotEgressBudget::remove_waiter_at(const std::size_t index) noexcept {
  if (index >= waiter_count_) {
    std::terminate();
  }
  for (std::size_t shifted = index + 1; shifted < waiter_count_; ++shifted) {
    waiters_[shifted - 1] = waiters_[shifted];
  }
  --waiter_count_;
  waiters_[waiter_count_] = 0;
}

void SnapshotEgressBudget::refill(const Clock::time_point now) noexcept {
  if (now <= last_refill_ ||
      available_token_byte_count_ == ServerLimits::kSnapshotEgressTokenBucketCapacityByteCount) {
    if (now > last_refill_) {
      last_refill_ = now;
      refill_fraction_numerator_ = 0;
    }
    return;
  }

  const std::size_t missing =
      ServerLimits::kSnapshotEgressTokenBucketCapacityByteCount - available_token_byte_count_;
  const std::size_t nanoseconds_to_full =
      ((missing * kNanosecondsPerSecond) + ServerLimits::kSnapshotEgressRefillByteCountPerSecond -
       1) /
      ServerLimits::kSnapshotEgressRefillByteCountPerSecond;
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_refill_).count();
  if (elapsed <= 0) {
    return;
  }
  if (static_cast<std::size_t>(elapsed) >= nanoseconds_to_full) {
    available_token_byte_count_ = ServerLimits::kSnapshotEgressTokenBucketCapacityByteCount;
    refill_fraction_numerator_ = 0;
    last_refill_ = now;
    return;
  }

  const std::size_t numerator =
      (static_cast<std::size_t>(elapsed) * ServerLimits::kSnapshotEgressRefillByteCountPerSecond) +
      refill_fraction_numerator_;
  available_token_byte_count_ =
      std::min(ServerLimits::kSnapshotEgressTokenBucketCapacityByteCount,
               available_token_byte_count_ + (numerator / kNanosecondsPerSecond));
  refill_fraction_numerator_ = numerator % kNanosecondsPerSecond;
  last_refill_ = now;
}

void SnapshotEgressBudget::commit_encoding(const std::size_t reserved_byte_count,
                                           const std::size_t encoded_byte_count) {
  if (reserved_byte_count != ServerLimits::kSnapshotEgressReservationByteCount ||
      encoded_byte_count == 0 || encoded_byte_count > reserved_byte_count ||
      active_byte_count_ < reserved_byte_count) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed,
                          "snapshot_egress.encoded_bytes",
                          "encoded payload violates its global egress reservation"};
  }
  const std::size_t unused_byte_count = reserved_byte_count - encoded_byte_count;
  active_byte_count_ -= unused_byte_count;
  available_token_byte_count_ = std::min(ServerLimits::kSnapshotEgressTokenBucketCapacityByteCount,
                                         available_token_byte_count_ + unused_byte_count);
}

void SnapshotEgressBudget::release(const std::size_t owned_byte_count,
                                   const bool committed) noexcept {
  if (owned_byte_count == 0 || owned_byte_count > active_byte_count_ || active_lease_count_ == 0) {
    std::terminate();
  }
  active_byte_count_ -= owned_byte_count;
  --active_lease_count_;
  if (!committed) {
    available_token_byte_count_ =
        std::min(ServerLimits::kSnapshotEgressTokenBucketCapacityByteCount,
                 available_token_byte_count_ + owned_byte_count);
  }
}

} // namespace blob_royale::server
