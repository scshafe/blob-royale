#include "motion_triggers.hpp"

#include "simulation_tolerance.hpp"
#include "swept_geometry.hpp"
#include "terrain_queries.hpp"

#include <algorithm>
#include <cmath>

namespace blob_royale::simulation {
namespace {

void validate_window(const MotionTriggerWindow& window) {
  if (window.eligible_from < window.motion.begin) {
    detail::fail_motion(SimulationValidationCode::kContinuousMotionInvalidInput,
                        "trigger eligibility precedes its motion epoch");
  }
}

[[nodiscard]] double circle_distance(const Vector2& first, const Vector2& second) {
  const double x = first.x() - second.x();
  const double y = first.y() - second.y();
  return std::sqrt(x * x + y * y);
}

} // namespace

std::optional<MotionTriggerProposal> support_loss_motion_trigger(const TerrainDefinition& terrain,
                                                                 const MotionTriggerWindow& window,
                                                                 MotionQueryBudget& budget) {
  validate_window(window);
  // Charge shared public swept-query invocations: four line planes, one circle per hole, and
  // one capsule per segment. A capsule invocation includes its bounded internal root work.
  // This deterministic upper count is charged before zero-motion/endpoint fast returns.
  std::size_t root_count = 4 + terrain.holes().size();
  for (const auto& corridor : terrain.corridors()) {
    root_count += corridor.points().size() - 1;
  }
  budget.roots(root_count);
  const auto intervals = swept_support_intervals(terrain, window.motion.subject.body.position(),
                                                 window.motion.displacement);
  for (const auto& interval : intervals) {
    const auto begin = map_motion_time(interval.begin, window.motion.begin, MotionTime::end());
    const auto end = map_motion_time(interval.end, window.motion.begin, MotionTime::end());
    if (begin <= window.eligible_from && window.eligible_from <= end) {
      return end < MotionTime::end()
                 ? std::optional{MotionTriggerProposal{end, MotionEventPriority::kSupportLoss}}
                 : std::nullopt;
    }
  }
  return MotionTriggerProposal{window.eligible_from, MotionEventPriority::kSupportLoss};
}

std::optional<MotionTriggerProposal>
ordered_gate_motion_trigger(const std::span<const MotionCircleGate> gates,
                            const std::uint64_t cursor, const MotionTriggerWindow& window,
                            MotionQueryBudget& budget) {
  validate_window(window);
  detail::require_motion_budget(gates.size(), kMaximumMotionTriggerCursorValue,
                                "motion ordered-gate declaration budget exhausted");
  if (cursor >= gates.size()) {
    return std::nullopt;
  }
  const auto& gate = gates[static_cast<std::size_t>(cursor)];
  if (!std::isfinite(gate.radius) || gate.radius <= 0.0 ||
      gate.radius > kMaximumPhysicalComponentMagnitude) {
    detail::fail_motion(SimulationValidationCode::kContinuousMotionInvalidInput,
                        "motion gate radius must be positive, finite, and physically bounded");
  }
  const auto& start = window.motion.subject.body.position();
  const auto finish = start + window.motion.displacement;
  const double radius = gate.radius + kPositionTolerance;
  const bool start_inside = circle_distance(start, gate.center) <= radius;
  const bool finish_inside = circle_distance(finish, gate.center) <= radius;
  budget.roots(1);
  const auto roots =
      swept_circle_boundary_roots(start, window.motion.displacement, gate.center, radius);
  MotionTime begin = MotionTime::start();
  MotionTime end = MotionTime::end();
  if (!start_inside || !finish_inside) {
    if (roots.empty()) {
      if (start_inside != finish_inside) {
        detail::fail_motion(SimulationValidationCode::kContinuousMotionPrecisionLost,
                            "gate occupancy changed without a representable boundary root");
      }
      return std::nullopt;
    }
    if (!start_inside) {
      begin = roots.times().front();
    }
    if (!finish_inside) {
      end = roots.times().back();
    }
  }
  const auto mapped_begin = map_motion_time(begin, window.motion.begin, MotionTime::end());
  const auto mapped_end = map_motion_time(end, window.motion.begin, MotionTime::end());
  const auto eligible = std::max(mapped_begin, window.eligible_from);
  return eligible <= mapped_end
             ? std::optional{MotionTriggerProposal{eligible, MotionEventPriority::kCheckpoint}}
             : std::nullopt;
}

} // namespace blob_royale::simulation
