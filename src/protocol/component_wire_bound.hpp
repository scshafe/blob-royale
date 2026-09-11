#ifndef BLOB_ROYALE_PROTOCOL_COMPONENT_WIRE_BOUND_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENT_WIRE_BOUND_HPP

#include "protocol_constants.hpp"
#include "protocol_encoding_error.hpp"

#include <string>
#include <string_view>

namespace blob_royale::protocol {

// canonical: component_wire_bound -- the scalar bounds the v3 schemas narrow past the simulation's.
//
// `Vector2` already guarantees every physical component is finite and within
// `kMaximumPhysicalComponentMagnitude`, which is exactly `finite_world_scalar`, so most values need
// no check here. Two do: `physics-body-component.schema.json` types `radius` as
// `positive_world_scalar` (exclusive minimum zero) and `mass` as `nonnegative_world_scalar`, and
// `zone-component.schema.json` types `radius` as `nonnegative_world_scalar` -- narrowings the world
// does not enforce, because `PhysicsBody::kUndeclaredRadius` is zero by design.
//
// The checks live beside the encoders that publish the values rather than in the snapshot encoder,
// so a kind's wire bounds are declared with the kind. They **fail closed**: the encoder never
// clamps a value to fit and never substitutes one, because a substituted radius is a body drawn at
// a size nothing in the simulation used (`docs/protocol/v3.md` § "Limits";
// § "Field dictionary and invariants").
// related: components/physics_body_component_encoding.hpp -- the first caller.
// related: components/zone_component_encoding.hpp -- the second.

inline void require_positive_world_scalar(const double value, const std::string_view context) {
  if (value > 0.0 && value <= kMaximumFiniteWorldScalar) {
    return;
  }
  throw ProtocolEncodingError{ProtocolEncodingErrorCode::kComponentValueOutOfRange,
                              std::string{context},
                              "value must be greater than zero and no greater than 1e12"};
}

inline void require_nonnegative_world_scalar(const double value, const std::string_view context) {
  if (value >= 0.0 && value <= kMaximumFiniteWorldScalar) {
    return;
  }
  throw ProtocolEncodingError{ProtocolEncodingErrorCode::kComponentValueOutOfRange,
                              std::string{context},
                              "value must be zero or greater and no greater than 1e12"};
}

} // namespace blob_royale::protocol

#endif
