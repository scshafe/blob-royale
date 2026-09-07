#ifndef BLOB_ROYALE_SERVER_GAME_API_ROUTER_HPP
#define BLOB_ROYALE_SERVER_GAME_API_ROUTER_HPP

#include "http_error.hpp"
#include "peer_identity.hpp"
#include "peer_traffic_policy.hpp"
#include "request_id.hpp"
#include "request_id_generator.hpp"
#include "server_config.hpp"
#include "snapshot_publication.hpp"
#include "v2_http_error.hpp"

#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>

#include <cstdint>
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

// Which upgradeable route an admitted handshake selected.
//
// **The route selects the subprotocol, never the reverse.** A client that offers both tokens on
// either route gets only that route's token, and an offer list carrying only the other route's
// token is `400 PROTOCOL.SUBPROTOCOL_REQUIRED` even though a token was offered. Selecting by
// first-acceptable-offer is the classic way this check is written wrong, and it would let a client
// choose which version's semantics a route runs -- which means choosing the weaker one
// (`docs/protocol/v2.md` § "Upgrade validation deltas").
enum class GameApiUpgradeRoute : std::uint8_t {
  // `/api/v1/snapshots` with `blob-royale.snapshot.v1`: read-only, accepts no client data.
  kSnapshotsV1 = 0,
  // `/api/v2/session` with `blob-royale.session.v2`: joins the match and accepts commands.
  kSessionV2 = 1,
};

[[nodiscard]] constexpr std::string_view
game_api_upgrade_route_name(const GameApiUpgradeRoute route) noexcept {
  switch (route) {
  case GameApiUpgradeRoute::kSnapshotsV1:
    return "/api/v1/snapshots";
  case GameApiUpgradeRoute::kSessionV2:
    return "/api/v2/session";
  }
  return "game_api_upgrade_route_invalid";
}

[[nodiscard]] constexpr std::string_view
game_api_upgrade_subprotocol(const GameApiUpgradeRoute route) noexcept {
  switch (route) {
  case GameApiUpgradeRoute::kSnapshotsV1:
    return "blob-royale.snapshot.v1";
  case GameApiUpgradeRoute::kSessionV2:
    return "blob-royale.session.v2";
  }
  return "game_api_upgrade_subprotocol_invalid";
}

// One complete route decision. An upgrade result owns a reserved WebSocket capacity slot.
class GameApiRouteResult final {
public:
  [[nodiscard]] static GameApiRouteResult http_response(GameApiHttpResponse response,
                                                        protocol::RequestId request_id);
  [[nodiscard]] static GameApiRouteResult websocket_upgrade(protocol::RequestId request_id,
                                                            WebSocketAdmissionLease websocket_lease,
                                                            GameApiUpgradeRoute upgrade_route,
                                                            PeerIdentity peer_identity);
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

  // Present exactly on an upgrade result. The route is what the session class is chosen by, and
  // the identity is carried rather than re-derived so the boundary decides trust once.
  [[nodiscard]] GameApiUpgradeRoute upgrade_route() const;
  [[nodiscard]] const PeerIdentity& peer_identity() const&;
  [[nodiscard]] const PeerIdentity& peer_identity() const&& = delete;

private:
  GameApiRouteResult(GameApiRouteDisposition disposition,
                     std::optional<GameApiHttpResponse> response,
                     std::optional<protocol::RequestId> request_id,
                     std::optional<WebSocketAdmissionLease> websocket_lease,
                     std::optional<GameApiUpgradeRoute> upgrade_route,
                     std::optional<PeerIdentity> peer_identity) noexcept;

  GameApiRouteDisposition disposition_;
  std::optional<GameApiHttpResponse> response_;
  std::optional<protocol::RequestId> request_id_;
  std::optional<WebSocketAdmissionLease> websocket_lease_;
  std::optional<GameApiUpgradeRoute> upgrade_route_;
  std::optional<PeerIdentity> peer_identity_;
};

// canonical: game_api_router -- the only HTTP/1.1 and WebSocket admission decision point.
//
// It validates untrusted request values once, derives the connection's accounting principal before
// any route runs, consumes that principal's budgets, and emits only the four accepted read-only v1
// routes plus the one v2 session route. It owns no socket, timer, mutable simulation, or lifecycle
// hook.
//
// **v2 is admitted under v1's policies, not beside them.** The same host allowlist, request-ID
// grammar, origin allowlist, readiness gate, connection caps, and token buckets decide a
// `/api/v2/session` upgrade, and it consumes one HTTP request token and one upgrade token from the
// same buckets. What v2 adds here is exactly three things: the route/subprotocol pairing, the
// proxy-forwarded accounting principal, and the v2 error envelope on `/api/v2/` targets
// (`docs/protocol/v2.md` § "Deltas from v1").
// related: peer_identity.hpp -- the identity derivation this runs before any route.
// related: match_session_context.hpp -- the capability the admitted v2 session runs on.
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
  // One error envelope, in the version the request target selects. A target whose path begins
  // `/api/v2/` gets the v2 envelope and every other target, including an unrouted one, gets v1's,
  // so a 404 is never ambiguous about which schema it validates against.
  [[nodiscard]] GameApiRouteResult
  error_response(const GameApiHttpRequest& request, const protocol::RequestId& request_id,
                 protocol::HttpError error,
                 std::optional<std::string_view> allowed_origin = std::nullopt) const;

  // The one row v2 adds, rendered in whichever envelope the target selects. On a `/api/v2/` target
  // it is `PROTOCOL.INVALID_FORWARDED_CLIENT` with `forwarded_client_reason`; on a v1 target it is
  // `PROTOCOL.INVALID_REQUEST` carrying the same closed reason, because v2 may not widen the code
  // registry a v1 client must accept.
  [[nodiscard]] GameApiRouteResult
  forwarded_client_error_response(const GameApiHttpRequest& request,
                                  const protocol::RequestId& request_id,
                                  protocol::ForwardedClientReason reason) const;

  const ServerConfig& server_config_;
  const runtime::SnapshotPublication& snapshot_publication_;
  PeerTrafficPolicy& peer_traffic_policy_;
  RequestIdGenerator& request_id_generator_;
};

} // namespace blob_royale::server

#endif
