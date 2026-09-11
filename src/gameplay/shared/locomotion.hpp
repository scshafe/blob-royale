#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_LOCOMOTION_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_LOCOMOTION_HPP

#include "fixed_delta.hpp"
#include "vector2.hpp"

#include <cstddef>

namespace blob_royale::gameplay {

// Initial value plus at most eight directed nextafter corrections on each of the two scales:
// nine radial targets, each with at most nine acceleration scales. This is a bounded numerical
// policy, not an epsilon admission band or a native performance claim.
inline constexpr std::size_t kMaximumLocomotionRoundingStepCount = 8;

// canonical: normalized_thrust_intent -- the single magnitude clamp retained between commands.
// Preserve sqrt(x*x+y*y), the reciprocal branch, then component*scale. Subunit analog intent
// keeps its magnitude. Vector2 owns finite-component validation and signed-zero canonicalization.
[[nodiscard]] simulation::Vector2 normalized_thrust_intent(const simulation::Vector2& direction);

// canonical: thrust_acceleration_from_intent -- scales already-normalized intent, never reclamps.
// The two component multiplications complete the old (component*scale)*acceleration order.
// This arithmetic keeps the legacy standalone helper's scalar domain; MovementTuning separately
// validates authored/runtime settings. Vector2 rejects any unrepresentable result as before.
[[nodiscard]] simulation::Vector2 thrust_acceleration_from_intent(const simulation::Vector2& intent,
                                                                  double acceleration);

// canonical: normal_propulsion_limit -- constrains a requested finite Euler step, not velocity.
// Uses canonical integrate_accelerated_velocity and written squared norms throughout. Return the
// original acceleration exactly when its endpoint is within max(normal_top_speed^2, velocity.dot
// velocity). Otherwise project that endpoint radially, and return a non-amplifying acceleration
// whose canonical endpoint meets the same computed bound. Drag and collision momentum are never
// changed here. This is a binary64 postcondition, not an exact-real magnitude certificate.
//
// FIRST computed projection == velocity is an explicit quantized-projection identity and returns
// zero; it does not prove that the real requested direction was radial. If that initial target
// differs, any later zero acceleration or integrated identity fails immediately, never coasts or
// continues shrinking into artificial braking. Bounded rounding exhaustion raises
// GAMEPLAY.LOCOMOTION_PRECISION_LOST. Invalid normal speed uses MovementTuning's scalar error.
//
// Vector2's +/-1e12 component domain and canonical integration's materialization failures remain.
// In particular a requested endpoint outside that domain fails before projection; this function
// does not supply an alternate raw integrator to salvage a physically unrepresentable request.
[[nodiscard]] simulation::Vector2
limit_normal_propulsion(const simulation::Vector2& velocity,
                        const simulation::Vector2& requested_acceleration, double normal_top_speed,
                        simulation::FixedDelta delta);

} // namespace blob_royale::gameplay

#endif
