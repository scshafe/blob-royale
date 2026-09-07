#include "match_session_context.hpp"

#include "game_server_error.hpp"
#include "protocol_v2_constants.hpp"
#include "session_welcome.hpp"

#include <string>
#include <utility>

namespace blob_royale::server {

MatchSessionContext::MatchSessionContext(
    runtime::CommandSink& command_sink, const runtime::ControllerDirectory& controller_directory,
    std::string map_name, const simulation::CommandKindMask accepted_command_kinds) noexcept
    : command_sink_(&command_sink), directory_view_(controller_directory),
      map_name_(std::move(map_name)), accepted_command_kinds_(accepted_command_kinds) {}

MatchSessionContext MatchSessionContext::create(
    runtime::CommandSink& command_sink, const runtime::ControllerDirectory& controller_directory,
    std::string map_name, const simulation::CommandKindMask accepted_command_kinds) {
  if (!protocol::is_accepted_map_name(map_name)) {
    throw GameServerError{
        GameServerErrorCode::kSessionInvariantFailed, "match_session_context.map_name",
        "the map name is not a protocol v2 map_name of at most " +
            std::to_string(protocol::kMapNameMaximumCharacterCount) + " characters"};
  }
  return {command_sink, controller_directory, std::move(map_name), accepted_command_kinds};
}

} // namespace blob_royale::server
