#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_SCORE_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_SCORE_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"

#include "components/score_component.hpp"

namespace blob_royale::protocol {

// The `score` wire object: one signed scoreboard cell, so a penalty needs no second kind.
// related: docs/protocol/schema/v2/score-component.schema.json -- the closed wire shape.
template <> struct ComponentWireEncoding<simulation::Score> {
  static void encode(const simulation::Score& score, const ComponentEncodingContext&,
                     ComponentObjectSink& sink) {
    sink.set_signed("points", score.points);
  }
};

} // namespace blob_royale::protocol

#endif
