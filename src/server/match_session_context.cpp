#include "match_session_context.hpp"

#include "game_server_error.hpp"
#include "protocol_v3_constants.hpp"
#include "session_welcome.hpp"

#include <string>
#include <utility>
#include <vector>

namespace blob_royale::server {

MatchSessionContext::MatchSessionContext(
    const std::uint64_t lobby_id, runtime::CommandSink& command_sink,
    runtime::MovementTuningResultDelivery& tuning_result_delivery,
    const runtime::ControllerDirectory& controller_directory, std::string map_name,
    const std::uint64_t seat_count_maximum,
    const simulation::CommandKindMask accepted_command_kinds,
    simulation::NpcCatalogue npc_catalogue) noexcept
    : lobby_id_(lobby_id), command_sink_(&command_sink),
      tuning_result_delivery_(&tuning_result_delivery), directory_view_(controller_directory),
      map_name_(std::move(map_name)), seat_count_maximum_(seat_count_maximum),
      accepted_command_kinds_(accepted_command_kinds), npc_catalogue_(std::move(npc_catalogue)) {}

MatchSessionContext
MatchSessionContext::create(const std::uint64_t lobby_id, runtime::CommandSink& command_sink,
                            runtime::MovementTuningResultDelivery& tuning_result_delivery,
                            const runtime::ControllerDirectory& controller_directory,
                            std::string map_name, const std::uint64_t seat_count_maximum,
                            const simulation::CommandKindMask accepted_command_kinds,
                            simulation::NpcCatalogue npc_catalogue) {
  if (lobby_id == 0) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed,
                          "match_session_context.lobby_id", "rooms are numbered from 1"};
  }
  if (seat_count_maximum == 0 || seat_count_maximum > protocol::kLobbySeatCountMaximum) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed,
                          "match_session_context.seat_count_maximum",
                          "a map seats between 1 and " +
                              std::to_string(protocol::kLobbySeatCountMaximum) + " players"};
  }
  if (!protocol::is_accepted_map_name(map_name)) {
    throw GameServerError{
        GameServerErrorCode::kSessionInvariantFailed, "match_session_context.map_name",
        "the map name is not a protocol v3 map_name of at most " +
            std::to_string(protocol::kMapNameMaximumCharacterCount) + " characters"};
  }
  return {lobby_id,
          command_sink,
          tuning_result_delivery,
          controller_directory,
          std::move(map_name),
          seat_count_maximum,
          accepted_command_kinds,
          std::move(npc_catalogue)};
}

} // namespace blob_royale::server
