#ifndef BLOB_ROYALE_SERVER_SNAPSHOT_DELIVERY_STATE_HPP
#define BLOB_ROYALE_SERVER_SNAPSHOT_DELIVERY_STATE_HPP

#include "world_snapshot.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace blob_royale::server {

struct SnapshotDelivery final {
  std::shared_ptr<const simulation::WorldSnapshot> snapshot;
  std::uint64_t message_sequence;
};

// canonical: snapshot_delivery_state -- one active write and one latest-state pending reference.
// This transport state never owns mutable simulation data and never queues historical snapshots.
class SnapshotDeliveryState final {
public:
  SnapshotDeliveryState() = default;

  // Observes one presentation slot and retains/replaces at most one pending latest reference.
  // This never starts a logical write or consumes a message sequence.
  void observe(std::shared_ptr<const simulation::WorldSnapshot> latest_snapshot);

  // Starts one logical write only after the caller owns global encoding/egress capacity.
  [[nodiscard]] std::optional<SnapshotDelivery> begin_active_write();

  // Commits one successful active write. A pending snapshot remains dormant until a later slot.
  void complete_active_write();

  // Discards active and pending state after transport failure or shutdown.
  void discard() noexcept;

  // Drops stale pending state while allowing the one active transport write to finish.
  void discard_pending() noexcept { pending_snapshot_.reset(); }

  [[nodiscard]] bool write_active() const noexcept { return active_snapshot_ != nullptr; }
  [[nodiscard]] bool has_pending_snapshot() const noexcept { return pending_snapshot_ != nullptr; }
  [[nodiscard]] bool ready_to_write() const noexcept {
    return active_snapshot_ == nullptr && pending_snapshot_ != nullptr;
  }
  [[nodiscard]] std::optional<std::uint64_t> last_delivered_tick() const noexcept {
    return last_delivered_tick_;
  }
  [[nodiscard]] std::uint64_t delivered_message_count() const noexcept { return message_sequence_; }

private:
  [[nodiscard]] bool is_newer_than_retained(std::uint64_t tick_sequence) const noexcept;

  std::shared_ptr<const simulation::WorldSnapshot> active_snapshot_;
  std::shared_ptr<const simulation::WorldSnapshot> pending_snapshot_;
  std::optional<std::uint64_t> last_delivered_tick_;
  std::uint64_t message_sequence_{0};
};

} // namespace blob_royale::server

#endif
