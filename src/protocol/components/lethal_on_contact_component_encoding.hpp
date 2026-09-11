#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_LETHAL_ON_CONTACT_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_LETHAL_ON_CONTACT_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"

#include "components/lethal_on_contact_component.hpp"

namespace blob_royale::protocol {

// The `lethal_on_contact` wire object, which is **empty**, and is the first one that is.
//
// The component's presence is its entire message, so the object carries no member: the client
// learns "this entity kills on touch" from the key existing under `components`, exactly as the
// simulation learns it from the store holding the entity. A synthetic `{"lethal": true}` was
// rejected because it is a member that can only ever hold one value -- an entity that is not lethal
// does not carry the kind at all -- and a field whose only possible value is `true` is a fact the
// client would have to trust rather than read.
//
// Writing nothing is therefore complete rather than unfinished, which is what the empty body says
// and what `additionalProperties: false` with no `required` pins on the wire. The sink is unnamed
// here for the same reason the value is: this encoder consults neither.
// related: docs/protocol/schema/v3/lethal-on-contact-component.schema.json -- the closed wire
// shape.
template <> struct ComponentWireEncoding<simulation::LethalOnContact> {
  static void encode(const simulation::LethalOnContact&, const ComponentEncodingContext&,
                     ComponentObjectSink&) {}
};

} // namespace blob_royale::protocol

#endif
