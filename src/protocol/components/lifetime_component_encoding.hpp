#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_LIFETIME_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_LIFETIME_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"

#include "components/lifetime_component.hpp"

namespace blob_royale::protocol {

// The `lifetime` wire object: a self-expiring entity's remaining committed ticks.
// related: docs/protocol/schema/v3/lifetime-component.schema.json -- the closed wire shape.
template <> struct ComponentWireEncoding<simulation::Lifetime> {
  static void encode(const simulation::Lifetime& lifetime, const ComponentEncodingContext&,
                     ComponentObjectSink& sink) {
    sink.set_unsigned("ticks_remaining", lifetime.ticks_remaining);
  }
};

} // namespace blob_royale::protocol

#endif
