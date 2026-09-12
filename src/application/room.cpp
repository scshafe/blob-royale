#include "room.hpp"

#include "controller_registry.hpp"

#include "command_registry.hpp"

#include <string>
#include <utility>

namespace blob_royale::application {

Room::Room(const std::uint64_t lobby_id, simulation::GameSimulation game_simulation,
           const simulation::CommandKindMask accepted_command_kinds,
           const MatchConfiguration& match_configuration,
           controllers::TacticalProfileCatalogue tactical_profiles,
           simulation::NpcCatalogue npc_catalogue, observability::StructuredLogger& logger)
    : lobby_id_(lobby_id), seed_(match_configuration.seed() + (lobby_id - 1)),
      seat_count_maximum_(game_simulation.map().spawn_points().size()),
      runtime_(std::move(game_simulation), npc_catalogue),
      host_(runtime_.snapshot_publication(), runtime_.command_sink()),
      // The only capability the network boundary receives for this room, named in full: a
      // write-only command sink, a read-only presentation directory, and the identities a `welcome`
      // announces. The server still receives no simulation, no runtime, and no lifecycle
      // transition.
      match_session_(server::MatchSessionContext::create(
          lobby_id, runtime_.command_sink(), runtime_.tuning_result_delivery(),
          runtime_.controller_directory(), std::string{match_configuration.map_name()},
          seat_count_maximum_, accepted_command_kinds, std::move(npc_catalogue))) {
  if (accepted_command_kinds.contains(simulation::CommandKind::kStartMatch)) {
    reconciler_.emplace(runtime_.command_sink(), host_, seed_, match_configuration.seed(),
                        lobby_id_, std::move(tactical_profiles), logger);
  } else {
    seat_configured_bots(match_configuration, logger);
  }
}

void Room::seat_configured_bots(const MatchConfiguration& match_configuration,
                                observability::StructuredLogger& logger) {
  for (const MatchConfiguration::BotRosterEntry& entry : match_configuration.bot_roster()) {
    for (std::uint64_t ordinal = 1; ordinal <= entry.count; ++ordinal) {
      // A bot opens a session exactly as a browser will, through the same `CommandSink`, and its
      // display name is derived from the roster rather than supplied by anyone: nothing about a bot
      // arrives from outside this process.
      const std::string display_name = entry.controller_kind + " " + std::to_string(ordinal);
      const simulation::ControllerId controller =
          runtime_.command_sink().open_session(entry.controller_kind, display_name);
      host_.add(controllers::ControllerRegistry::create(entry.controller_kind, controller, seed_));
    }
  }
  if (!match_configuration.bot_roster().empty()) {
    logger.write({.severity = observability::LogSeverity::kInfo,
                  .event = "controllers.roster_seated",
                  .lobby_id = lobby_id_,
                  .detail = "hosted_controller_count=" + std::to_string(host_.size()) +
                            " match_seed=" + std::to_string(seed_)});
  }
}

} // namespace blob_royale::application
