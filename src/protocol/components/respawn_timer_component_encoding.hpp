#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_RESPAWN_TIMER_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_RESPAWN_TIMER_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"

#include "components/respawn_timer_component.hpp"

namespace blob_royale::protocol {

// The `respawn_timer` wire object: the committed ticks left before an out-of-play entity is offered
// a seat again. An absent component means "not respawning" on the wire exactly as it does in the
// world, so the encoder publishes the entry the store holds and synthesizes none. Added in 2.5.
// related: docs/protocol/schema/v2/respawn-timer-component.schema.json -- the closed wire shape.
template <> struct ComponentWireEncoding<simulation::RespawnTimer> {
  static void encode(const simulation::RespawnTimer& timer, const ComponentEncodingContext&,
                     ComponentObjectSink& sink) {
    sink.set_unsigned("ticks_remaining", timer.ticks_remaining);
  }
};

} // namespace blob_royale::protocol

#endif
