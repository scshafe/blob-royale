#ifndef BLOB_ROYALE_SIMULATION_MOTION_TRIGGER_TABLE_HPP
#define BLOB_ROYALE_SIMULATION_MOTION_TRIGGER_TABLE_HPP

#include "bound_motion_triggers.hpp"
#include "motion_trigger_policy.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace blob_royale::simulation {

// canonical: motion_trigger_table -- independently owned eighth mode declaration.
// A table may outlive its mode. Authored order breaks binding ties; event order is exclusively
// MotionEventKey's order. Registration does not run geometry or introduce another event engine.
class MotionTriggerTable final {
public:
  struct Declaration final {
    std::uint64_t feature_base;
    std::uint64_t cursor_limit;
    std::unique_ptr<const MotionTriggerPolicy> policy;
  };

  // Rejects null policies, excessive declaration count and invalid feature/cursor ranges.
  [[nodiscard]] static MotionTriggerTable create(std::vector<Declaration> declarations);
  [[nodiscard]] static MotionTriggerTable empty();
  MotionTriggerTable(const MotionTriggerTable&) = delete;
  MotionTriggerTable& operator=(const MotionTriggerTable&) = delete;
  MotionTriggerTable(MotionTriggerTable&&) noexcept = default;
  MotionTriggerTable& operator=(MotionTriggerTable&&) noexcept = default;
  ~MotionTriggerTable() = default;

  // Binds in ascending body ID then declaration order. Rejects invalid limits, excessive body/
  // row counts or invalid initial cursors. No binding survives an exception. Per-entity feature
  // overlap is checked by the canonical solver. Temporary tables cannot produce dangling loans.
  [[nodiscard]] BoundMotionTriggers bind(const GameWorld& world, const TickContext& context,
                                         const MotionLimits& limits = {}) const&;
  [[nodiscard]] BoundMotionTriggers bind(const GameWorld&, const TickContext&,
                                         const MotionLimits& = {}) const&& = delete;
  [[nodiscard]] std::size_t size() const noexcept { return declarations_.size(); }

private:
  explicit MotionTriggerTable(std::vector<Declaration> declarations) noexcept
      : declarations_(std::move(declarations)) {}
  std::vector<Declaration> declarations_;
};

} // namespace blob_royale::simulation

#endif
