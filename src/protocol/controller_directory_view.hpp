#ifndef BLOB_ROYALE_PROTOCOL_CONTROLLER_DIRECTORY_VIEW_HPP
#define BLOB_ROYALE_PROTOCOL_CONTROLLER_DIRECTORY_VIEW_HPP

#include "controller_id.hpp"

#include <optional>
#include <string>

namespace blob_royale::protocol {

// What the wire `controllable` object says about one controller, beyond its id.
//
// Owned strings and not views, exactly as `runtime::ControllerPresentation` is owned and
// `ControllerDirectory::find` returns a copy: an encoder that held a reference into a directory
// entry would race the disconnect erasing it. Both values are already validated and bounded by the
// writer, so the encoder validates shape rather than re-deriving trust.
struct PublishedController final {
  std::string controller_kind;
  std::string display_name;

  friend bool operator==(const PublishedController&, const PublishedController&) = default;
};

// canonical: controller_directory_view -- the port through which the v3 encoder reads presentation.
//
// **One lookup, because a placement now carries its own controller.** This port briefly also
// answered "which controller held this already destroyed entity", for `match.placements`; that
// bridge is gone. `simulation::RoyalePlacement` records the `ControllerId` at the instant the mode
// eliminates the entity, so the encoder reads the link from the snapshot it is already encoding
// instead of asking a directory that has legitimately forgotten a closed session.
//
// **Why a port and not the directory itself.** Protocol v3 publishes `controller_kind` and
// `display_name` inside the wire `controllable` object, joined at the encoding boundary rather
// than stored in the `Controllable` component, so no proxy-supplied string ever enters the
// deterministic core (`docs/architecture/0004-gameplay-architecture.md`, amendment of 2026-09-06).
// The values live in `runtime::ControllerDirectory`, and `blob_protocol` may depend on
// `blob_simulation` and Boost.JSON and nothing else
// (`docs/architecture/0002-simulation-architecture.md` § target table). Naming `blob_runtime` here
// would invert the domain graph, so the dependency is inverted instead: `blob_protocol` declares
// the two lookups it needs, and `blob_server` -- which already depends on both targets -- passes an
// adapter over the runtime directory. The encoder therefore reads presentation without any
// `blob_protocol` translation unit ever seeing a runtime type.
//
// **The lookup is total.** `std::nullopt` is an ordinary answer, not a failure: a snapshot a
// reader still holds can outlive the session of a controller it names, and the directory erases an
// entry when its session closes. The encoder answers an absent presentation with the documented
// fallback (`protocol_v3_constants.hpp`) rather than failing a frame that every other peer is also
// waiting for.
// related: src/runtime/controller_directory.hpp -- the one production implementation's source.
// related: protocol_v3_json_encoding.hpp -- the only consumer.
class ControllerDirectoryView {
public:
  ControllerDirectoryView() = default;
  ControllerDirectoryView(const ControllerDirectoryView&) = default;
  ControllerDirectoryView(ControllerDirectoryView&&) = default;
  ControllerDirectoryView& operator=(const ControllerDirectoryView&) = default;
  ControllerDirectoryView& operator=(ControllerDirectoryView&&) = default;
  virtual ~ControllerDirectoryView() = default;

  // What a client should be told about this controller, or nullopt when the id names no open one.
  [[nodiscard]] virtual std::optional<PublishedController>
  find_controller(simulation::ControllerId controller) const = 0;
};

} // namespace blob_royale::protocol

#endif
