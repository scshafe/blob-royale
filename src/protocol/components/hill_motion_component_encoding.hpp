#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_HILL_MOTION_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_HILL_MOTION_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"

#include "components/hill_motion_component.hpp"

namespace blob_royale::protocol {

// Only committed velocity crosses this boundary, including zero after axis cancellation. Vector2
// owns finite/signed-zero bounds; neither the private schedule nor a sampled speed is published.
// related: docs/protocol/schema/v3/hill-motion-component.schema.json -- the closed wire shape.
template <> struct ComponentWireEncoding<simulation::HillMotion> {
  static void encode(const simulation::HillMotion& motion, const ComponentEncodingContext&,
                     ComponentObjectSink& sink) {
    sink.set_vector("velocity", motion.velocity.x(), motion.velocity.y());
  }
};

} // namespace blob_royale::protocol

#endif
