#ifndef BLOB_ROYALE_RUNTIME_SNAPSHOT_PUBLICATION_HPP
#define BLOB_ROYALE_RUNTIME_SNAPSHOT_PUBLICATION_HPP

#include "world_snapshot.hpp"

#include <atomic>
#include <memory>

namespace blob_royale::runtime {

class SimulationRuntime;

// canonical: snapshot_publication -- atomic reader access to one complete immutable snapshot.
class SnapshotPublication final {
public:
  // Publishes the required initial committed state before any reader can observe this object.
  explicit SnapshotPublication(simulation::WorldSnapshot initial_snapshot);

  SnapshotPublication(const SnapshotPublication&) = delete;
  SnapshotPublication(SnapshotPublication&&) = delete;
  SnapshotPublication& operator=(const SnapshotPublication&) = delete;
  SnapshotPublication& operator=(SnapshotPublication&&) = delete;
  ~SnapshotPublication() = default;

  // Retains the complete snapshot that was current at one atomic acquisition point.
  [[nodiscard]] std::shared_ptr<const simulation::WorldSnapshot> latest() const noexcept;

  // True only while the owning runtime is active after publishing at least one completed tick.
  [[nodiscard]] bool is_ready() const noexcept;

private:
  friend class SimulationRuntime;

  // Publishes an already-retained complete tick without a failure point inside the lifecycle lock.
  void publish(std::shared_ptr<const simulation::WorldSnapshot> snapshot) noexcept;
  void mark_not_ready() noexcept;

  std::atomic<std::shared_ptr<const simulation::WorldSnapshot>> latest_snapshot_;
  std::atomic<bool> is_ready_{false};
};

} // namespace blob_royale::runtime

#endif
