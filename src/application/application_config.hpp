#ifndef BLOB_ROYALE_APPLICATION_APPLICATION_CONFIG_HPP
#define BLOB_ROYALE_APPLICATION_APPLICATION_CONFIG_HPP

#include "game_mode_configuration.hpp"
#include "lobbies_configuration.hpp"
#include "match_configuration.hpp"
#include "server_config.hpp"
#include "simulation_config.hpp"

#include <utility>

namespace blob_royale::application {

// canonical: application_config -- immutable startup aggregate for the composition root.
//
// Five independently validated values, each owned by the domain whose rules it carries: transport
// policy (`server::ServerConfig`), kernel parameters (`simulation::SimulationConfig`), which match
// is played (`MatchConfiguration`), each configured mode's balance
// (`gameplay::GameModeConfiguration`), and how many rooms play it (`LobbiesConfiguration`). The
// composition root reads all five and no other file aggregates them.
class ApplicationConfig final {
public:
  // Aggregates independently validated server, simulation, match, mode, and lobbies
  // configurations.
  [[nodiscard]] static ApplicationConfig
  create(server::ServerConfig server_config, simulation::SimulationConfig simulation_config,
         MatchConfiguration match_configuration,
         gameplay::GameModeConfiguration game_mode_configuration,
         LobbiesConfiguration lobbies_configuration);

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
  [[nodiscard]] const LobbiesConfiguration& lobbies_configuration() const& noexcept {
    return lobbies_configuration_;
  }
  [[nodiscard]] const LobbiesConfiguration& lobbies_configuration() const&& = delete;

  friend bool operator==(const ApplicationConfig&, const ApplicationConfig&) = default;

private:
  ApplicationConfig(server::ServerConfig server_config,
                    simulation::SimulationConfig simulation_config,
                    MatchConfiguration match_configuration,
                    gameplay::GameModeConfiguration game_mode_configuration,
                    LobbiesConfiguration lobbies_configuration) noexcept;

  server::ServerConfig server_config_;
  simulation::SimulationConfig simulation_config_;
  MatchConfiguration match_configuration_;
  gameplay::GameModeConfiguration game_mode_configuration_;
  LobbiesConfiguration lobbies_configuration_;
};

} // namespace blob_royale::application

#endif
