#ifndef BLOB_ROYALE_SIMULATION_MOTION_BODY_ENVELOPE_HPP
#define BLOB_ROYALE_SIMULATION_MOTION_BODY_ENVELOPE_HPP

#include "arena_bounds.hpp"
#include "physics_body.hpp"

#include <cstdint>
#include <optional>

namespace blob_royale::simulation {

enum class MotionBodyEnvelopeViolation : std::uint8_t {
  kStaticOutsideEnvelope,
  kFoldingBodyDoesNotFitEnvelope,
};

// canonical: motion_body_envelope -- classifies one validated body's motion-envelope legality.
// configured_radius is a validated, positive configured radius, used only for an undeclared body
// radius. A present violation preserves the reviewed solver's static/folding error distinction;
// absence means legal. Static centers must be inside even with kCross. Dynamic crossing bodies
// are exempt; folding bodies may initially overlap walls but must fit each axis, with zero
// velocity on exactly zero-span axes. This never relocates the body or checks spawn clearance.
// Shared by the live solver, simulation admission/commit and spatial index after the verified
// Step 16 promotion. Related: motion_body_envelope_tests.cpp's independent frozen admission.
[[nodiscard]] std::optional<MotionBodyEnvelopeViolation>
motion_body_envelope_violation(const PhysicsBody& body, const ArenaBounds& bounds,
                               double configured_radius) noexcept;

} // namespace blob_royale::simulation

#endif
