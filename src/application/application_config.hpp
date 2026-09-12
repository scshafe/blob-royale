#ifndef BLOB_ROYALE_APPLICATION_APPLICATION_CONFIG_HPP
#define BLOB_ROYALE_APPLICATION_APPLICATION_CONFIG_HPP

#include "game_mode_configuration.hpp"
#include "lobbies_configuration.hpp"
#include "match_configuration.hpp"
#include "server_config.hpp"
#include "simulation_config.hpp"
#include "tactical_profile_catalogue.hpp"

#include <utility>

namespace blob_royale::application {

// canonical: application_config -- immutable startup aggregate for the composition root.
//
// Independently validated values, each owned by the domain whose rules it carries: transport
// policy (`server::ServerConfig`), kernel parameters (`simulation::SimulationConfig`), which match
// is played (`MatchConfiguration`), each configured mode's balance
// (`gameplay::GameModeConfiguration`), how many rooms play it (`LobbiesConfiguration`), and
// authored bot profiles (`controllers::TacticalProfileCatalogue`). This boundary also resolves
// roster profile selections and requires a mode with stable lobby seats for profiled startup bots.
class ApplicationConfig final {
public:
  // Aggregates validated configuration. Throws ApplicationInputError with
  // APPLICATION.MATCH.BOT_PROFILE_UNKNOWN or BOT_PROFILE_MODE_UNSUPPORTED when the selected
  // profile does not resolve or the actual configured mode accepts no StartMatch command.
  [[nodiscard]] static ApplicationConfig
  create(server::ServerConfig server_config, simulation::SimulationConfig simulation_config,
         MatchConfiguration match_configuration,
         gameplay::GameModeConfiguration game_mode_configuration,
         LobbiesConfiguration lobbies_configuration,
         controllers::TacticalProfileCatalogue tactical_profiles = {});

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
  [[nodiscard]] const controllers::TacticalProfileCatalogue& tactical_profiles() const& noexcept {
    return tactical_profiles_;
  }
  [[nodiscard]] const controllers::TacticalProfileCatalogue& tactical_profiles() const&& = delete;

  friend bool operator==(const ApplicationConfig&, const ApplicationConfig&) = default;

private:
  ApplicationConfig(server::ServerConfig server_config,
                    simulation::SimulationConfig simulation_config,
                    MatchConfiguration match_configuration,
                    gameplay::GameModeConfiguration game_mode_configuration,
                    LobbiesConfiguration lobbies_configuration,
                    controllers::TacticalProfileCatalogue tactical_profiles) noexcept;

  server::ServerConfig server_config_;
  simulation::SimulationConfig simulation_config_;
  MatchConfiguration match_configuration_;
  gameplay::GameModeConfiguration game_mode_configuration_;
  LobbiesConfiguration lobbies_configuration_;
  controllers::TacticalProfileCatalogue tactical_profiles_;
};

} // namespace blob_royale::application

#endif
