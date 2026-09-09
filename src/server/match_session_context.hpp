#ifndef BLOB_ROYALE_SERVER_MATCH_SESSION_CONTEXT_HPP
#define BLOB_ROYALE_SERVER_MATCH_SESSION_CONTEXT_HPP

#include "runtime_controller_directory_view.hpp"

#include "command_sink.hpp"
#include "controller_directory.hpp"

#include "command_kind_mask.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

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
//
// **`npc_controller_kinds` is `ControllerRegistry`'s own list**, read once at composition for the
// same reason and carried here because `blob_server` neither links `blob_controllers` nor should:
// the server has no business constructing a bot, only publishing which ones exist and refusing a
// `seat_npc` that names one that does not. It is the *fourth* thing this bundle carries, and it
// earns its place by being the one value that makes "registering a bot costs no client change" true
// -- the `welcome` publishes it and `decode_command_envelope` enforces it, from one source
// (`src/protocol/session_welcome.hpp`).
// related: docs/protocol/v2.md -- the boundary this crosses.
// related: session_websocket_session.hpp -- the only consumer.
class MatchSessionContext final {
public:
  // Validates the map name against `common.schema.json#/$defs/map_name`, because a name the
  // welcome could not encode must fail at startup rather than on every session's first frame.
  // Throws GameServerError with `SERVER.SESSION.INVARIANT_FAILED`.
  // Validates the map name and every published NPC controller kind against
  // `common.schema.json#/$defs/kind_name`, because a name the welcome could not encode must fail at
  // startup rather than on every session's first frame.
  //
  // `lobby_id` is the room this capability belongs to, `1..N`: every line a session logs carries
  // it, and protocol 2.4's `welcome.lobby_id` publishes it. Zero is refused.
  [[nodiscard]] static MatchSessionContext
  create(std::uint64_t lobby_id, runtime::CommandSink& command_sink,
         const runtime::ControllerDirectory& controller_directory, std::string map_name,
         simulation::CommandKindMask accepted_command_kinds,
         std::vector<std::string> npc_controller_kinds);

  MatchSessionContext(const MatchSessionContext&) = default;
  MatchSessionContext(MatchSessionContext&&) noexcept = default;
  MatchSessionContext& operator=(const MatchSessionContext&) = default;
  MatchSessionContext& operator=(MatchSessionContext&&) noexcept = default;
  ~MatchSessionContext() = default;

  [[nodiscard]] std::uint64_t lobby_id() const noexcept { return lobby_id_; }
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

  // The seatable NPC kinds, in the registry's declared order.
  [[nodiscard]] std::span<const std::string> npc_controller_kinds() const& noexcept {
    return npc_controller_kinds_;
  }
  [[nodiscard]] std::span<const std::string> npc_controller_kinds() const&& = delete;

private:
  MatchSessionContext(std::uint64_t lobby_id, runtime::CommandSink& command_sink,
                      const runtime::ControllerDirectory& controller_directory,
                      std::string map_name, simulation::CommandKindMask accepted_command_kinds,
                      std::vector<std::string> npc_controller_kinds) noexcept;

  std::uint64_t lobby_id_;
  runtime::CommandSink* command_sink_;
  RuntimeControllerDirectoryView directory_view_;
  std::string map_name_;
  simulation::CommandKindMask accepted_command_kinds_;
  std::vector<std::string> npc_controller_kinds_;
};

} // namespace blob_royale::server

#endif
