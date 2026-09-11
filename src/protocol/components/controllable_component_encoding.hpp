#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_CONTROLLABLE_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_CONTROLLABLE_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"
#include "controller_directory_view.hpp"
#include "protocol_v3_constants.hpp"

#include "components/controllable_component.hpp"

#include <optional>
#include <string>

namespace blob_royale::protocol {

// The `controllable` wire object: the durable controller id from the component, and the controller
// kind and display name **joined from the directory**.
//
// The join is the whole point of this kind's encoder. `Controllable` carries only the
// `ControllerId`, because a display name is proxy-supplied and a tick that could read one would
// make the simulation a function of a header a proxy wrote
// (`docs/architecture/0004-gameplay-architecture.md`, amendment of 2026-09-06). The two
// presentation values therefore meet the wire here, at the encoding boundary, and nowhere earlier.
//
// **`commands_this_tick` is not published and cannot be**: `ComponentPublication<Controllable>`
// already stripped it before this snapshot existed, so the leak protocol v3 withholds is closed at
// the simulation boundary rather than remembered here
// (`src/simulation/components/controllable_component.hpp`).
//
// An absent directory entry yields the documented fallback rather than a failed frame; see
// `protocol_v3_constants.hpp` for why absence is ordinary.
// related: docs/protocol/schema/v3/controllable-component.schema.json -- the closed wire shape.
template <> struct ComponentWireEncoding<simulation::Controllable> {
  static void encode(const simulation::Controllable& controllable,
                     const ComponentEncodingContext& context, ComponentObjectSink& sink) {
    const std::optional<PublishedController> published =
        context.directory->find_controller(controllable.controller_id);

    sink.set_unsigned("controller_id", controllable.controller_id.value());
    if (published.has_value()) {
      sink.set_string("controller_kind", published->controller_kind);
      sink.set_string("display_name", published->display_name);
      return;
    }
    sink.set_string("controller_kind", kUnknownControllerKind);
    sink.set_string("display_name", std::string{kFallbackDisplayNamePrefix} +
                                        std::to_string(context.entity.value()));
  }
};

} // namespace blob_royale::protocol

#endif
