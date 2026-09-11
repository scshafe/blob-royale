#ifndef BLOB_ROYALE_SERVER_RUNTIME_CONTROLLER_DIRECTORY_VIEW_HPP
#define BLOB_ROYALE_SERVER_RUNTIME_CONTROLLER_DIRECTORY_VIEW_HPP

#include "controller_directory.hpp"
#include "controller_directory_view.hpp"

#include "controller_id.hpp"

#include <optional>

namespace blob_royale::server {

// canonical: runtime_controller_directory_view -- the one production implementation of the v3
// encoder's presentation port.
//
// **It lives here because `blob_server` is the only target allowed to depend on both.**
// `blob_protocol` publishes `controller_kind` and `display_name` inside the wire `controllable`
// object but may depend on `blob_simulation` and Boost.JSON and nothing else
// (`docs/architecture/0002-simulation-architecture.md` § target table), so it declares the two
// lookups it needs as `protocol::ControllerDirectoryView` and the dependency is inverted rather
// than the domain graph. This adapter is the inversion's other half: it holds a
// `const runtime::ControllerDirectory&` and translates one runtime value into one protocol value,
// which is the whole of it.
//
// **Absence is an ordinary answer.** A snapshot a reader still holds can outlive the session of a
// controller it names, and a placement outlives its controller's session for the whole match, so
// `std::nullopt` reaches the encoder routinely and the encoder answers it with the documented
// fallback rather than failing a frame every other peer is also waiting for.
//
// The directory carries its own reader/writer lock and `find` returns a copy, so this view is safe
// to call from the server event loop while the same event loop opens and closes sessions.
// related: src/runtime/controller_directory.hpp -- the values this reads.
// related: src/protocol/controller_directory_view.hpp -- the port this satisfies.
class RuntimeControllerDirectoryView final : public protocol::ControllerDirectoryView {
public:
  explicit RuntimeControllerDirectoryView(
      const runtime::ControllerDirectory& controller_directory) noexcept;

  RuntimeControllerDirectoryView(const RuntimeControllerDirectoryView&) = default;
  RuntimeControllerDirectoryView(RuntimeControllerDirectoryView&&) noexcept = default;
  RuntimeControllerDirectoryView& operator=(const RuntimeControllerDirectoryView&) = default;
  RuntimeControllerDirectoryView& operator=(RuntimeControllerDirectoryView&&) noexcept = default;
  ~RuntimeControllerDirectoryView() override = default;

  [[nodiscard]] std::optional<protocol::PublishedController>
  find_controller(simulation::ControllerId controller) const override;

private:
  const runtime::ControllerDirectory* controller_directory_;
};

} // namespace blob_royale::server

#endif
