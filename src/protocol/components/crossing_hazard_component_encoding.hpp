#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_CROSSING_HAZARD_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_CROSSING_HAZARD_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"
#include "components/crossing_hazard_component.hpp"

namespace blob_royale::protocol {

// Presence identifies a bounded spawned crossing object; PhysicsBody/Lifetime own its values.
template <> struct ComponentWireEncoding<simulation::CrossingHazard> {
  static void encode(const simulation::CrossingHazard&, const ComponentEncodingContext&,
                     ComponentObjectSink&) {}
};

} // namespace blob_royale::protocol

#endif
