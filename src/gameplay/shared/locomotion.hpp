#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_LOCOMOTION_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_LOCOMOTION_HPP

#include "fixed_delta.hpp"
#include "vector2.hpp"

#include <cstddef>
#include <optional>

namespace blob_royale::gameplay {

// Initial value plus at most eight directed nextafter corrections on each of the two scales:
// nine radial targets, each with at most nine acceleration scales. This is a bounded numerical
// policy, not an epsilon admission band or a native performance claim.
inline constexpr std::size_t kMaximumLocomotionRoundingStepCount = 8;

// canonical: normalized_thrust_intent -- the single magnitude clamp retained between commands.
// Preserve sqrt(x*x+y*y), the reciprocal branch, then component*scale. Subunit analog intent
// keeps its magnitude. Vector2 owns finite-component validation and signed-zero canonicalization.
//
// **This is a clamp, not a normalizer, and the two are not interchangeable.** Its scale is exactly
// `1.0` whenever the magnitude is at most one, which is the whole point for an analog throttle:
// half a stick is half the acceleration. A fixed-gain activation needs the opposite -- a direction
// alone, with the strength supplied by the server -- and must use `unit_direction` below. Routing
// an activation through this function would make pointer distance into strength.
[[nodiscard]] simulation::Vector2 normalized_thrust_intent(const simulation::Vector2& direction);

// canonical: unit_direction -- the one true normalization, for a fixed-gain activation.
//
// **Why charge normalizes where thrust clamps.** `normalized_thrust_intent` above is the tree's
// single magnitude *clamp* and is right for a held analog throttle: subunit intent keeps its
// magnitude, so a half-pressed stick is half the acceleration. A one-shot activation has a stated
// fixed gain -- ADR 0008's "Initial gain: 0.75 times the current normal movement ceiling" -- and
// the owner's decision is that its strength is authoritative and server-side. Routed through the
// clamp, a client sending `{"x":0.5,"y":0}` -- legal under the per-component unit bound the wire
// applies -- would receive half the burst, making pointer distance into strength: the inverse of
// the boundary `ShieldCommand` draws between intent and effect, and a direct contradiction of both
// the ADR and the owner's decision. So charge divides by the magnitude in every case, including
// the subunit one, and the direction a client sends decides only *where*.
//
// **It never throws, and returning an optional is the reason why.** Its one caller is admission
// inside a `kPreKernel` system, and nothing in `AbilitySystem::apply` may throw for a world a
// client can reach: an escaping throw stops the runtime worker permanently and kills the room
// (`shared/ability_system.hpp`). Every unusable direction is therefore `std::nullopt` and the
// caller turns it into a silent refusal.
//
// **The refused band is the whole non-constructible band, not just exact zero.** `{"x":1e-200,
// "y":0}` passes the decoder and `InputBatch`: its squared magnitude underflows to zero, so the
// computed magnitude is zero and a reciprocal would be infinite. Nullopt covers a magnitude that
// is not finite, a magnitude of zero, and divided components that are not finite or leave the
// `Vector2` component domain. The last of those is a structural guard rather than a reachable
// case -- a `Vector2` argument is already finite and inside the domain, so `x*x + y*y` cannot
// overflow and the quotient cannot exceed one by more than a rounding step -- and it is written
// out anyway because the alternative to a free refusal here is a thrown tick there.
//
// It computes the magnitude with the written-out `sqrt(x*x + y*y)` this codebase uses everywhere
// for cross-toolchain agreement, and it **divides** by that magnitude rather than multiplying by a
// reciprocal: one rounding instead of two, and no infinity to reason about at the small end.
// Vector2 owns finite-component validation and signed-zero canonicalization, as above.
// related: ../../simulation/commands/charge_command.hpp -- the direction a client sends.
// related: shared/ability_system.hpp -- the admission that turns nullopt into a silent refusal.
[[nodiscard]] std::optional<simulation::Vector2>
unit_direction(const simulation::Vector2& direction);

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
