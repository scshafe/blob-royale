#ifndef BLOB_ROYALE_APPLICATION_APPLICATION_CONFIG_HPP
#define BLOB_ROYALE_APPLICATION_APPLICATION_CONFIG_HPP

#include "game_mode_configuration.hpp"
#include "match_configuration.hpp"
#include "server_config.hpp"
#include "simulation_config.hpp"

#include <utility>

namespace blob_royale::application {

// canonical: application_config -- immutable startup aggregate for the composition root.
//
// Four independently validated values, each owned by the domain whose rules it carries: transport
// policy (`server::ServerConfig`), kernel parameters (`simulation::SimulationConfig`), which match
// is played (`MatchConfiguration`), and each configured mode's balance
// (`gameplay::GameModeConfiguration`). The composition root reads all four and no other file
// aggregates them.
class ApplicationConfig final {
public:
  // Aggregates independently validated server, simulation, match, and mode configurations.
  [[nodiscard]] static ApplicationConfig
  create(server::ServerConfig server_config, simulation::SimulationConfig simulation_config,
         MatchConfiguration match_configuration,
         gameplay::GameModeConfiguration game_mode_configuration);

  ApplicationConfig(const ApplicationConfig&) = default;
  ApplicationConfig(ApplicationConfig&&) noexcept = default;
  ApplicationConfig& operator=(const ApplicationConfig&) = default;
  ApplicationConfig& operator=(ApplicationConfig&&) noexcept = default;
  ~ApplicationConfig() = default;

  [[nodiscard]] const server::ServerConfig& server_config() const& noexcept {
    return server_config_;
  }
  [[nodiscard]] const server::ServerConfig& server_config() const&& = delete;
  [[nodiscard]] const simulation::SimulationConfig& simulation_config() const& noexcept {
    return simulation_config_;
  }
  [[nodiscard]] const simulation::SimulationConfig& simulation_config() const&& = delete;
  [[nodiscard]] const MatchConfiguration& match_configuration() const& noexcept {
    return match_configuration_;
  }
  [[nodiscard]] const MatchConfiguration& match_configuration() const&& = delete;
  [[nodiscard]] const gameplay::GameModeConfiguration& game_mode_configuration() const& noexcept {
    return game_mode_configuration_;
  }
  [[nodiscard]] const gameplay::GameModeConfiguration& game_mode_configuration() const&& = delete;

  friend bool operator==(const ApplicationConfig&, const ApplicationConfig&) = default;

private:
  ApplicationConfig(server::ServerConfig server_config,
                    simulation::SimulationConfig simulation_config,
                    MatchConfiguration match_configuration,
                    gameplay::GameModeConfiguration game_mode_configuration) noexcept;

  server::ServerConfig server_config_;
  simulation::SimulationConfig simulation_config_;
  MatchConfiguration match_configuration_;
  gameplay::GameModeConfiguration game_mode_configuration_;
};

} // namespace blob_royale::application

#endif
