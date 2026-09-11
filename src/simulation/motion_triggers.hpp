#ifndef BLOB_ROYALE_SIMULATION_MOTION_TRIGGERS_HPP
#define BLOB_ROYALE_SIMULATION_MOTION_TRIGGERS_HPP

#include "continuous_motion.hpp"
#include "terrain_definition.hpp"

namespace blob_royale::simulation {

struct MotionCircleGate final {
  Vector2 center;
  // Authored radius. The query applies the canonical closed occupancy position tolerance once.
  double radius;
};

// canonical: support_loss_motion_trigger -- next loss on an anchored actual linear epoch.
// Geometry and mapped roots are retained across the eligibility window; no shortened re-sweep.
// Already-void eligibility returns that instant. Exact supported final rims do not fall.
// The caller supplies the narrow typed elimination/despawn consequence and termination response.
// Numeric domain: start, displacement, and their full unhandled endpoint must be representable
// Vector2 values, as required by swept_support_intervals. An earlier potential event does not
// enlarge that shared query domain; out-of-domain sweeps fail before publishing a result.
[[nodiscard]] std::optional<MotionTriggerProposal>
support_loss_motion_trigger(const TerrainDefinition& terrain, const MotionTriggerWindow& window,
                            MotionQueryBudget& budget);

// canonical: ordered_gate_motion_trigger -- first eligible closed occupancy of the next gate.
// Cursor names the only eligible gate; cursor>=gate count means complete. Inputs are bounded by
// kMaximumMotionTriggerCursorValue; radii must be positive finite physical values. A response
// advances the cursor and terminates at finish. Existing occupancy counts, including tangency.
// The full unhandled endpoint must be a representable Vector2, as for the support helper.
[[nodiscard]] std::optional<MotionTriggerProposal>
ordered_gate_motion_trigger(std::span<const MotionCircleGate> gates, std::uint64_t cursor,
                            const MotionTriggerWindow& window, MotionQueryBudget& budget);

} // namespace blob_royale::simulation

#endif
