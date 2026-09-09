#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_HILL_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_HILL_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"
#include "component_wire_bound.hpp"

#include "components/hill_component.hpp"

namespace blob_royale::protocol {

// The `hill` wire object, and the only publication of the hill: one circle, published where the
// entity that carries it is, exactly as the zone is (`docs/protocol/v2.md` § "snapshot"). Added in
// 2.5.
// related: docs/protocol/schema/v2/hill-component.schema.json -- the closed wire shape.
template <> struct ComponentWireEncoding<simulation::Hill> {
  static void encode(const simulation::Hill& hill, const ComponentEncodingContext&,
                     ComponentObjectSink& sink) {
    require_nonnegative_world_scalar(hill.radius, "snapshot.entities.components.hill.radius");
    sink.set_vector("center", hill.center.x(), hill.center.y());
    sink.set_number("radius", hill.radius);
  }
};

} // namespace blob_royale::protocol

#endif
