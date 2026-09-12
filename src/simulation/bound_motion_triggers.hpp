#ifndef BLOB_ROYALE_SIMULATION_BOUND_MOTION_TRIGGERS_HPP
#define BLOB_ROYALE_SIMULATION_BOUND_MOTION_TRIGGERS_HPP

#include "continuous_motion.hpp"
#include "live_motion_facts.hpp"
#include "world_event_registry.hpp"

#include <span>
#include <vector>

namespace blob_royale::simulation {

using LiveMotionTrigger = MotionTrigger<WorldEvent, LiveMotionFacts>;

// canonical: bound_motion_triggers -- tick-local owner of stable per-row facts loans.
// The declaration table must outlive this owner and its synchronous solve. A move transfers
// vector storage without invalidating facts references. Copying would leave cross-owner loans.
class BoundMotionTriggers final {
public:
  BoundMotionTriggers(const BoundMotionTriggers&) = delete;
  BoundMotionTriggers& operator=(const BoundMotionTriggers&) = delete;
  BoundMotionTriggers(BoundMotionTriggers&&) noexcept = default;
  BoundMotionTriggers& operator=(BoundMotionTriggers&&) noexcept = default;
  ~BoundMotionTriggers() = default;

  [[nodiscard]] std::span<const LiveMotionTrigger> rows() const& noexcept { return rows_; }
  [[nodiscard]] std::span<const LiveMotionTrigger> rows() const&& = delete;

private:
  friend class MotionTriggerTable;
  BoundMotionTriggers() = default;
  // Facts are completely constructed before any row borrows one; rows die first.
  std::vector<LiveMotionFacts> facts_;
  std::vector<LiveMotionTrigger> rows_;
};

} // namespace blob_royale::simulation

#endif
