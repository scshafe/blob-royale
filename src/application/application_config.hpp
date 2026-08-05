#ifndef BLOB_ROYALE_APPLICATION_APPLICATION_CONFIG_HPP
#define BLOB_ROYALE_APPLICATION_APPLICATION_CONFIG_HPP

#include "server_config.hpp"
#include "simulation_config.hpp"

namespace blob_royale::application {

// canonical: application_config -- immutable startup aggregate for the composition root.
class ApplicationConfig final {
public:
  // Aggregates independently validated server and simulation configurations.
  [[nodiscard]] static ApplicationConfig create(server::ServerConfig server_config,
                                                simulation::SimulationConfig simulation_config);

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

  friend bool operator==(const ApplicationConfig&, const ApplicationConfig&) = default;

private:
  ApplicationConfig(server::ServerConfig server_config,
                    simulation::SimulationConfig simulation_config) noexcept;

  server::ServerConfig server_config_;
  simulation::SimulationConfig simulation_config_;
};

} // namespace blob_royale::application

#endif
