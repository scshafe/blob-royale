#include "snapshot_delivery_state.hpp"

#include "game_server_error.hpp"
#include "protocol_constants.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace blob_royale::server {

bool SnapshotDeliveryState::is_newer_than_retained(
    const std::uint64_t tick_sequence) const noexcept {
  std::uint64_t newest_retained_tick = last_delivered_tick_.value_or(0);
  if (active_snapshot_ != nullptr) {
    newest_retained_tick =
        std::max(newest_retained_tick, active_snapshot_->tick_sequence().value());
  }
  if (pending_snapshot_ != nullptr) {
    newest_retained_tick =
        std::max(newest_retained_tick, pending_snapshot_->tick_sequence().value());
  }
  return tick_sequence > newest_retained_tick;
}

void SnapshotDeliveryState::observe(
    std::shared_ptr<const simulation::WorldSnapshot> latest_snapshot) {
  if (latest_snapshot == nullptr) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed, "snapshot_delivery.latest",
                          "publication returned a null snapshot"};
  }
  const std::uint64_t latest_tick = latest_snapshot->tick_sequence().value();
  if (latest_tick == 0 || latest_tick > protocol::kMaximumSafeInteger) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed,
                          "snapshot_delivery.tick_sequence",
                          "publication exposed an invalid committed tick"};
  }

  if (write_active()) {
    if (is_newer_than_retained(latest_tick)) {
      pending_snapshot_ = std::move(latest_snapshot);
    }
    return;
  }

  if (pending_snapshot_ != nullptr && latest_tick > pending_snapshot_->tick_sequence().value()) {
    pending_snapshot_ = std::move(latest_snapshot);
  } else if (pending_snapshot_ == nullptr && is_newer_than_retained(latest_tick)) {
    pending_snapshot_ = std::move(latest_snapshot);
  }
}

std::optional<SnapshotDelivery> SnapshotDeliveryState::begin_active_write() {
  if (!ready_to_write()) {
    return std::nullopt;
  }
  active_snapshot_ = std::move(pending_snapshot_);
  pending_snapshot_.reset();
  if (message_sequence_ == protocol::kMaximumSafeInteger) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed,
                          "snapshot_delivery.message_sequence",
                          "per-connection message sequence exhausted"};
  }
  ++message_sequence_;
  return SnapshotDelivery{active_snapshot_, message_sequence_};
}

void SnapshotDeliveryState::complete_active_write() {
  if (active_snapshot_ == nullptr) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed,
                          "snapshot_delivery.active_write",
                          "write completion occurred without an active snapshot"};
  }
  last_delivered_tick_ = active_snapshot_->tick_sequence().value();
  active_snapshot_.reset();
}

void SnapshotDeliveryState::discard() noexcept {
  active_snapshot_.reset();
  pending_snapshot_.reset();
}

} // namespace blob_royale::server
