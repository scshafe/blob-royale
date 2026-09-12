#ifndef BLOB_ROYALE_SERVER_MATCH_SESSION_CONTEXT_HPP
#define BLOB_ROYALE_SERVER_MATCH_SESSION_CONTEXT_HPP

#include "runtime_controller_directory_view.hpp"

#include "command_sink.hpp"
#include "controller_directory.hpp"
#include "movement_tuning_result_delivery.hpp"

#include "command_kind_mask.hpp"
#include "npc_catalogue.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace blob_royale::server {

// canonical: match_session_context -- everything `/api/v3/lobbies/1/session` needs that protocol v1
// did not.
//
// **It is a capability bundle, and its size is the whole point.** Protocol v1's server reads only
// immutable `ServerConfig` and `const SnapshotPublication&`. v3 adds a route that can cause an
// effect, so it adds a write-only `runtime::CommandSink&`
// whose entire interface is `open_session`, `submit`, and `close_session`; a read-only
// presentation directory to join `controller_kind` and `display_name` at the encoding boundary;
// the identities a `welcome` announces; and a narrow consuming tuning-result capability. Claim
// returns one owned result, not mailbox or world access. It grants no world read, no lifecycle
// transition, and no reference to `GameSimulation`, `GameWorld`, or `SimulationRuntime`, which is
// why the absence of a v3 lifecycle route is structural rather than a rule
// (`docs/protocol/v3.md` § "Boundaries").
//
// **The mode name is deliberately absent.** Every snapshot already carries it
// (`simulation::MatchSnapshot::mode_name`), and two sources for one value is the defect the whole
// v3 match section refuses. The map name is here because no snapshot carries it.
//
// **`accepted_command_kinds` is the running mode's own mask**, read once at composition from the
// mode the engine is constructed with, so the advertised set and the enforced set cannot drift:
// the mode is fixed for the process lifetime and this value is a copy of its declaration.
//
// **`npc_catalogue` is the composition root's validated selection set**, shared with runtime
// admission. Its plain kinds and profiled declarations teach the welcome and decoder the same
// exact choices. The server neither links controllers nor chooses tactical policy.
// related: docs/protocol/v3.md -- the boundary this crosses.
// related: session_websocket_session.hpp -- the only consumer.
class MatchSessionContext final {
public:
  // Validates the map name against `common.schema.json#/$defs/map_name`, because a name the
  // welcome could not encode must fail at startup rather than on every session's first frame.
  // Throws GameServerError with `SERVER.SESSION.INVARIANT_FAILED`.
  // NPC names and partition budgets are already validated by NpcCatalogue construction.
  //
  // `lobby_id` is the room this capability belongs to, `1..N`: every line a session logs carries
  // it, and protocol 2.4's `welcome.lobby_id` publishes it. Zero is refused.
  //
  // `seat_count_maximum` is the most seats this room's map can seat -- its spawn-marker count --
  // which protocol 2.4's `welcome.seat_count_maximum` publishes so a client can bound its seat
  // control without a second round trip. It is a map fact, so it is carried here once rather than
  // in every snapshot. Zero and anything above `kLobbySeatCountMaximum` are refused.
  [[nodiscard]] static MatchSessionContext
  create(std::uint64_t lobby_id, runtime::CommandSink& command_sink,
         runtime::MovementTuningResultDelivery& tuning_result_delivery,
         const runtime::ControllerDirectory& controller_directory, std::string map_name,
         std::uint64_t seat_count_maximum, simulation::CommandKindMask accepted_command_kinds,
         simulation::NpcCatalogue npc_catalogue);

  MatchSessionContext(const MatchSessionContext&) = default;
  MatchSessionContext(MatchSessionContext&&) noexcept = default;
  MatchSessionContext& operator=(const MatchSessionContext&) = default;
  MatchSessionContext& operator=(MatchSessionContext&&) noexcept = default;
  ~MatchSessionContext() = default;

  [[nodiscard]] std::uint64_t lobby_id() const noexcept { return lobby_id_; }
  [[nodiscard]] runtime::CommandSink& command_sink() const noexcept { return *command_sink_; }
  [[nodiscard]] runtime::MovementTuningResultDelivery& tuning_result_delivery() const noexcept {
    return *tuning_result_delivery_;
  }

  [[nodiscard]] const protocol::ControllerDirectoryView& directory_view() const& noexcept {
    return directory_view_;
  }
  [[nodiscard]] const protocol::ControllerDirectoryView& directory_view() const&& = delete;

  [[nodiscard]] const std::string& map_name() const& noexcept { return map_name_; }
  [[nodiscard]] const std::string& map_name() const&& = delete;

  [[nodiscard]] std::uint64_t seat_count_maximum() const noexcept { return seat_count_maximum_; }

  [[nodiscard]] simulation::CommandKindMask accepted_command_kinds() const noexcept {
    return accepted_command_kinds_;
  }

  // The seatable NPC kinds, in the registry's declared order.
  [[nodiscard]] std::span<const std::string> npc_controller_kinds() const& noexcept {
    return npc_catalogue_.unprofiled_kinds();
  }
  [[nodiscard]] std::span<const std::string> npc_controller_kinds() const&& = delete;
  [[nodiscard]] std::span<const simulation::NpcDeclaration> npc_profiles() const& noexcept {
    return npc_catalogue_.profiles();
  }
  [[nodiscard]] std::span<const simulation::NpcDeclaration> npc_profiles() const&& = delete;
  [[nodiscard]] const simulation::NpcCatalogue& npc_catalogue() const& noexcept {
    return npc_catalogue_;
  }
  [[nodiscard]] const simulation::NpcCatalogue& npc_catalogue() const&& = delete;

private:
  MatchSessionContext(std::uint64_t lobby_id, runtime::CommandSink& command_sink,
                      runtime::MovementTuningResultDelivery& tuning_result_delivery,
                      const runtime::ControllerDirectory& controller_directory,
                      std::string map_name, std::uint64_t seat_count_maximum,
                      simulation::CommandKindMask accepted_command_kinds,
                      simulation::NpcCatalogue npc_catalogue) noexcept;

  std::uint64_t lobby_id_;
  runtime::CommandSink* command_sink_;
  runtime::MovementTuningResultDelivery* tuning_result_delivery_;
  RuntimeControllerDirectoryView directory_view_;
  std::string map_name_;
  std::uint64_t seat_count_maximum_;
  simulation::CommandKindMask accepted_command_kinds_;
  simulation::NpcCatalogue npc_catalogue_;
};

} // namespace blob_royale::server

#endif
