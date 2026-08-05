#ifndef BLOB_ROYALE_SERVER_GAME_SERVER_HPP
#define BLOB_ROYALE_SERVER_GAME_SERVER_HPP

#include "game_server_state.hpp"
#include "server_config.hpp"
#include "server_execution_context.hpp"
#include "snapshot_publication.hpp"
#include "structured_logger.hpp"
#include "tcp_listener.hpp"

#include <boost/asio/io_context.hpp>

#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>

namespace blob_royale::server {

// canonical: game_server -- bounded foreground ownership of the protocol-v1 network event loop.
// Its only state source is const SnapshotPublication; no simulation or runtime lifecycle capability
// can cross this constructor boundary.
class GameServer final {
public:
  GameServer(ServerConfig server_config, const runtime::SnapshotPublication& snapshot_publication,
             observability::StructuredLogger& logger);

  GameServer(const GameServer&) = delete;
  GameServer(GameServer&&) = delete;
  GameServer& operator=(const GameServer&) = delete;
  GameServer& operator=(GameServer&&) = delete;
  ~GameServer();

  // Runs one foreground event loop until stop or hard failure. Single-use.
  // The exact original listener/session/encoding exception is stored and rethrown.
  void run();

  // Thread-safe and idempotent. Stops acceptance first, then drains/closes owned sessions.
  void stop() noexcept;

  [[nodiscard]] GameServerState state() const noexcept;
  [[nodiscard]] bool wait_for_state(GameServerState expected_state,
                                    std::chrono::steady_clock::duration timeout) const;

  // Waits until listener startup either succeeds or reaches a terminal/stop state.
  [[nodiscard]] bool wait_for_startup_resolution(std::chrono::steady_clock::duration timeout) const;

  // Rethrows the exact exception captured by run(), if one exists.
  void rethrow_if_failed() const;

private:
  void set_state(GameServerState state) noexcept;

  boost::asio::io_context io_context_{1};
  observability::StructuredLogger& logger_;
  std::shared_ptr<ServerExecutionContext> server_context_;
  std::shared_ptr<TcpListener> tcp_listener_;

  // Serializes state mutation with its corresponding lifecycle event without holding the
  // state-observation mutex across logger I/O.
  std::mutex lifecycle_transition_mutex_;
  mutable std::mutex lifecycle_mutex_;
  mutable std::condition_variable lifecycle_changed_;
  GameServerState lifecycle_state_{GameServerState::kReady};
  bool run_started_{false};
  std::exception_ptr failure_;
};

} // namespace blob_royale::server

#endif
