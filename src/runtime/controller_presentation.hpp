#ifndef BLOB_ROYALE_RUNTIME_CONTROLLER_PRESENTATION_HPP
#define BLOB_ROYALE_RUNTIME_CONTROLLER_PRESENTATION_HPP

#include <string>

namespace blob_royale::runtime {

// canonical: controller_presentation -- everything a wire encoder may say about one controller.
//
// These are the two values ADR 0004's amendment of 2026-09-06 moves **out** of the `Controllable`
// component: the controller kind and the display name are published inside the wire `controllable`
// object but are joined at the encoding boundary from `ControllerDirectory`, so the deterministic
// core never stores a proxy-supplied string. A tick that could read a display name would make the
// simulation a function of a header a proxy wrote, which is exactly what the 100-fresh-run
// bit-identity rule forbids.
//
// Both strings are validated and bounded before an entry is registered
// (`command_sink.hpp`, `runtime_limits.hpp`). They are owned values, not views, because the
// directory hands out copies: an encoder that held a reference would race a session closing
// mid-encode.
// related: controller_directory.hpp -- the bounded map these live in.
// related: controller_id.hpp -- the durable identity they describe.
struct ControllerPresentation final {
  // What kind of agent decides: "session" for a networked player, a bot's registered kind name for
  // an in-process controller, "scripted_replay" for a fixture.
  std::string controller_kind;
  // The name a client renders. Proxy-supplied for a networked player, roster-supplied for a bot.
  std::string display_name;

  friend bool operator==(const ControllerPresentation&, const ControllerPresentation&) = default;
};

} // namespace blob_royale::runtime

#endif
