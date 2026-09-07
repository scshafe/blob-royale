#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_TEAM_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_TEAM_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"

#include "components/team_component.hpp"

namespace blob_royale::protocol {

// The `team` wire object. An entity without this component is unaligned, so the wire has no
// reserved "no team" value and absence carries the whole meaning.
// related: docs/protocol/schema/v2/team-component.schema.json -- the closed wire shape.
template <> struct ComponentWireEncoding<simulation::Team> {
  static void encode(const simulation::Team& team, const ComponentEncodingContext&,
                     ComponentObjectSink& sink) {
    sink.set_unsigned("team_id", team.team_id.value());
  }
};

} // namespace blob_royale::protocol

#endif
