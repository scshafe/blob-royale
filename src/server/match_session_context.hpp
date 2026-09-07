#ifndef BLOB_ROYALE_SERVER_MATCH_SESSION_CONTEXT_HPP
#define BLOB_ROYALE_SERVER_MATCH_SESSION_CONTEXT_HPP

#include "runtime_controller_directory_view.hpp"

#include "command_sink.hpp"
#include "controller_directory.hpp"

#include "command_kind_mask.hpp"

#include <string>

namespace blob_royale::server {

// canonical: match_session_context -- everything `/api/v2/session` needs that protocol v1 did not.
//
// **It is a capability bundle, and its size is the whole point.** Protocol v1's server reads only
// immutable `ServerConfig` and `const SnapshotPublication&`. v2 adds a route that can cause an
// effect, so it adds exactly three things and no fourth: a write-only `runtime::CommandSink&`
// whose entire interface is `open_session`, `submit`, and `close_session`; a read-only
// presentation directory to join `controller_kind` and `display_name` at the encoding boundary;
// and the two match identities a `welcome` announces. It grants no world read, no lifecycle
// transition, and no reference to `GameSimulation`, `GameWorld`, or `SimulationRuntime`, which is
// why the absence of a v2 lifecycle route is structural rather than a rule
// (`docs/protocol/v2.md` § "Boundaries").
//
// **The mode name is deliberately absent.** Every snapshot already carries it
// (`simulation::MatchSnapshot::mode_name`), and two sources for one value is the defect the whole
// v2 match section refuses. The map name is here because no snapshot carries it.
//
// **`accepted_command_kinds` is the running mode's own mask**, read once at composition from the
// mode the engine is constructed with, so the advertised set and the enforced set cannot drift:
// the mode is fixed for the process lifetime and this value is a copy of its declaration.
// related: docs/protocol/v2.md -- the boundary this crosses.
// related: session_websocket_session.hpp -- the only consumer.
class MatchSessionContext final {
public:
  // Validates the map name against `common.schema.json#/$defs/map_name`, because a name the
  // welcome could not encode must fail at startup rather than on every session's first frame.
  // Throws GameServerError with `SERVER.SESSION.INVARIANT_FAILED`.
  [[nodiscard]] static MatchSessionContext
  create(runtime::CommandSink& command_sink,
         const runtime::ControllerDirectory& controller_directory, std::string map_name,
         simulation::CommandKindMask accepted_command_kinds);

  MatchSessionContext(const MatchSessionContext&) = default;
  MatchSessionContext(MatchSessionContext&&) noexcept = default;
  MatchSessionContext& operator=(const MatchSessionContext&) = default;
  MatchSessionContext& operator=(MatchSessionContext&&) noexcept = default;
  ~MatchSessionContext() = default;

  [[nodiscard]] runtime::CommandSink& command_sink() const noexcept { return *command_sink_; }

  [[nodiscard]] const protocol::ControllerDirectoryView& directory_view() const& noexcept {
    return directory_view_;
  }
  [[nodiscard]] const protocol::ControllerDirectoryView& directory_view() const&& = delete;

  [[nodiscard]] const std::string& map_name() const& noexcept { return map_name_; }
  [[nodiscard]] const std::string& map_name() const&& = delete;

  [[nodiscard]] simulation::CommandKindMask accepted_command_kinds() const noexcept {
    return accepted_command_kinds_;
  }

private:
  MatchSessionContext(runtime::CommandSink& command_sink,
                      const runtime::ControllerDirectory& controller_directory,
                      std::string map_name,
                      simulation::CommandKindMask accepted_command_kinds) noexcept;

  runtime::CommandSink* command_sink_;
  RuntimeControllerDirectoryView directory_view_;
  std::string map_name_;
  simulation::CommandKindMask accepted_command_kinds_;
};

} // namespace blob_royale::server

#endif
