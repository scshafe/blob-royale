#ifndef BLOB_ROYALE_APPLICATION_BLOB_ROYALE_APPLICATION_HPP
#define BLOB_ROYALE_APPLICATION_BLOB_ROYALE_APPLICATION_HPP

#include "application_config.hpp"
#include "room.hpp"

#include "game_server.hpp"
#include "lobby_directory.hpp"

#include "game_world.hpp"
#include "map_definition.hpp"

#include "structured_logger.hpp"

#include <atomic>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace blob_royale::application {

// canonical: blob_royale_application -- the sole process-lifecycle composition root.
//
// **It is the only file that knows every registry.** `[match] mode` resolves through
// `gameplay::GameModeRegistry` and each `[match] bots` kind through
// `controllers::ControllerRegistry`; the mode validates the map, each room's simulation is
// constructed with both, and the hosted bots reach the world through the same `CommandSink` a
// browser session will (`docs/architecture/0004-gameplay-architecture.md` § "Controllers"). Nothing
// below this class resolves a name.
//
// **It owns `[lobbies] count` rooms and one server.** Every room is the single-match server this
// process used to be -- a runtime on its own thread, its bots, its session capability -- numbered
// `1..N` and seeded `seed + (lobby_id - 1)`; the server receives a `LobbyDirectory` of all of them
// and serves room 1 on every route until plan Step 13 adds the directory and room routes. One
// control loop on the caller's thread drives every room: dropped commands, overruns, controller
// passes, bot reconciliation, phase changes, and abandonment
// (`docs/architecture/0006-lobbies-as-rooms.md` § "Rooms" and § "The lobby lifecycle").
//
// **A room that fails does not stop the process.** Its runtime keeps `failed`, its publication is
// not ready so its sessions close themselves within a presentation period, the control loop logs
// `runtime.failed` once with the exception, and the process keeps serving the rooms that work;
// readiness reports room 1. The failure is retained and rethrown at shutdown so the exit code
// says a worker died.
class BlobRoyaleApplication final {
public:
  // Builds the complete owned room/server graph from validated startup values.
  //
  // `map` is the loaded content `[match] map` named and `initial_world` is the world that map and
  // any scenario produced, which becomes room 1's; every further room is built from the map alone
  // with its own seed. Both arrive already validated; this factory adds the cross-value startup
  // rules (`match_startup_validation.hpp`), resolves the mode once per room, and seats each
  // room's bots.
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

  // Starts process policy and blocks on SIGINT, SIGTERM, or the server's terminal state.
  // A graceful signal returns normally; hard component failures retain their original type.
  void run();

private:
  BlobRoyaleApplication(ApplicationConfig application_config,
                        std::vector<std::unique_ptr<Room>> rooms,
                        observability::StructuredLogger& logger);

  // The registered bot kinds, as the `welcome` publishes them. Static because it reads only the
  // constexpr registry table.
  [[nodiscard]] static std::vector<std::string> registered_npc_controller_kinds();
  // The directory the server is handed: one row per room, in room order.
  [[nodiscard]] static server::LobbyDirectory
  directory_of(std::span<const std::unique_ptr<Room>> rooms);
  void start_server_thread();
  void stop_owned_components() noexcept;
  void rethrow_retained_component_failure() const;

  // These owned values remain in dependency order. Explicit shutdown and reverse destruction both
  // remove the server thread and network boundary before the directory and rooms they reference.
  observability::StructuredLogger& logger_;
  const ApplicationConfig application_config_;
  std::vector<std::unique_ptr<Room>> rooms_;
  server::LobbyDirectory lobby_directory_;
  server::GameServer game_server_;
  std::jthread server_thread_;

  // Coordination state is independent of the owned component graph above.
  std::atomic<bool> run_invoked_{false};
};

} // namespace blob_royale::application

#endif
