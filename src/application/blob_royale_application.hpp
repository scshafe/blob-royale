#ifndef BLOB_ROYALE_APPLICATION_BLOB_ROYALE_APPLICATION_HPP
#define BLOB_ROYALE_APPLICATION_BLOB_ROYALE_APPLICATION_HPP

#include "application_config.hpp"
#include "command_kind_mask.hpp"
#include "controller_host.hpp"
#include "game_server.hpp"
#include "game_simulation.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "match_session_context.hpp"
#include "simulation_runtime.hpp"
#include "structured_logger.hpp"

#include <atomic>
#include <thread>

namespace blob_royale::application {

// canonical: blob_royale_application -- the sole process-lifecycle composition root.
//
// **It is the only file that knows every registry.** `[match] mode` resolves through
// `gameplay::GameModeRegistry` and each `[match] bots` kind through
// `controllers::ControllerRegistry`; the mode validates the map, the simulation is constructed with
// both, and the hosted bots reach the world through the same `CommandSink` a browser session will
// (`docs/architecture/0004-gameplay-architecture.md` § "Controllers"). Nothing below this class
// resolves a name.
class BlobRoyaleApplication final {
public:
  // Builds the complete owned simulation/runtime/server/controller graph from validated startup
  // values.
  //
  // `map` is the loaded content `[match] map` named and `initial_world` is the world that map and
  // any scenario produced. Both arrive already validated; this factory adds the two cross-value
  // startup rules (`match_startup_validation.hpp`), resolves the mode, and opens one session per
  // configured bot.
  //
  // Throws ApplicationInputError for a startup rule, GameplayValidationError for a mode that
  // refuses the map, SimulationValidationError for a world the kernel refuses, and
  // ControllersValidationError for a roster the host refuses.
  [[nodiscard]] static BlobRoyaleApplication create(ApplicationConfig application_config,
                                                    simulation::MapDefinition map,
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
  // `accepted_command_kinds` is the running mode's own declaration, read in `create` before the
  // engine destroys the mode, and handed to the server so a `welcome` advertises exactly the set
  // the boundary enforces.
  BlobRoyaleApplication(ApplicationConfig application_config,
                        simulation::GameSimulation game_simulation,
                        simulation::CommandKindMask accepted_command_kinds,
                        observability::StructuredLogger& logger);

  // Opens one `CommandSink` session per configured bot and files the constructed controller with
  // the host, in roster order and then in count order, so the `ControllerId` a bot receives is a
  // function of the configuration alone.
  void seat_configured_bots();

  void start_server_thread();
  void stop_owned_components() noexcept;
  void rethrow_retained_component_failure() const;

  // These owned values remain in dependency order. Explicit shutdown and reverse destruction both
  // remove the server thread and network boundary before their referenced publication/runtime.
  // `controller_host_` holds a `const SnapshotPublication&` and a `CommandSink&` from the runtime,
  // so it is declared after it and destroyed before it.
  observability::StructuredLogger& logger_;
  const ApplicationConfig application_config_;
  runtime::SimulationRuntime simulation_runtime_;
  controllers::ControllerHost controller_host_;
  server::GameServer game_server_;
  std::jthread server_thread_;

  // Coordination state is independent of the owned component graph above.
  std::atomic<bool> run_invoked_{false};
};

} // namespace blob_royale::application

#endif
