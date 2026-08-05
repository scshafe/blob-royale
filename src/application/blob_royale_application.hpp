#ifndef BLOB_ROYALE_APPLICATION_BLOB_ROYALE_APPLICATION_HPP
#define BLOB_ROYALE_APPLICATION_BLOB_ROYALE_APPLICATION_HPP

#include "application_config.hpp"
#include "game_server.hpp"
#include "game_simulation.hpp"
#include "game_world.hpp"
#include "simulation_runtime.hpp"
#include "structured_logger.hpp"

#include <atomic>
#include <thread>

namespace blob_royale::application {

// canonical: blob_royale_application -- the sole process-lifecycle composition root.
class BlobRoyaleApplication final {
public:
  // Builds the complete owned simulation/runtime/server graph from validated startup values.
  [[nodiscard]] static BlobRoyaleApplication create(ApplicationConfig application_config,
                                                    simulation::GameWorld initial_world,
                                                    observability::StructuredLogger& logger);

  BlobRoyaleApplication(const BlobRoyaleApplication&) = delete;
  BlobRoyaleApplication(BlobRoyaleApplication&&) = delete;
  BlobRoyaleApplication& operator=(const BlobRoyaleApplication&) = delete;
  BlobRoyaleApplication& operator=(BlobRoyaleApplication&&) = delete;
  ~BlobRoyaleApplication() noexcept;

  // Starts process policy and blocks on SIGINT, SIGTERM, or an owned-component terminal state.
  // A graceful signal returns normally; hard component failures retain their original type.
  void run();

private:
  BlobRoyaleApplication(ApplicationConfig application_config,
                        simulation::GameSimulation game_simulation,
                        observability::StructuredLogger& logger);

  void start_server_thread();
  void stop_owned_components() noexcept;
  void rethrow_retained_component_failure() const;

  // These owned values remain in dependency order. Explicit shutdown and reverse destruction both
  // remove the server thread and network boundary before their referenced publication/runtime.
  observability::StructuredLogger& logger_;
  const ApplicationConfig application_config_;
  runtime::SimulationRuntime simulation_runtime_;
  server::GameServer game_server_;
  std::jthread server_thread_;

  // Coordination state is independent of the owned component graph above.
  std::atomic<bool> run_invoked_{false};
};

} // namespace blob_royale::application

#endif
