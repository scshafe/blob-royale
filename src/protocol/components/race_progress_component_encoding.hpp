#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_RACE_PROGRESS_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_RACE_PROGRESS_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"

#include "components/race_progress_component.hpp"

namespace blob_royale::protocol {

// The `race_progress` wire object: the next ordered gate, or the gate count when finished.
// Added in 2.5; publishes exactly the stored value, including zero for a racer returning to grid.
// related: docs/protocol/schema/v2/race-progress-component.schema.json -- the closed wire shape.
template <> struct ComponentWireEncoding<simulation::RaceProgress> {
  static void encode(const simulation::RaceProgress& progress, const ComponentEncodingContext&,
                     ComponentObjectSink& sink) {
    sink.set_unsigned("next_checkpoint", progress.next_checkpoint);
  }
};

} // namespace blob_royale::protocol

#endif
