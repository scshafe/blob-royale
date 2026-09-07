#include "application_config.hpp"

#include <utility>

namespace blob_royale::application {

ApplicationConfig
ApplicationConfig::create(server::ServerConfig server_config,
                          simulation::SimulationConfig simulation_config,
                          MatchConfiguration match_configuration,
                          gameplay::GameModeConfiguration game_mode_configuration) {
  return ApplicationConfig{std::move(server_config), simulation_config,
                           std::move(match_configuration), std::move(game_mode_configuration)};
}

ApplicationConfig::ApplicationConfig(
    server::ServerConfig server_config, simulation::SimulationConfig simulation_config,
    MatchConfiguration match_configuration,
    gameplay::GameModeConfiguration game_mode_configuration) noexcept
    : server_config_(std::move(server_config)), simulation_config_(simulation_config),
      match_configuration_(std::move(match_configuration)),
      game_mode_configuration_(std::move(game_mode_configuration)) {}

} // namespace blob_royale::application
