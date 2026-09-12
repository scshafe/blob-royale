#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_STUN_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_STUN_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"
#include "protocol_encoding_error.hpp"

#include "components/stun_component.hpp"

namespace blob_royale::protocol {

// Both absolute endpoints are public to every reader. TickWindow guarantees safe integers;
// status windows additionally require a positive activation and a nonempty interval.
// related: docs/protocol/schema/v3/stun-component.schema.json -- the closed wire shape.
template <> struct ComponentWireEncoding<simulation::Stun> {
  static void encode(const simulation::Stun& stun, const ComponentEncodingContext&,
                     ComponentObjectSink& sink) {
    const auto activation = stun.window.activation_tick().value();
    const auto expiry = stun.window.expiry_tick().value();
    if (activation == 0 || expiry <= activation) {
      throw ProtocolEncodingError(ProtocolEncodingErrorCode::kComponentValueOutOfRange,
                                  "stun.window", "stun requires positive activation before expiry");
    }
    sink.set_unsigned("activation_tick", activation);
    sink.set_unsigned("expiry_tick", expiry);
  }
};

} // namespace blob_royale::protocol

#endif
