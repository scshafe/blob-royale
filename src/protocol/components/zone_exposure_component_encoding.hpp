#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_ZONE_EXPOSURE_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_ZONE_EXPOSURE_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"

#include "components/zone_exposure_component.hpp"

namespace blob_royale::protocol {

// The `zone_exposure` wire object. An absent component reads as zero on the wire exactly as it does
// in the world, so the encoder publishes the entry the store holds and synthesizes none.
// related: docs/protocol/schema/v2/zone-exposure-component.schema.json -- the closed wire shape.
template <> struct ComponentWireEncoding<simulation::ZoneExposure> {
  static void encode(const simulation::ZoneExposure& exposure, const ComponentEncodingContext&,
                     ComponentObjectSink& sink) {
    sink.set_unsigned("outside_ticks", exposure.outside_ticks);
  }
};

} // namespace blob_royale::protocol

#endif
