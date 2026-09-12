#include "motion_body_envelope.hpp"

namespace blob_royale::simulation {

std::optional<MotionBodyEnvelopeViolation>
motion_body_envelope_violation(const PhysicsBody& body, const ArenaBounds& bounds,
                               const double configured_radius) noexcept {
  const double radius = effective_radius(body, configured_radius);
  if (body.is_static()) {
    if (!bounds.contains(body.position())) {
      return MotionBodyEnvelopeViolation::kStaticOutsideEnvelope;
    }
  } else if (!body.crosses_bounds()) {
    if (!bounds.contains(body.position()) || bounds.width() < 2.0 * radius ||
        bounds.height() < 2.0 * radius ||
        (bounds.width() == 2.0 * radius && body.velocity().x() != 0.0) ||
        (bounds.height() == 2.0 * radius && body.velocity().y() != 0.0)) {
      return MotionBodyEnvelopeViolation::kFoldingBodyDoesNotFitEnvelope;
    }
  }
  return std::nullopt;
}

} // namespace blob_royale::simulation
