#include "snapshot_publication.hpp"

#include <memory>
#include <utility>

namespace blob_royale::runtime {

SnapshotPublication::SnapshotPublication(simulation::WorldSnapshot initial_snapshot)
    : latest_snapshot_(
          std::make_shared<const simulation::WorldSnapshot>(std::move(initial_snapshot))) {}

std::shared_ptr<const simulation::WorldSnapshot> SnapshotPublication::latest() const noexcept {
  return latest_snapshot_.load(std::memory_order_acquire);
}

bool SnapshotPublication::is_ready() const noexcept {
  return is_ready_.load(std::memory_order_acquire);
}

void SnapshotPublication::publish(
    std::shared_ptr<const simulation::WorldSnapshot> snapshot) noexcept {
  latest_snapshot_.store(std::move(snapshot), std::memory_order_release);
  is_ready_.store(true, std::memory_order_release);
}

void SnapshotPublication::mark_not_ready() noexcept {
  is_ready_.store(false, std::memory_order_release);
}

} // namespace blob_royale::runtime
