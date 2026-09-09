#ifndef BLOB_ROYALE_APPLICATION_ROOM_HPP
#define BLOB_ROYALE_APPLICATION_ROOM_HPP

#include "bot_reconciliation.hpp"
#include "match_configuration.hpp"

#include "controller_host.hpp"

#include "match_session_context.hpp"

#include "command_kind_mask.hpp"
#include "game_simulation.hpp"
#include "simulation_runtime.hpp"

#include "structured_logger.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace blob_royale::application {

// canonical: room -- one lobby: a runtime, the bots that play in it, and the capability its
// sessions run on.
//
// Every room is the single-match server this process used to be, built the same way and owned in
// the same dependency order: the runtime first, then the host and the reconciliation that hold
// references into it, then the `MatchSessionContext` the server hands to sessions. N rooms are N of
// these on N worker threads (`docs/architecture/0006-lobbies-as-rooms.md` § "Rooms"), and nothing
// a room owns knows another room exists. The composition root numbers them `1..N`, seeds each
// world with `seed + (lobby_id - 1)` so two rooms never play the same seed, and drives them all
// from one control loop.
// related: blob_royale_application.hpp -- the owner and the loop.
// related: ../server/lobby_directory.hpp -- how the server sees a room.
class Room final {
public:
  // Takes ownership of a validated simulation whose initial world already carries its lobby. Seats
  // the `[match] bots` roster at once for a mode without a lobby, exactly as the single-match
  // server did; a mode with a lobby gets its bots from the reconciliation on every control poll.
  Room(std::uint64_t lobby_id, simulation::GameSimulation game_simulation,
       simulation::CommandKindMask accepted_command_kinds,
       const MatchConfiguration& match_configuration, std::vector<std::string> npc_controller_kinds,
       observability::StructuredLogger& logger);

  Room(const Room&) = delete;
  Room(Room&&) = delete;
  Room& operator=(const Room&) = delete;
  Room& operator=(Room&&) = delete;
  ~Room() = default;

  [[nodiscard]] std::uint64_t lobby_id() const noexcept { return lobby_id_; }
  [[nodiscard]] runtime::SimulationRuntime& runtime() noexcept { return runtime_; }
  [[nodiscard]] const runtime::SimulationRuntime& runtime() const noexcept { return runtime_; }
  [[nodiscard]] controllers::ControllerHost& host() noexcept { return host_; }
  // Present exactly when the mode has a lobby.
  [[nodiscard]] SeatBotReconciler* reconciler() noexcept {
    return reconciler_.has_value() ? &*reconciler_ : nullptr;
  }
  [[nodiscard]] const server::MatchSessionContext& match_session() const& noexcept {
    return match_session_;
  }
  [[nodiscard]] const server::MatchSessionContext& match_session() const&& = delete;
  // The seed this room's bots and world were built from.
  [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

private:
  void seat_configured_bots(const MatchConfiguration& match_configuration,
                            observability::StructuredLogger& logger);

  std::uint64_t lobby_id_;
  std::uint64_t seed_;
  runtime::SimulationRuntime runtime_;
  controllers::ControllerHost host_;
  std::optional<SeatBotReconciler> reconciler_;
  server::MatchSessionContext match_session_;
};

} // namespace blob_royale::application

#endif
