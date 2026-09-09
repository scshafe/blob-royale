#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_HILL_PRESENCE_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_HILL_PRESENCE_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"

#include "components/hill_presence_component.hpp"

namespace blob_royale::protocol {

// The `hill_presence` wire object. An absent component reads as zero on the wire exactly as it does
// in the world, so the encoder publishes the entry the store holds and synthesizes none. Added in
// 2.5.
// related: docs/protocol/schema/v2/hill-presence-component.schema.json -- the closed wire shape.
template <> struct ComponentWireEncoding<simulation::HillPresence> {
  static void encode(const simulation::HillPresence& presence, const ComponentEncodingContext&,
                     ComponentObjectSink& sink) {
    sink.set_unsigned("inside_ticks", presence.inside_ticks);
  }
};

} // namespace blob_royale::protocol

#endif
