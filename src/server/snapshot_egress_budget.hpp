#ifndef BLOB_ROYALE_SERVER_SNAPSHOT_EGRESS_BUDGET_HPP
#define BLOB_ROYALE_SERVER_SNAPSHOT_EGRESS_BUDGET_HPP

#include "server_limits.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace blob_royale::server {

class SnapshotEgressBudget;

// Move-only ownership of one globally admitted encoded payload. Before commit it owns the
// complete worst-case reservation; after commit it owns exactly the encoded payload bytes.
class SnapshotEgressLease final {
public:
  SnapshotEgressLease(const SnapshotEgressLease&) = delete;
  SnapshotEgressLease(SnapshotEgressLease&& other) noexcept;
  SnapshotEgressLease& operator=(const SnapshotEgressLease&) = delete;
  SnapshotEgressLease& operator=(SnapshotEgressLease&& other) noexcept;
  ~SnapshotEgressLease();

  // Commits the actual bounded encoding size and refunds unused worst-case capacity.
  void commit_encoded_bytes(std::size_t encoded_byte_count);
  void release() noexcept;

  [[nodiscard]] bool active() const noexcept { return budget_ != nullptr; }
  [[nodiscard]] bool committed() const noexcept { return committed_; }
  [[nodiscard]] std::size_t owned_byte_count() const noexcept { return owned_byte_count_; }

private:
  friend class SnapshotEgressBudget;
  SnapshotEgressLease(SnapshotEgressBudget& budget, std::size_t reserved_byte_count) noexcept;

  SnapshotEgressBudget* budget_;
  std::size_t owned_byte_count_;
  bool committed_{false};
};

// canonical: snapshot_egress_budget -- one event-loop-confined aggregate encoding/egress ledger.
// It has no worker queue or executor: presentation slots either acquire one fixed reservation or
// leave their latest snapshot coalesced in the session-local delivery state.
class SnapshotEgressBudget final {
public:
  using Clock = std::chrono::steady_clock;
  using ClientId = std::uint64_t;

  explicit SnapshotEgressBudget(Clock::time_point created_at = Clock::now()) noexcept;

  SnapshotEgressBudget(const SnapshotEgressBudget&) = delete;
  SnapshotEgressBudget(SnapshotEgressBudget&&) = delete;
  SnapshotEgressBudget& operator=(const SnapshotEgressBudget&) = delete;
  SnapshotEgressBudget& operator=(SnapshotEgressBudget&&) = delete;
  ~SnapshotEgressBudget() = default;

  // Admits the protocol's full output maximum before encoding begins. Denial consumes nothing.
  [[nodiscard]] std::optional<SnapshotEgressLease> try_reserve(ClientId client_id,
                                                               Clock::time_point now);

  // Removes a session that can no longer retry at a presentation slot.
  void cancel_waiter(ClientId client_id) noexcept;

  [[nodiscard]] std::size_t active_byte_count() const noexcept { return active_byte_count_; }
  [[nodiscard]] std::size_t available_token_byte_count() const noexcept {
    return available_token_byte_count_;
  }
  [[nodiscard]] std::size_t active_lease_count() const noexcept { return active_lease_count_; }
  [[nodiscard]] std::size_t waiting_client_count() const noexcept { return waiter_count_; }

private:
  friend class SnapshotEgressLease;

  void refill(Clock::time_point now) noexcept;
  void enqueue_waiter(ClientId client_id);
  void remove_waiter_at(std::size_t index) noexcept;
  void commit_encoding(std::size_t reserved_byte_count, std::size_t encoded_byte_count);
  void release(std::size_t owned_byte_count, bool committed) noexcept;

  Clock::time_point last_refill_;
  std::size_t available_token_byte_count_;
  std::size_t refill_fraction_numerator_{0};
  std::size_t active_byte_count_{0};
  std::size_t active_lease_count_{0};
  std::array<ClientId, ServerLimits::kConcurrentWebSocketMaximumCount> waiters_{};
  std::size_t waiter_count_{0};
};

} // namespace blob_royale::server

#endif
