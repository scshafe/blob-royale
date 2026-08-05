#include "application_config.hpp"

#include <utility>

namespace blob_royale::application {

ApplicationConfig ApplicationConfig::create(server::ServerConfig server_config,
                                            simulation::SimulationConfig simulation_config) {
  return ApplicationConfig{std::move(server_config), simulation_config};
}

ApplicationConfig::ApplicationConfig(server::ServerConfig server_config,
                                     simulation::SimulationConfig simulation_config) noexcept
    : server_config_(std::move(server_config)), simulation_config_(simulation_config) {}

} // namespace blob_royale::application
