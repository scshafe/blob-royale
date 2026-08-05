#ifndef BLOB_ROYALE_SERVER_GAME_API_ROUTER_HPP
#define BLOB_ROYALE_SERVER_GAME_API_ROUTER_HPP

#include "http_error.hpp"
#include "peer_traffic_policy.hpp"
#include "request_id.hpp"
#include "request_id_generator.hpp"
#include "server_config.hpp"
#include "snapshot_publication.hpp"

#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace blob_royale::server {

using GameApiHttpRequest = boost::beast::http::request<boost::beast::http::string_body>;
using GameApiHttpResponse = boost::beast::http::response<boost::beast::http::string_body>;

enum class GameApiRouteDisposition {
  kHttpResponse,
  kWebSocketUpgrade,
  kCloseWithoutResponse,
};

// One complete route decision. An upgrade result owns a reserved WebSocket capacity slot.
class GameApiRouteResult final {
public:
  [[nodiscard]] static GameApiRouteResult http_response(GameApiHttpResponse response,
                                                        protocol::RequestId request_id);
  [[nodiscard]] static GameApiRouteResult
  websocket_upgrade(protocol::RequestId request_id, WebSocketAdmissionLease websocket_lease);
  [[nodiscard]] static GameApiRouteResult close_without_response();

  GameApiRouteResult(const GameApiRouteResult&) = delete;
  GameApiRouteResult(GameApiRouteResult&&) noexcept = default;
  GameApiRouteResult& operator=(const GameApiRouteResult&) = delete;
  GameApiRouteResult& operator=(GameApiRouteResult&&) noexcept = default;
  ~GameApiRouteResult() = default;

  [[nodiscard]] GameApiRouteDisposition disposition() const noexcept { return disposition_; }
  [[nodiscard]] GameApiHttpResponse take_response();
  [[nodiscard]] WebSocketAdmissionLease take_websocket_lease();
  [[nodiscard]] const protocol::RequestId& request_id() const&;
  [[nodiscard]] const protocol::RequestId& request_id() const&& = delete;

private:
  GameApiRouteResult(GameApiRouteDisposition disposition,
                     std::optional<GameApiHttpResponse> response,
                     std::optional<protocol::RequestId> request_id,
                     std::optional<WebSocketAdmissionLease> websocket_lease) noexcept;

  GameApiRouteDisposition disposition_;
  std::optional<GameApiHttpResponse> response_;
  std::optional<protocol::RequestId> request_id_;
  std::optional<WebSocketAdmissionLease> websocket_lease_;
};

// canonical: game_api_router -- the only HTTP/1.1 and WebSocket admission decision point.
// It validates untrusted request values once, consumes direct-peer budgets, and emits only the
// four accepted read-only routes. It owns no socket, timer, mutable simulation, or lifecycle hook.
class GameApiRouter final {
public:
  GameApiRouter(const ServerConfig& server_config,
                const runtime::SnapshotPublication& snapshot_publication,
                PeerTrafficPolicy& peer_traffic_policy,
                RequestIdGenerator& request_id_generator) noexcept;

  GameApiRouter(const GameApiRouter&) = delete;
  GameApiRouter(GameApiRouter&&) = delete;
  GameApiRouter& operator=(const GameApiRouter&) = delete;
  GameApiRouter& operator=(GameApiRouter&&) = delete;
  ~GameApiRouter() = default;

  // Routes one completely parsed request. The caller supplies only the socket peer identity.
  // Protocol encoding or invariant failures propagate as hard failures to GameServer::run().
  [[nodiscard]] GameApiRouteResult route(const GameApiHttpRequest& request,
                                         std::string_view peer_address,
                                         PeerTrafficPolicy::Clock::time_point now);

private:
  [[nodiscard]] GameApiRouteResult
  error_response(const GameApiHttpRequest& request, const protocol::RequestId& request_id,
                 protocol::HttpError error,
                 std::optional<std::string_view> allowed_origin = std::nullopt) const;

  const ServerConfig& server_config_;
  const runtime::SnapshotPublication& snapshot_publication_;
  PeerTrafficPolicy& peer_traffic_policy_;
  RequestIdGenerator& request_id_generator_;
};

} // namespace blob_royale::server

#endif
