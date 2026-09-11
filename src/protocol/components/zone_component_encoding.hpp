#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_ZONE_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_ZONE_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"
#include "component_wire_bound.hpp"

#include "components/zone_component.hpp"

namespace blob_royale::protocol {

// The `zone` wire object, and **the only publication of the safe zone**. It is not duplicated into
// the `match` section: publishing one circle twice would give a client two sources for it and a way
// to disagree with itself (`docs/architecture/0005-royale-mode.md` § "Match section fields";
// `docs/protocol/v3.md` § "snapshot").
// related: docs/protocol/schema/v3/zone-component.schema.json -- the closed wire shape.
template <> struct ComponentWireEncoding<simulation::Zone> {
  static void encode(const simulation::Zone& zone, const ComponentEncodingContext&,
                     ComponentObjectSink& sink) {
    require_nonnegative_world_scalar(zone.radius, "snapshot.entities.components.zone.radius");
    sink.set_vector("center", zone.center.x(), zone.center.y());
    sink.set_number("radius", zone.radius);
  }
};

} // namespace blob_royale::protocol

#endif
