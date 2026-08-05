#ifndef BLOB_ROYALE_SERVER_SERVER_EXECUTION_CONTEXT_HPP
#define BLOB_ROYALE_SERVER_SERVER_EXECUTION_CONTEXT_HPP

#include "game_api_router.hpp"
#include "peer_traffic_policy.hpp"
#include "request_id_generator.hpp"
#include "server_config.hpp"
#include "snapshot_egress_budget.hpp"
#include "snapshot_publication.hpp"
#include "structured_logger.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace blob_royale::server {

enum class SessionStopMode {
  kGraceful,
  kImmediate,
};

// Shared concrete ownership context for one GameServer event loop. It centralizes session
// shutdown and hard-failure propagation without exposing runtime lifecycle capabilities.
class ServerExecutionContext final : public std::enable_shared_from_this<ServerExecutionContext> {
public:
  using SessionId = std::uint64_t;
  using SessionStopAction = std::function<void(SessionStopMode)>;
  using ListenerStopAction = std::function<void()>;

  ServerExecutionContext(boost::asio::io_context& io_context, ServerConfig server_config,
                         const runtime::SnapshotPublication& snapshot_publication,
                         observability::StructuredLogger& logger);

  ServerExecutionContext(const ServerExecutionContext&) = delete;
  ServerExecutionContext(ServerExecutionContext&&) = delete;
  ServerExecutionContext& operator=(const ServerExecutionContext&) = delete;
  ServerExecutionContext& operator=(ServerExecutionContext&&) = delete;
  ~ServerExecutionContext() = default;

  [[nodiscard]] const ServerConfig& config() const& noexcept { return server_config_; }
  [[nodiscard]] const ServerConfig& config() const&& = delete;
  [[nodiscard]] const runtime::SnapshotPublication& publication() const& noexcept {
    return snapshot_publication_;
  }
  [[nodiscard]] const runtime::SnapshotPublication& publication() const&& = delete;
  [[nodiscard]] PeerTrafficPolicy& traffic_policy() & noexcept { return peer_traffic_policy_; }
  [[nodiscard]] PeerTrafficPolicy& traffic_policy() && = delete;
  [[nodiscard]] GameApiRouter& router() & noexcept { return game_api_router_; }
  [[nodiscard]] GameApiRouter& router() && = delete;
  [[nodiscard]] SnapshotEgressBudget& snapshot_egress_budget() & noexcept {
    return snapshot_egress_budget_;
  }
  [[nodiscard]] SnapshotEgressBudget& snapshot_egress_budget() && = delete;
  [[nodiscard]] observability::StructuredLogger& logger() const noexcept { return logger_; }

  // Registers one owned asynchronous session. Must run on the server event loop.
  [[nodiscard]] SessionId register_session(SessionStopAction stop_action);
  void unregister_session(SessionId session_id) noexcept;

  void set_listener_stop_action(ListenerStopAction stop_action);

  // Thread-safe, idempotent request to stop acceptance then drain sessions within the deadline.
  void request_stop() noexcept;

  // Records the exact original exception, closes all server resources, and stops the event loop.
  void fail(std::exception_ptr failure) noexcept;

  [[nodiscard]] bool stopping() const noexcept { return stopping_; }
  [[nodiscard]] std::exception_ptr failure() const noexcept;

private:
  void begin_stop_on_executor();
  void fail_on_executor(std::exception_ptr failure) noexcept;
  void stop_sessions(SessionStopMode mode) noexcept;
  void stop_if_drained() noexcept;

  boost::asio::io_context& io_context_;
  ServerConfig server_config_;
  const runtime::SnapshotPublication& snapshot_publication_;
  observability::StructuredLogger& logger_;
  PeerTrafficPolicy peer_traffic_policy_;
  SnapshotEgressBudget snapshot_egress_budget_;
  RequestIdGenerator request_id_generator_;
  GameApiRouter game_api_router_;
  boost::asio::steady_timer shutdown_timer_;

  std::unordered_map<SessionId, SessionStopAction> sessions_;
  ListenerStopAction listener_stop_action_;
  SessionId next_session_id_{1};
  bool stopping_{false};
  mutable std::mutex failure_mutex_;
  std::exception_ptr failure_;
};

} // namespace blob_royale::server

#endif
