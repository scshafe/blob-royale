#include "application_config.hpp"

#include "application_input_error.hpp"
#include "game_mode_registry.hpp"

#include <string>
#include <utility>

namespace blob_royale::application {

ApplicationConfig ApplicationConfig::create(
    server::ServerConfig server_config, simulation::SimulationConfig simulation_config,
    MatchConfiguration match_configuration, gameplay::GameModeConfiguration game_mode_configuration,
    const LobbiesConfiguration lobbies_configuration,
    controllers::TacticalProfileCatalogue tactical_profiles) {
  bool has_profiled_bot = false;
  for (const MatchConfiguration::BotRosterEntry& entry : match_configuration.bot_roster()) {
    if (!entry.profile_name.has_value()) {
      continue;
    }
    has_profiled_bot = true;
    if (tactical_profiles.find(*entry.profile_name) == nullptr) {
      throw ApplicationInputError{ApplicationInputErrorCode::kMatchBotProfileUnknown, "match.bots",
                                  "profile " + std::string{entry.profile_name->value()} +
                                      " is not declared by a bot_profile section"};
    }
  }
  // The selected mode owns lobby capability. A second mode-name list would drift as modes grow.
  if (has_profiled_bot &&
      !gameplay::GameModeRegistry::create(match_configuration.mode_name(), game_mode_configuration)
           ->accepted_command_kinds()
           .contains(simulation::CommandKind::kStartMatch)) {
    throw ApplicationInputError{ApplicationInputErrorCode::kMatchBotProfileModeUnsupported,
                                "match.bots",
                                "profiled startup bots require a mode accepting start_match for "
                                "stable lobby-seat identity"};
  }
  return ApplicationConfig{std::move(server_config),       simulation_config,
                           std::move(match_configuration), std::move(game_mode_configuration),
                           lobbies_configuration,          std::move(tactical_profiles)};
}

ApplicationConfig::ApplicationConfig(
    server::ServerConfig server_config, simulation::SimulationConfig simulation_config,
    MatchConfiguration match_configuration, gameplay::GameModeConfiguration game_mode_configuration,
    const LobbiesConfiguration lobbies_configuration,
    controllers::TacticalProfileCatalogue tactical_profiles) noexcept
    : server_config_(std::move(server_config)), simulation_config_(simulation_config),
      match_configuration_(std::move(match_configuration)),
      game_mode_configuration_(std::move(game_mode_configuration)),
      lobbies_configuration_(lobbies_configuration),
      tactical_profiles_(std::move(tactical_profiles)) {}

} // namespace blob_royale::application
