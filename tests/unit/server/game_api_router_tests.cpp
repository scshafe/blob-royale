#include "game_api_router.hpp"
#include "peer_traffic_policy.hpp"
#include "request_id_generator.hpp"
#include "server_test_fixture.hpp"
#include "simulation_runtime.hpp"

#include <boost/beast/core/string.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace server = blob_royale::server;
namespace fixture = blob_royale::server::test_fixture;
namespace runtime = blob_royale::runtime;
namespace http = boost::beast::http;

namespace {

class RouterFixture final {
public:
  explicit RouterFixture(server::ServerConfig server_config = fixture::loopback_server_config())
      : config(std::move(server_config)), publication(fixture::game_simulation().snapshot()),
        lobbies(fixture::single_lobby(publication, match_session.context())),
        router(config, lobbies, traffic_policy, request_id_generator) {}

  server::ServerConfig config;
  runtime::SnapshotPublication publication;
  fixture::MatchSessionFixture match_session;
  server::LobbyDirectory lobbies;
  server::PeerTrafficPolicy traffic_policy;
  server::RequestIdGenerator request_id_generator;
  server::GameApiRouter router;
};

[[nodiscard]] server::GameApiHttpResponse
route_response(RouterFixture& fixture_state, const server::GameApiHttpRequest& request,
               const server::PeerTrafficPolicy::Clock::time_point now =
                   server::PeerTrafficPolicy::Clock::time_point{}) {
  server::GameApiRouteResult result = fixture_state.router.route(request, "127.0.0.1", now);
  REQUIRE(result.disposition() == server::GameApiRouteDisposition::kHttpResponse);
  return result.take_response();
}

[[nodiscard]] server::GameApiHttpRequest upgrade_request(const std::string_view target) {
  server::GameApiHttpRequest request = fixture::request(http::verb::get, target);
  request.set(http::field::connection, "keep-alive, Upgrade");
  request.set(http::field::upgrade, "websocket");
  request.set(http::field::sec_websocket_version, "13");
  request.set(http::field::sec_websocket_key, "dGhlIHNhbXBsZSBub25jZQ==");
  return request;
}

[[nodiscard]] server::GameApiHttpRequest websocket_request() {
  server::GameApiHttpRequest request = upgrade_request("/api/v1/snapshots");
  request.set(http::field::sec_websocket_protocol, "blob-royale.snapshot.v1");
  return request;
}

[[nodiscard]] server::GameApiHttpRequest session_websocket_request() {
  server::GameApiHttpRequest request = upgrade_request("/api/v3/lobbies/1/session");
  request.set(http::field::sec_websocket_protocol, "blob-royale.session.v3");
  request.set(http::field::origin, "https://game.example.test");
  return request;
}

constexpr std::string_view kTrustedProxyAddress = "127.0.0.1";

// One runtime whose publication has committed a tick, because every upgrade passes v1's readiness
// gate before capacity is reserved.
class ReadyRouterFixture final {
public:
  ReadyRouterFixture()
      : simulation_runtime(fixture::game_simulation()),
        lobbies(fixture::single_lobby(simulation_runtime.snapshot_publication(),
                                      fixture::match_session_context_for(simulation_runtime))),
        router(config, lobbies, traffic_policy, request_id_generator) {
    simulation_runtime.start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!simulation_runtime.snapshot_publication().is_ready() &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::yield();
    }
    REQUIRE(simulation_runtime.snapshot_publication().is_ready());
  }

  ReadyRouterFixture(const ReadyRouterFixture&) = delete;
  ReadyRouterFixture(ReadyRouterFixture&&) = delete;
  ReadyRouterFixture& operator=(const ReadyRouterFixture&) = delete;
  ReadyRouterFixture& operator=(ReadyRouterFixture&&) = delete;
  ~ReadyRouterFixture() { simulation_runtime.stop(); }

  server::ServerConfig config = fixture::loopback_server_config();
  runtime::SimulationRuntime simulation_runtime;
  server::LobbyDirectory lobbies;
  server::PeerTrafficPolicy traffic_policy;
  server::RequestIdGenerator request_id_generator;
  server::GameApiRouter router;
};

// The deployed shape: the trusted proxy is the host's own loopback address, so there is no direct
// loopback peer at all.
class ProxyRouterFixture final {
public:
  ProxyRouterFixture()
      : simulation_runtime(fixture::game_simulation()),
        lobbies(fixture::single_lobby(simulation_runtime.snapshot_publication(),
                                      fixture::match_session_context_for(simulation_runtime))),
        router(config, lobbies, traffic_policy, request_id_generator) {
    simulation_runtime.start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!simulation_runtime.snapshot_publication().is_ready() &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::yield();
    }
    REQUIRE(simulation_runtime.snapshot_publication().is_ready());
  }

  ProxyRouterFixture(const ProxyRouterFixture&) = delete;
  ProxyRouterFixture(ProxyRouterFixture&&) = delete;
  ProxyRouterFixture& operator=(const ProxyRouterFixture&) = delete;
  ProxyRouterFixture& operator=(ProxyRouterFixture&&) = delete;
  ~ProxyRouterFixture() { simulation_runtime.stop(); }

  server::ServerConfig config = fixture::loopback_server_config(
      {"127.0.0.1", "localhost", "[::1]"}, {"https://game.example.test"},
      {std::string{kTrustedProxyAddress}});
  runtime::SimulationRuntime simulation_runtime;
  server::LobbyDirectory lobbies;
  server::PeerTrafficPolicy traffic_policy;
  server::RequestIdGenerator request_id_generator;
  server::GameApiRouter router;
};

[[nodiscard]] server::GameApiHttpResponse
proxy_route_response(ProxyRouterFixture& proxy, const server::GameApiHttpRequest& request) {
  server::GameApiRouteResult result =
      proxy.router.route(request, kTrustedProxyAddress, server::PeerTrafficPolicy::Clock::now());
  REQUIRE(result.disposition() == server::GameApiRouteDisposition::kHttpResponse);
  return result.take_response();
}

constexpr std::array<std::string_view, 4> kRetiredSessionTargets{
    "/api/v2/session", "/api/v2/lobbies", "/api/v2/lobbies/1/session",
    "/api/v2/lobbies/999/session"};

void check_v3_error(const server::GameApiHttpResponse& response, const http::status status,
                    const std::string_view code) {
  CHECK(response.result() == status);
  const boost::json::value document = boost::json::parse(response.body());
  REQUIRE(document.is_object());
  const boost::json::object& envelope = document.as_object();
  CHECK(envelope.at("data").is_null());
  const boost::json::object& meta = envelope.at("meta").as_object();
  CHECK(meta.at("protocol_version").as_string() == "3.0");
  CHECK(meta.at("schema_id").as_string() == "blob-royale://protocol/v3/error-response");
  const boost::json::string& encoded_code = envelope.at("error").as_object().at("code").as_string();
  CHECK(std::string_view{encoded_code.data(), encoded_code.size()} == code);
}

void check_retired_response(const server::GameApiHttpResponse& response,
                            const std::string_view target) {
  check_v3_error(response, http::status::upgrade_required,
                 "PROTOCOL.SESSION_VERSION_UPGRADE_REQUIRED");
  CHECK(boost::beast::iequals(response.at(http::field::upgrade), "websocket"));
  CHECK(boost::beast::iequals(response.at(http::field::connection),
                              response.keep_alive() ? "Upgrade" : "close, Upgrade"));
  const boost::json::value document = boost::json::parse(response.body());
  const boost::json::object& error = document.as_object().at("error").as_object();
  CHECK_FALSE(error.at("retryable").as_bool());
  const boost::json::object& details = error.at("details").as_object();
  REQUIRE(details.size() == 1);
  CHECK(details.at("required_protocol_version").as_string() == "3.0");
  const boost::json::string& message = error.at("message").as_string();
  const std::string_view guidance{message.data(), message.size()};
  CHECK(guidance.find("/api/v3/lobbies/<lobby_id>/session") != std::string_view::npos);
  CHECK(guidance.find("blob-royale.session.v3") != std::string_view::npos);
  CHECK_FALSE(fixture::response_contains(response, target));
  CHECK(response.body().size() < 1'024);
}

} // namespace

TEST_CASE("GameApiRouter returns immutable public configuration with stable headers",
          "[unit][server][router]") {
  RouterFixture state;
  const server::GameApiHttpResponse response =
      route_response(state, fixture::request(http::verb::get, "/api/v1/config"));

  CHECK(response.result() == http::status::ok);
  CHECK(response.at(http::field::content_type) == "application/json; charset=utf-8");
  CHECK(response.at(http::field::cache_control) == "no-store");
  CHECK(response.at("X-Content-Type-Options") == "nosniff");
  CHECK(response.at("X-Request-ID") == "server-test-request-1");
  CHECK(fixture::response_contains(response, "\"width_world_units\":960"));
}

TEST_CASE("GameApiRouter keeps liveness independent from snapshot readiness",
          "[unit][server][router]") {
  RouterFixture state;
  const server::GameApiHttpResponse response =
      route_response(state, fixture::request(http::verb::get, "/api/v1/health/live"));

  CHECK(response.result() == http::status::ok);
  CHECK(fixture::response_contains(response, "\"status\":\"alive\""));
}

TEST_CASE("GameApiRouter reports unavailable publication as retryable readiness failure",
          "[unit][server][router]") {
  RouterFixture state;
  const server::GameApiHttpResponse response =
      route_response(state, fixture::request(http::verb::get, "/api/v1/health/ready"));

  CHECK(response.result() == http::status::service_unavailable);
  CHECK(response.at(http::field::retry_after) == "1");
  CHECK(fixture::response_contains(response, "SERVICE.NOT_READY"));
}

TEST_CASE("GameApiRouter rejects known-route methods without implicit HEAD or OPTIONS",
          "[unit][server][router]") {
  RouterFixture state;
  for (const http::verb method : {http::verb::head, http::verb::post, http::verb::options}) {
    const server::GameApiHttpResponse response =
        route_response(state, fixture::request(method, "/api/v1/config"));
    CHECK(response.result() == http::status::method_not_allowed);
    CHECK(response.at(http::field::allow) == "GET");
  }
}

TEST_CASE("GameApiRouter gives no aliases to legacy, query, or trailing-slash targets",
          "[unit][server][router]") {
  RouterFixture state;
  for (const std::string_view target :
       {"/game-state", "/start-sim", "/api/v1/config?x=1", "/api/v1/config/"}) {
    const server::GameApiHttpResponse response =
        route_response(state, fixture::request(http::verb::get, target));
    CHECK(response.result() == http::status::not_found);
  }
}

TEST_CASE("GameApiRouter applies configured default-port Host semantics",
          "[unit][server][router]") {
  RouterFixture state;
  CHECK(route_response(state, fixture::request(http::verb::get, "/api/v1/health/live", "LOCALHOST"))
            .result() == http::status::ok);
  CHECK(route_response(state,
                       fixture::request(http::verb::get, "/api/v1/health/live", "localhost:8001"))
            .result() == http::status::bad_request);
}

TEST_CASE("GameApiRouter rejects missing and duplicate Host fields", "[unit][server][router]") {
  RouterFixture state;
  server::GameApiHttpRequest missing = fixture::request(http::verb::get, "/api/v1/config");
  missing.erase(http::field::host);
  CHECK(route_response(state, missing).result() == http::status::bad_request);

  server::GameApiHttpRequest duplicate = fixture::request(http::verb::get, "/api/v1/config");
  duplicate.insert(http::field::host, "127.0.0.1:8000");
  CHECK(route_response(state, duplicate).result() == http::status::bad_request);
}

TEST_CASE("GameApiRouter accepts and echoes only an exact configured Origin",
          "[unit][server][router]") {
  RouterFixture state;
  server::GameApiHttpRequest allowed = fixture::request(http::verb::get, "/api/v1/config");
  allowed.set(http::field::origin, "https://game.example.test");
  const server::GameApiHttpResponse allowed_response = route_response(state, allowed);
  CHECK(allowed_response.result() == http::status::ok);
  CHECK(allowed_response.at(http::field::access_control_allow_origin) ==
        "https://game.example.test");
  CHECK(allowed_response.at(http::field::vary) == "Origin");

  server::GameApiHttpRequest rejected = fixture::request(http::verb::get, "/api/v1/config");
  rejected.set(http::field::origin, "https://evil.example.test");
  const server::GameApiHttpResponse rejected_response = route_response(state, rejected);
  CHECK(rejected_response.result() == http::status::forbidden);
  CHECK_FALSE(fixture::response_contains(rejected_response, "evil.example.test"));
}

TEST_CASE("GameApiRouter rejects duplicate or grammatically invalid request IDs",
          "[unit][server][router]") {
  RouterFixture state;
  server::GameApiHttpRequest duplicate = fixture::request(http::verb::get, "/api/v1/config");
  duplicate.insert("X-Request-ID", "second-id");
  CHECK(
      fixture::response_contains(route_response(state, duplicate), "PROTOCOL.INVALID_REQUEST_ID"));

  server::GameApiHttpRequest invalid = fixture::request(http::verb::get, "/api/v1/config");
  invalid.set("X-Request-ID", "contains space");
  CHECK(fixture::response_contains(route_response(state, invalid), "PROTOCOL.INVALID_REQUEST_ID"));
}

TEST_CASE("GameApiRouter generates a schema-safe request ID when absent",
          "[unit][server][router]") {
  RouterFixture state;
  server::GameApiHttpRequest request = fixture::request(http::verb::get, "/api/v1/config");
  request.erase("X-Request-ID");
  const server::GameApiHttpResponse response = route_response(state, request);

  REQUIRE(response.find("X-Request-ID") != response.end());
  CHECK_FALSE(response.at("X-Request-ID").empty());
  CHECK(fixture::response_contains(response, std::string{response.at("X-Request-ID")}));
}

TEST_CASE("GameApiRouter distinguishes conflicting framing from forbidden bodies",
          "[unit][server][router]") {
  RouterFixture state;
  server::GameApiHttpRequest conflict = fixture::request(http::verb::get, "/api/v1/config");
  conflict.set(http::field::content_length, "0");
  conflict.set(http::field::transfer_encoding, "chunked");
  const server::GameApiHttpResponse conflict_response = route_response(state, conflict);
  CHECK(conflict_response.result() == http::status::bad_request);
  CHECK(fixture::response_contains(conflict_response, "PROTOCOL.INVALID_REQUEST"));

  server::GameApiHttpRequest body = fixture::request(http::verb::get, "/api/v1/config");
  body.set(http::field::content_length, "1");
  const server::GameApiHttpResponse body_response = route_response(state, body);
  CHECK(body_response.result() == http::status::payload_too_large);
  CHECK(fixture::response_contains(body_response, "PROTOCOL.PAYLOAD_TOO_LARGE"));
}

TEST_CASE("GameApiRouter closes an oversized target and answers an unsupported HTTP version",
          "[unit][server][router]") {
  RouterFixture state;
  const std::string oversized_target(server::ServerLimits::kRequestTargetMaximumByteCount + 1, 'a');
  const server::GameApiRouteResult oversized =
      state.router.route(fixture::request(http::verb::get, oversized_target), "127.0.0.1",
                         server::PeerTrafficPolicy::Clock::time_point{});
  CHECK(oversized.disposition() == server::GameApiRouteDisposition::kCloseWithoutResponse);

  const server::GameApiHttpResponse unsupported =
      route_response(state, fixture::request(http::verb::get, "/api/v1/config", "localhost", 10));
  CHECK(unsupported.result() == http::status::http_version_not_supported);
  CHECK_FALSE(unsupported.keep_alive());
}

TEST_CASE("GameApiRouter charges oversized parsed targets to the peer request budget",
          "[unit][server][router][rate]") {
  RouterFixture state;
  const auto now = server::PeerTrafficPolicy::Clock::time_point{};
  const std::string oversized_target(server::ServerLimits::kRequestTargetMaximumByteCount + 1, 'a');
  for (std::size_t request = 0;
       request < static_cast<std::size_t>(server::ServerLimits::kHttpRequestBucketCapacity);
       ++request) {
    const server::GameApiRouteResult result =
        state.router.route(fixture::request(http::verb::get, oversized_target), "127.0.0.1", now);
    CHECK(result.disposition() == server::GameApiRouteDisposition::kCloseWithoutResponse);
  }

  const server::GameApiHttpResponse exhausted =
      route_response(state, fixture::request(http::verb::get, "/api/v1/config"), now);
  CHECK(exhausted.result() == http::status::too_many_requests);
  CHECK(fixture::response_contains(exhausted, "PROTOCOL.RATE_LIMITED"));
}

TEST_CASE("GameApiRouter returns 426 for an ordinary snapshots request", "[unit][server][router]") {
  RouterFixture state;
  const server::GameApiHttpResponse response =
      route_response(state, fixture::request(http::verb::get, "/api/v1/snapshots"));
  CHECK(response.result() == http::status::upgrade_required);
  CHECK(response.at(http::field::upgrade) == "websocket");
}

TEST_CASE("GameApiRouter rejects an invalid WebSocket base64 terminal quantum",
          "[unit][server][router][websocket]") {
  RouterFixture state;
  server::GameApiHttpRequest request = websocket_request();
  request.set(http::field::sec_websocket_key, "dGhlIHNhbXBsZSBub25jZR==");
  const server::GameApiHttpResponse response = route_response(state, request);
  CHECK(response.result() == http::status::bad_request);
  CHECK(fixture::response_contains(response, "PROTOCOL.INVALID_REQUEST"));
}

TEST_CASE("GameApiRouter requires the exact snapshot subprotocol",
          "[unit][server][router][websocket]") {
  RouterFixture state;
  server::GameApiHttpRequest request = websocket_request();
  request.set(http::field::sec_websocket_protocol, "blob-royale.snapshot.v3");
  const server::GameApiHttpResponse response = route_response(state, request);
  CHECK(response.result() == http::status::bad_request);
  CHECK(fixture::response_contains(response, "PROTOCOL.SUBPROTOCOL_REQUIRED"));
}

TEST_CASE("GameApiRouter returns the stable WebSocket version error pair",
          "[unit][server][router][websocket]") {
  RouterFixture state;
  server::GameApiHttpRequest request = websocket_request();
  request.set(http::field::sec_websocket_version, "12");
  const server::GameApiHttpResponse response = route_response(state, request);
  CHECK(response.result() == http::status::bad_request);
  CHECK(response.at(http::field::sec_websocket_version) == "13");
  CHECK(fixture::response_contains(response, "PROTOCOL.WEBSOCKET_VERSION_UNSUPPORTED"));
}

TEST_CASE("GameApiRouter consumes a bounded request budget before route selection",
          "[unit][server][router][rate]") {
  RouterFixture state;
  const auto now = server::PeerTrafficPolicy::Clock::time_point{};
  for (std::size_t request = 0;
       request < static_cast<std::size_t>(server::ServerLimits::kHttpRequestBucketCapacity);
       ++request) {
    CHECK(route_response(state, fixture::request(http::verb::get, "/missing"), now).result() ==
          http::status::not_found);
  }
  const server::GameApiHttpResponse denied =
      route_response(state, fixture::request(http::verb::get, "/missing"), now);
  CHECK(denied.result() == http::status::too_many_requests);
  CHECK(denied.at(http::field::retry_after) == "1");
}

TEST_CASE("GameApiRouter transfers and releases a successful WebSocket admission lease",
          "[unit][server][router][websocket][ownership]") {
  runtime::SimulationRuntime simulation_runtime(fixture::game_simulation());
  simulation_runtime.start();
  const auto readiness_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!simulation_runtime.snapshot_publication().is_ready() &&
         std::chrono::steady_clock::now() < readiness_deadline) {
    std::this_thread::yield();
  }
  REQUIRE(simulation_runtime.snapshot_publication().is_ready());

  const server::ServerConfig config = fixture::loopback_server_config();
  const server::LobbyDirectory lobbies =
      fixture::single_lobby(simulation_runtime.snapshot_publication(),
                            fixture::match_session_context_for(simulation_runtime));
  server::PeerTrafficPolicy traffic_policy;
  server::RequestIdGenerator request_id_generator;
  server::GameApiRouter router(config, lobbies, traffic_policy, request_id_generator);
  {
    server::GameApiRouteResult result =
        router.route(websocket_request(), "127.0.0.1", server::PeerTrafficPolicy::Clock::now());
    REQUIRE(result.disposition() == server::GameApiRouteDisposition::kWebSocketUpgrade);
    CHECK(traffic_policy.active_websocket_count() == 1);
  }
  CHECK(traffic_policy.active_websocket_count() == 0);
  simulation_runtime.stop();
}

TEST_CASE("GameApiRouter returns 426 in the v3 envelope for an ordinary session request",
          "[unit][server][router][v3]") {
  RouterFixture state;
  const server::GameApiHttpResponse response =
      route_response(state, fixture::request(http::verb::get, "/api/v3/lobbies/1/session"));
  CHECK(response.result() == http::status::upgrade_required);
  CHECK(response.at(http::field::upgrade) == "websocket");
  CHECK(fixture::response_contains(response, "PROTOCOL.UPGRADE_REQUIRED"));
  // A `/api/v3/` target's failure must name v3, so the envelope is selected by the target's
  // version prefix and not by the route that answered it.
  CHECK(fixture::response_contains(response, "\"protocol_version\":\"3.0\""));
  CHECK(fixture::response_contains(response, "blob-royale://protocol/v3/error-response"));
}

TEST_CASE("GameApiRouter answers an unrouted v3 target in the v3 envelope",
          "[unit][server][router][v3]") {
  RouterFixture state;
  const server::GameApiHttpResponse response =
      route_response(state, fixture::request(http::verb::get, "/api/v3/nonsense"));
  CHECK(response.result() == http::status::not_found);
  CHECK(fixture::response_contains(response, "\"protocol_version\":\"3.0\""));
}

TEST_CASE("GameApiRouter keeps non-session-prefix unknown targets in the v1 envelope",
          "[unit][server][router][v3]") {
  RouterFixture state;
  for (const std::string_view target : {"/nonsense", "/api/v3", "/api/v2"}) {
    const server::GameApiHttpResponse response =
        route_response(state, fixture::request(http::verb::get, target));
    INFO("target " << target);
    CHECK(response.result() == http::status::not_found);
    CHECK(fixture::response_contains(response, "\"protocol_version\":\"1.0\""));
  }
}

TEST_CASE("GameApiRouter retires exact v2 routes before methods handshakes and allocation",
          "[unit][server][router][v3][retirement][trust-boundary]") {
  enum class Handshake { kPlain, kMalformed, kOldToken, kCurrentToken, kMixedTokens };
  for (const std::string_view target : kRetiredSessionTargets) {
    for (const http::verb method : {http::verb::get, http::verb::post}) {
      for (const Handshake handshake :
           {Handshake::kPlain, Handshake::kMalformed, Handshake::kOldToken,
            Handshake::kCurrentToken, Handshake::kMixedTokens}) {
        INFO("target " << target << " method " << http::to_string(method) << " handshake "
                       << static_cast<int>(handshake));
        RouterFixture state;
        server::GameApiHttpRequest request = handshake == Handshake::kPlain
                                                 ? fixture::request(method, target)
                                                 : upgrade_request(target);
        request.method(method);
        if (handshake == Handshake::kMalformed) {
          request.set(http::field::sec_websocket_key, "invalid-key");
          request.set(http::field::sec_websocket_version, "12");
        } else if (handshake == Handshake::kOldToken) {
          request.set(http::field::sec_websocket_protocol, "blob-royale.session.v2");
        } else if (handshake == Handshake::kCurrentToken) {
          request.set(http::field::sec_websocket_protocol, "blob-royale.session.v3");
        } else if (handshake == Handshake::kMixedTokens) {
          request.set(http::field::sec_websocket_protocol,
                      "blob-royale.session.v2, blob-royale.session.v3");
        }
        const auto next_controller = state.match_session.command_sink().next_controller_id();
        const auto directory_size = state.match_session.controller_directory().size();
        const auto room_count = state.lobbies.size();
        const auto sessions = state.lobbies.room(1).session_count();
        const auto websockets = state.traffic_policy.active_websocket_count();
        check_retired_response(route_response(state, request), target);
        CHECK(state.match_session.command_sink().next_controller_id() == next_controller);
        CHECK(state.match_session.controller_directory().size() == directory_size);
        CHECK(state.lobbies.size() == room_count);
        CHECK(state.lobbies.room(1).session_count() == sessions);
        CHECK(state.traffic_policy.active_websocket_count() == websockets);
        // Probe the complete original burst at the same instant: an early retirement must not
        // spend even one upgrade token, including when the old request looks like an upgrade.
        const auto now = server::PeerTrafficPolicy::Clock::time_point{};
        for (std::size_t token = 0;
             token <
             static_cast<std::size_t>(server::ServerLimits::kWebSocketUpgradeBucketCapacity);
             ++token) {
          CHECK(state.traffic_policy.consume_websocket_upgrade("127.0.0.1", now).allowed);
        }
        CHECK_FALSE(state.traffic_policy.consume_websocket_upgrade("127.0.0.1", now).allowed);
      }
    }
  }
}

TEST_CASE("GameApiRouter gives malformed retired routes and the new room-one alias no retirement",
          "[unit][server][router][v3][retirement]") {
  for (const std::string_view target :
       {"/api/v2/lobbies/0/session", "/api/v2/lobbies/01/session", "/api/v2/lobbies/1000/session",
        "/api/v2/lobbies/%31/session", "/api/v2/lobbies/1/session?x=1",
        "/api/v2/lobbies/1/session/", "/api/v2/lobbies//session", "/api/v2/session/",
        "/api/v2/session?x=1", "/api/v2/lobbies?x=1", "/api/v2/lobbies/", "/api/v3/session"}) {
    INFO("target " << target);
    RouterFixture state;
    const server::GameApiHttpResponse response = route_response(state, upgrade_request(target));
    const std::string_view expected =
        target.starts_with("/api/v2/lobbies/") ? "LOBBY.NOT_FOUND" : "PROTOCOL.ROUTE_NOT_FOUND";
    check_v3_error(response, http::status::not_found, expected);
    CHECK_FALSE(fixture::response_contains(response, "SESSION_VERSION_UPGRADE_REQUIRED"));
    CHECK(state.traffic_policy.active_websocket_count() == 0);
  }
}

TEST_CASE("GameApiRouter preserves global security errors in the current session envelope",
          "[unit][server][router][v3][retirement][trust-boundary]") {
  enum class Failure { kHost, kOrigin, kFraming, kBody, kRequestId };
  for (const std::string_view target :
       {"/api/v2/session", "/api/v2/lobbies/01/session", "/api/v3/lobbies/1/session"}) {
    for (const Failure failure : {Failure::kHost, Failure::kOrigin, Failure::kFraming,
                                  Failure::kBody, Failure::kRequestId}) {
      INFO("target " << target << " failure " << static_cast<int>(failure));
      RouterFixture state;
      server::GameApiHttpRequest request = upgrade_request(target);
      http::status expected_status = http::status::bad_request;
      std::string_view expected_code = "PROTOCOL.INVALID_REQUEST";
      switch (failure) {
      case Failure::kHost:
        request.set(http::field::host, "untrusted.example.test");
        break;
      case Failure::kOrigin:
        request.set(http::field::origin, "https://untrusted.example.test");
        expected_status = http::status::forbidden;
        expected_code = "PROTOCOL.ORIGIN_REJECTED";
        break;
      case Failure::kFraming:
        request.set(http::field::content_length, "0");
        request.set(http::field::transfer_encoding, "chunked");
        break;
      case Failure::kBody:
        request.set(http::field::content_length, "1");
        expected_status = http::status::payload_too_large;
        expected_code = "PROTOCOL.PAYLOAD_TOO_LARGE";
        break;
      case Failure::kRequestId:
        request.set("X-Request-ID", "invalid request id");
        expected_code = "PROTOCOL.INVALID_REQUEST_ID";
        break;
      }
      const auto response = route_response(state, request);
      check_v3_error(response, expected_status, expected_code);
      CHECK_FALSE(fixture::response_contains(response, "SESSION_VERSION_UPGRADE_REQUIRED"));
      CHECK_FALSE(fixture::response_contains(response, "untrusted.example.test"));
      CHECK(state.traffic_policy.active_websocket_count() == 0);
    }
  }
}

TEST_CASE("GameApiRouter keeps forwarded identity validation ahead of v2 retirement",
          "[unit][server][router][v3][retirement][trust-boundary]") {
  for (const std::string_view target : {"/api/v2/session", "/api/v3/lobbies/1/session"}) {
    RouterFixture state(fixture::loopback_server_config(
        {"127.0.0.1", "localhost", "[::1]"}, {"https://game.example.test"}, {"127.0.0.1"}));
    server::GameApiHttpRequest request = upgrade_request(target);
    const auto absent = route_response(state, request);
    check_v3_error(absent, http::status::bad_request, "PROTOCOL.INVALID_FORWARDED_CLIENT");
    request.set("X-Forwarded-For", "100.101.102.103, 100.101.102.104");
    const auto invalid = route_response(state, request);
    check_v3_error(invalid, http::status::bad_request, "PROTOCOL.INVALID_FORWARDED_CLIENT");
    CHECK_FALSE(fixture::response_contains(invalid, "100.101.102.103"));
  }
  RouterFixture state(fixture::loopback_server_config(
      {"127.0.0.1", "localhost", "[::1]"}, {"https://game.example.test"}, {"127.0.0.1"}));
  server::GameApiHttpRequest request = upgrade_request("/api/v2/session");
  request.set("X-Forwarded-For", "100.101.102.103");
  // Required-Origin enforcement belongs to active upgrades, after retirement. No Origin here is
  // different from a supplied rejected Origin, which the global gate already rejects above.
  check_retired_response(route_response(state, request), "/api/v2/session");
}

TEST_CASE("GameApiRouter charges retired requests to HTTP budget before version guidance",
          "[unit][server][router][v3][retirement][rate]") {
  for (const std::string_view target : {"/api/v2/session", "/api/v3/lobbies/1/session"}) {
    RouterFixture state;
    for (std::size_t request = 0;
         request < static_cast<std::size_t>(server::ServerLimits::kHttpRequestBucketCapacity);
         ++request) {
      const auto response =
          route_response(state, fixture::request(http::verb::get, "/api/v2/session"));
      CHECK(response.result() == http::status::upgrade_required);
    }
    check_v3_error(route_response(state, fixture::request(http::verb::get, target)),
                   http::status::too_many_requests, "PROTOCOL.RATE_LIMITED");
  }
}

TEST_CASE("GameApiRouter rejects only-old session tokens and selects v3 from mixed tokens",
          "[unit][server][router][v3][retirement][websocket]") {
  ReadyRouterFixture state;
  server::GameApiHttpRequest request = session_websocket_request();
  request.set(http::field::sec_websocket_protocol, "blob-royale.session.v2");
  auto refused = state.router.route(request, "127.0.0.1", server::PeerTrafficPolicy::Clock::now());
  REQUIRE(refused.disposition() == server::GameApiRouteDisposition::kHttpResponse);
  check_v3_error(refused.take_response(), http::status::bad_request,
                 "PROTOCOL.SUBPROTOCOL_REQUIRED");
  for (const std::string_view tokens : {"blob-royale.session.v2, blob-royale.session.v3",
                                        "blob-royale.session.v3, blob-royale.session.v2"}) {
    request.set(http::field::sec_websocket_protocol, tokens);
    const auto admitted =
        state.router.route(request, "127.0.0.1", server::PeerTrafficPolicy::Clock::now());
    REQUIRE(admitted.disposition() == server::GameApiRouteDisposition::kWebSocketUpgrade);
    CHECK(admitted.upgrade_route() == server::GameApiUpgradeRoute::kSessionV3);
    CHECK(admitted.lobby_id() == 1);
  }
}

TEST_CASE("GameApiRouter rejects the v1 subprotocol offered on the v3 session route",
          "[unit][server][router][v3][websocket][trust-boundary]") {
  RouterFixture state;
  server::GameApiHttpRequest request = session_websocket_request();
  request.set(http::field::sec_websocket_protocol, "blob-royale.snapshot.v1");
  const server::GameApiHttpResponse response = route_response(state, request);
  // A token was offered; it belongs to the other route's version, and selecting by
  // first-acceptable-offer would let the client choose which version's semantics this route runs.
  CHECK(response.result() == http::status::bad_request);
  CHECK(fixture::response_contains(response, "PROTOCOL.SUBPROTOCOL_REQUIRED"));
  CHECK(fixture::response_contains(response, "session_subprotocol_required"));
}

TEST_CASE("GameApiRouter rejects the v3 subprotocol offered on the v1 snapshots route",
          "[unit][server][router][v3][websocket][trust-boundary]") {
  RouterFixture state;
  server::GameApiHttpRequest request = websocket_request();
  request.set(http::field::sec_websocket_protocol, "blob-royale.session.v3");
  const server::GameApiHttpResponse response = route_response(state, request);
  CHECK(response.result() == http::status::bad_request);
  CHECK(fixture::response_contains(response, "PROTOCOL.SUBPROTOCOL_REQUIRED"));
  CHECK(fixture::response_contains(response, "snapshot_subprotocol_required"));
}

TEST_CASE("GameApiRouter selects only the requested route's token from an offer list naming both",
          "[unit][server][router][v3][websocket]") {
  ReadyRouterFixture ready;
  for (const auto& [target, expected_route] :
       std::vector<std::pair<std::string_view, server::GameApiUpgradeRoute>>{
           {"/api/v1/snapshots", server::GameApiUpgradeRoute::kSnapshotsV1},
           {"/api/v3/lobbies/1/session", server::GameApiUpgradeRoute::kSessionV3}}) {
    server::GameApiHttpRequest request = upgrade_request(target);
    request.set(http::field::sec_websocket_protocol,
                "blob-royale.snapshot.v1, blob-royale.session.v3");
    server::GameApiRouteResult result =
        ready.router.route(request, "127.0.0.1", server::PeerTrafficPolicy::Clock::now());
    INFO("target " << target);
    REQUIRE(result.disposition() == server::GameApiRouteDisposition::kWebSocketUpgrade);
    CHECK(result.upgrade_route() == expected_route);
  }
}

TEST_CASE("GameApiRouter admits a v3 session upgrade under v1's host, origin, and rate policy",
          "[unit][server][router][v3][websocket]") {
  ReadyRouterFixture ready;
  server::GameApiRouteResult admitted = ready.router.route(session_websocket_request(), "127.0.0.1",
                                                           server::PeerTrafficPolicy::Clock::now());
  REQUIRE(admitted.disposition() == server::GameApiRouteDisposition::kWebSocketUpgrade);
  CHECK(admitted.upgrade_route() == server::GameApiUpgradeRoute::kSessionV3);
  CHECK(admitted.peer_identity().is_direct_peer());

  server::GameApiHttpRequest wrong_host = session_websocket_request();
  wrong_host.set(http::field::host, "elsewhere.example.test");
  server::GameApiRouteResult host_rejected =
      ready.router.route(wrong_host, "127.0.0.1", server::PeerTrafficPolicy::Clock::now());
  REQUIRE(host_rejected.disposition() == server::GameApiRouteDisposition::kHttpResponse);
  CHECK(host_rejected.take_response().result() == http::status::bad_request);

  server::GameApiHttpRequest wrong_origin = session_websocket_request();
  wrong_origin.set(http::field::origin, "https://evil.example.test");
  server::GameApiRouteResult origin_rejected =
      ready.router.route(wrong_origin, "127.0.0.1", server::PeerTrafficPolicy::Clock::now());
  REQUIRE(origin_rejected.disposition() == server::GameApiRouteDisposition::kHttpResponse);
  CHECK(origin_rejected.take_response().result() == http::status::forbidden);
}

TEST_CASE("GameApiRouter rejects methods other than GET on the v3 session route",
          "[unit][server][router][v3]") {
  RouterFixture state;
  for (const http::verb method : {http::verb::head, http::verb::post, http::verb::options}) {
    const server::GameApiHttpResponse response =
        route_response(state, fixture::request(method, "/api/v3/lobbies/1/session"));
    CHECK(response.result() == http::status::method_not_allowed);
    CHECK(response.at(http::field::allow) == "GET");
  }
}

TEST_CASE("GameApiRouter gives the v3 session route no query or trailing-slash aliases",
          "[unit][server][router][v3]") {
  RouterFixture state;
  for (const std::string_view target :
       {"/api/v3/lobbies/1/session/", "/api/v3/lobbies/1/session?x=1", "/api/v3//session"}) {
    const server::GameApiHttpResponse response =
        route_response(state, fixture::request(http::verb::get, target));
    INFO("target " << target);
    CHECK(response.result() == http::status::not_found);
  }
}

TEST_CASE("GameApiRouter refuses a proxy-forwarded connection without a canonical forwarded client",
          "[unit][server][router][v3][trust-boundary]") {
  ProxyRouterFixture proxy;
  server::GameApiHttpRequest absent = session_websocket_request();
  const server::GameApiHttpResponse absent_response = proxy_route_response(proxy, absent);
  CHECK(absent_response.result() == http::status::bad_request);
  CHECK(fixture::response_contains(absent_response, "PROTOCOL.INVALID_FORWARDED_CLIENT"));
  CHECK(fixture::response_contains(absent_response, "\"forwarded_client_reason\":\"absent\""));

  server::GameApiHttpRequest listed = session_websocket_request();
  listed.set("X-Forwarded-For", "100.101.102.103, 10.0.0.1");
  const server::GameApiHttpResponse listed_response = proxy_route_response(proxy, listed);
  CHECK(listed_response.result() == http::status::bad_request);
  CHECK(fixture::response_contains(listed_response,
                                   "\"forwarded_client_reason\":\"multiple_values\""));

  server::GameApiHttpRequest bracketed = session_websocket_request();
  bracketed.set("X-Forwarded-For", "[2001:db8::1]");
  const server::GameApiHttpResponse bracketed_response = proxy_route_response(proxy, bracketed);
  CHECK(bracketed_response.result() == http::status::bad_request);
  CHECK(fixture::response_contains(bracketed_response,
                                   "\"forwarded_client_reason\":\"not_canonical\""));
  // No byte of the received forwarding header may reach a response body.
  CHECK_FALSE(fixture::response_contains(bracketed_response, "2001:db8"));
}

TEST_CASE("GameApiRouter reports a forwarded-client refusal on a v1 target in the v1 envelope",
          "[unit][server][router][v3][trust-boundary]") {
  ProxyRouterFixture proxy;
  const server::GameApiHttpResponse response =
      proxy_route_response(proxy, fixture::request(http::verb::get, "/api/v1/config"));
  CHECK(response.result() == http::status::bad_request);
  // v3 may not widen the closed code registry a v1 client must accept, so a v1 target names
  // PROTOCOL.INVALID_REQUEST and carries the same closed reason as its detail.
  CHECK(fixture::response_contains(response, "\"protocol_version\":\"1.0\""));
  CHECK(fixture::response_contains(response, "PROTOCOL.INVALID_REQUEST"));
  CHECK(fixture::response_contains(response, "forwarded_client_absent"));
  CHECK_FALSE(fixture::response_contains(response, "PROTOCOL.INVALID_FORWARDED_CLIENT"));
}

TEST_CASE(
    "GameApiRouter requires a canonical forwarded client on a trusted loopback readiness probe",
    "[unit][server][router][readiness][trust-boundary]") {
  ProxyRouterFixture proxy;
  // The deployment scripts call this ordinary GET directly from the host. Readiness is not an
  // exception to proxy identity policy, and the runtime is ready before either request is made.
  server::GameApiHttpRequest request = fixture::request(http::verb::get, "/api/v1/health/ready");
  request.set(http::field::host, "127.0.0.1:8000");
  const server::GameApiHttpResponse refused = proxy_route_response(proxy, request);
  CHECK(refused.result() == http::status::bad_request);
  CHECK(fixture::response_contains(refused, "PROTOCOL.INVALID_REQUEST"));
  CHECK(fixture::response_contains(refused, "forwarded_client_absent"));

  request.set("X-Forwarded-For", "127.0.0.1");
  const server::GameApiHttpResponse ready = proxy_route_response(proxy, request);
  CHECK(ready.result() == http::status::ok);
  CHECK(fixture::response_contains(ready, "\"status\":\"ready\""));
  CHECK(fixture::response_contains(ready, "\"error\":null"));
}

TEST_CASE(
    "GameApiRouter never grants the direct-peer Origin relaxation to a trusted loopback proxy",
    "[unit][server][router][v3][trust-boundary]") {
  // The deployed proxy is loopback. Loopback-first classification would put exactly the deployed
  // configuration into the direct arm, where an absent Origin is allowed.
  ProxyRouterFixture proxy;
  server::GameApiHttpRequest without_origin = session_websocket_request();
  without_origin.erase(http::field::origin);
  without_origin.set("X-Forwarded-For", "100.101.102.103");
  const server::GameApiHttpResponse response = proxy_route_response(proxy, without_origin);
  CHECK(response.result() == http::status::forbidden);
  CHECK(fixture::response_contains(response, "PROTOCOL.ORIGIN_REJECTED"));

  // The identical request from a peer that is *not* configured as a trusted proxy is admitted,
  // which is what makes the precedence rule the only difference between the two outcomes.
  ReadyRouterFixture direct;
  server::GameApiHttpRequest direct_request = session_websocket_request();
  direct_request.erase(http::field::origin);
  server::GameApiRouteResult admitted =
      direct.router.route(direct_request, "127.0.0.1", server::PeerTrafficPolicy::Clock::now());
  CHECK(admitted.disposition() == server::GameApiRouteDisposition::kWebSocketUpgrade);
}

TEST_CASE("GameApiRouter accounts a proxy-forwarded upgrade to the forwarded client address",
          "[unit][server][router][v3][rate][trust-boundary]") {
  ProxyRouterFixture proxy;
  const auto now = server::PeerTrafficPolicy::Clock::now();
  server::GameApiHttpRequest first = session_websocket_request();
  first.set("X-Forwarded-For", "100.101.102.103");
  server::GameApiHttpRequest second = session_websocket_request();
  second.set("X-Forwarded-For", "100.101.102.104");

  // Two distinct forwarded clients each get their own upgrade bucket. Sharing the proxy's socket
  // address would collapse every tailnet player into one principal, which is the accounting
  // collapse the identity rules exist to undo.
  for (std::size_t upgrade = 0;
       upgrade < static_cast<std::size_t>(server::ServerLimits::kWebSocketUpgradeBucketCapacity);
       ++upgrade) {
    server::GameApiRouteResult result = proxy.router.route(first, kTrustedProxyAddress, now);
    REQUIRE(result.disposition() == server::GameApiRouteDisposition::kWebSocketUpgrade);
  }
  server::GameApiRouteResult exhausted = proxy.router.route(first, kTrustedProxyAddress, now);
  REQUIRE(exhausted.disposition() == server::GameApiRouteDisposition::kHttpResponse);
  CHECK(exhausted.take_response().result() == http::status::too_many_requests);

  server::GameApiRouteResult other = proxy.router.route(second, kTrustedProxyAddress, now);
  CHECK(other.disposition() == server::GameApiRouteDisposition::kWebSocketUpgrade);
}

namespace {

// Two rooms, each a two-seat lobby with nobody in it. Room 1 always serves; room 2 serves only when
// asked to, so one fixture covers the directory's `healthy` column, `503 LOBBY.UNAVAILABLE`, and an
// upgrade admitted into a room other than 1.
class TwoRoomRouterFixture final {
public:
  explicit TwoRoomRouterFixture(const bool second_room_serves)
      : first_runtime(fixture::lobby_game_simulation(2)),
        second_runtime(fixture::lobby_game_simulation(2)),
        lobbies(server::LobbyDirectory::create(rooms())),
        router(config, lobbies, traffic_policy, request_id_generator) {
    first_runtime.start();
    await_ready(first_runtime);
    if (second_room_serves) {
      second_runtime.start();
      await_ready(second_runtime);
    }
  }

  TwoRoomRouterFixture(const TwoRoomRouterFixture&) = delete;
  TwoRoomRouterFixture(TwoRoomRouterFixture&&) = delete;
  TwoRoomRouterFixture& operator=(const TwoRoomRouterFixture&) = delete;
  TwoRoomRouterFixture& operator=(TwoRoomRouterFixture&&) = delete;
  ~TwoRoomRouterFixture() {
    first_runtime.stop();
    second_runtime.stop();
  }

  // A complete, well-formed v3 session upgrade offered to `target`, from the direct loopback peer.
  [[nodiscard]] server::GameApiRouteResult upgrade(const std::string_view target) {
    server::GameApiHttpRequest request = upgrade_request(target);
    request.set(http::field::sec_websocket_protocol, "blob-royale.session.v3");
    request.set(http::field::origin, "https://game.example.test");
    return router.route(request, "127.0.0.1", server::PeerTrafficPolicy::Clock::now());
  }

  [[nodiscard]] server::GameApiHttpResponse refused(const std::string_view target) {
    server::GameApiRouteResult result = upgrade(target);
    REQUIRE(result.disposition() == server::GameApiRouteDisposition::kHttpResponse);
    return result.take_response();
  }

  server::ServerConfig config = fixture::loopback_server_config();
  runtime::SimulationRuntime first_runtime;
  runtime::SimulationRuntime second_runtime;
  server::LobbyDirectory lobbies;
  server::PeerTrafficPolicy traffic_policy;
  server::RequestIdGenerator request_id_generator;
  server::GameApiRouter router;

private:
  [[nodiscard]] std::vector<server::LobbyDirectory::Room> rooms() {
    std::vector<server::LobbyDirectory::Room> result;
    result.push_back(fixture::room_of(first_runtime, 1));
    result.push_back(fixture::room_of(second_runtime, 2));
    return result;
  }

  static void await_ready(runtime::SimulationRuntime& simulation_runtime) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!simulation_runtime.snapshot_publication().is_ready() &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::yield();
    }
    REQUIRE(simulation_runtime.snapshot_publication().is_ready());
  }
};

constexpr std::string_view kV3ErrorSchemaId = "blob-royale://protocol/v3/error-response";

[[nodiscard]] server::GameApiHttpResponse response_of(server::GameApiRouteResult result) {
  REQUIRE(result.disposition() == server::GameApiRouteDisposition::kHttpResponse);
  return result.take_response();
}

} // namespace

TEST_CASE("GameApiRouter lists every room in the directory with its census and health",
          "[unit][server][router][v3][lobbies]") {
  TwoRoomRouterFixture state{false};
  state.lobbies.room(1).count_session_in();
  const server::GameApiHttpResponse response =
      response_of(state.router.route(fixture::request(http::verb::get, "/api/v3/lobbies"),
                                     "127.0.0.1", server::PeerTrafficPolicy::Clock::now()));
  state.lobbies.room(1).count_session_out();

  CHECK(response.result() == http::status::ok);
  CHECK(response.at(http::field::content_type) == "application/json; charset=utf-8");
  CHECK(response.at(http::field::cache_control) == "no-store");
  CHECK(fixture::response_contains(response, R"("error":null)"));
  CHECK(fixture::response_contains(
      response,
      R"("meta":{"protocol_version":"3.0","schema_id":"blob-royale://protocol/v3/lobby-directory","request_id":"server-test-request-1"})"));
  // Room 1 is serving, past tick zero, with the one session counted in. Room 2 never started, so it
  // lists its initial world -- the two seats it was configured with, nobody in them -- at tick zero
  // and unhealthy, which is exactly what a join to it would be told with `503`.
  CHECK(fixture::response_contains(
      response,
      R"("lobbies":[{"lobby_id":1,"mode":"idle","map":"arena-960x640","phase":"lobby","phase_started_tick":0,"tick_sequence":)"));
  CHECK(fixture::response_contains(
      response,
      R"("seat_count":2,"seat_count_maximum":32,"filled_seat_count":0,"npc_seat_count":0,"session_count":1,"healthy":true})"));
  CHECK_FALSE(fixture::response_contains(
      response,
      R"("lobby_id":1,"mode":"idle","map":"arena-960x640","phase":"lobby","phase_started_tick":0,"tick_sequence":0,)"));
  CHECK(fixture::response_contains(
      response,
      R"({"lobby_id":2,"mode":"idle","map":"arena-960x640","phase":"lobby","phase_started_tick":0,"tick_sequence":0,"seat_count":2,"seat_count_maximum":32,"filled_seat_count":0,"npc_seat_count":0,"session_count":0,"healthy":false}]})"));
}

TEST_CASE("GameApiRouter answers the directory only to GET, in the v3 envelope",
          "[unit][server][router][v3][lobbies]") {
  TwoRoomRouterFixture state{false};
  const server::GameApiHttpResponse response =
      response_of(state.router.route(fixture::request(http::verb::post, "/api/v3/lobbies"),
                                     "127.0.0.1", server::PeerTrafficPolicy::Clock::now()));
  CHECK(response.result() == http::status::method_not_allowed);
  CHECK(response.at(http::field::allow) == "GET");
  CHECK(fixture::response_contains(response, "PROTOCOL.METHOD_NOT_ALLOWED"));
  CHECK(fixture::response_contains(response, kV3ErrorSchemaId));
}

TEST_CASE("GameApiRouter matches the room target by grammar and answers everything else "
          "404 LOBBY.NOT_FOUND",
          "[unit][server][router][v3][lobbies]") {
  TwoRoomRouterFixture state{true};
  // Each offered as a complete upgrade, so the 404 provably precedes every handshake rule and the
  // upgrade bucket: the classic wrong matcher -- a leading zero, a fourth digit, a trailing slash,
  // percent-encoding, a query, an empty segment, a neighbour that is not exact -- and the two
  // well-formed ids the two-room directory does not hold.
  for (const std::string_view target :
       {"/api/v3/lobbies/0/session", "/api/v3/lobbies/01/session", "/api/v3/lobbies/1000/session",
        "/api/v3/lobbies/1/session/", "/api/v3/lobbies/%31/session",
        "/api/v3/lobbies/1/session?x=1", "/api/v3/lobbies//session", "/api/v3/lobbies/",
        "/api/v3/lobbies/1", "/api/v3/lobbies/1/sessions", "/api/v3/lobbies/ 1/session",
        "/api/v3/lobbies/3/session", "/api/v3/lobbies/999/session"}) {
    INFO(target);
    const server::GameApiHttpResponse response = state.refused(target);
    CHECK(response.result() == http::status::not_found);
    CHECK(fixture::response_contains(response, R"("code":"LOBBY.NOT_FOUND")"));
    CHECK(fixture::response_contains(response, R"("retryable":false)"));
    CHECK(fixture::response_contains(response, R"("details":{})"));
    CHECK(fixture::response_contains(response, kV3ErrorSchemaId));
  }
  // Outside the prefix nothing is a lobby: an unrouted v3 target keeps its plain 404.
  const server::GameApiHttpResponse unrouted = state.refused("/api/v3/lobbiesx");
  CHECK(unrouted.result() == http::status::not_found);
  CHECK(fixture::response_contains(unrouted, "PROTOCOL.ROUTE_NOT_FOUND"));
  // And a room target that exists is a session target like room 1's: an ordinary GET is `426`.
  const server::GameApiHttpResponse ordinary =
      response_of(state.router.route(fixture::request(http::verb::get, "/api/v3/lobbies/2/session"),
                                     "127.0.0.1", server::PeerTrafficPolicy::Clock::now()));
  CHECK(ordinary.result() == http::status::upgrade_required);
  CHECK(fixture::response_contains(ordinary, "PROTOCOL.UPGRADE_REQUIRED"));
}

TEST_CASE("GameApiRouter admits a room target into the named room and /api/v3/lobbies/1/session "
          "into room 1",
          "[unit][server][router][v3][lobbies]") {
  TwoRoomRouterFixture state{true};
  {
    server::GameApiRouteResult result = state.upgrade("/api/v3/lobbies/2/session");
    REQUIRE(result.disposition() == server::GameApiRouteDisposition::kWebSocketUpgrade);
    CHECK(result.upgrade_route() == server::GameApiUpgradeRoute::kSessionV3);
    CHECK(result.lobby_id() == 2);
  }
  {
    server::GameApiRouteResult result = state.upgrade("/api/v3/lobbies/1/session");
    REQUIRE(result.disposition() == server::GameApiRouteDisposition::kWebSocketUpgrade);
    CHECK(result.upgrade_route() == server::GameApiUpgradeRoute::kSessionV3);
    CHECK(result.lobby_id() == 1);
  }
  {
    // The v1 stream reads room 1, and says so the same way.
    server::GameApiRouteResult result = state.router.route(websocket_request(), "127.0.0.1",
                                                           server::PeerTrafficPolicy::Clock::now());
    REQUIRE(result.disposition() == server::GameApiRouteDisposition::kWebSocketUpgrade);
    CHECK(result.upgrade_route() == server::GameApiUpgradeRoute::kSnapshotsV1);
    CHECK(result.lobby_id() == 1);
  }
  CHECK(state.traffic_policy.active_websocket_count() == 0);
}

TEST_CASE("GameApiRouter refuses a room that is not serving with 503 LOBBY.UNAVAILABLE naming it",
          "[unit][server][router][v3][lobbies]") {
  TwoRoomRouterFixture state{false};
  const server::GameApiHttpResponse refused = state.refused("/api/v3/lobbies/2/session");
  CHECK(refused.result() == http::status::service_unavailable);
  CHECK(refused.at(http::field::retry_after) == "1");
  CHECK(fixture::response_contains(refused, R"("code":"LOBBY.UNAVAILABLE")"));
  CHECK(fixture::response_contains(refused, R"("retryable":true)"));
  CHECK(fixture::response_contains(refused, R"("details":{"lobby_id":2})"));
  CHECK(fixture::response_contains(refused, kV3ErrorSchemaId));
  // A failed or unready room refuses only its own joins.
  server::GameApiRouteResult admitted = state.upgrade("/api/v3/lobbies/1/session");
  REQUIRE(admitted.disposition() == server::GameApiRouteDisposition::kWebSocketUpgrade);
  CHECK(admitted.lobby_id() == 1);
}

TEST_CASE("GameApiRouter refuses a full room with 409 LOBBY.FULL and admits it again when a "
          "session leaves",
          "[unit][server][router][v3][lobbies]") {
  TwoRoomRouterFixture state{true};
  state.lobbies.room(1).count_session_in();
  state.lobbies.room(1).count_session_in();

  const server::GameApiHttpResponse refused = state.refused("/api/v3/lobbies/1/session");
  CHECK(refused.result() == http::status::conflict);
  CHECK(refused.find(http::field::retry_after) == refused.end());
  CHECK(fixture::response_contains(refused, R"("code":"LOBBY.FULL")"));
  CHECK(fixture::response_contains(refused, R"("retryable":true)"));
  CHECK(fixture::response_contains(refused, R"("details":{"lobby_id":1})"));
  CHECK(fixture::response_contains(refused, kV3ErrorSchemaId));
  {
    // The other room has its two seats free.
    server::GameApiRouteResult admitted = state.upgrade("/api/v3/lobbies/2/session");
    REQUIRE(admitted.disposition() == server::GameApiRouteDisposition::kWebSocketUpgrade);
    CHECK(admitted.lobby_id() == 2);
  }
  state.lobbies.room(1).count_session_out();
  {
    server::GameApiRouteResult admitted = state.upgrade("/api/v3/lobbies/1/session");
    REQUIRE(admitted.disposition() == server::GameApiRouteDisposition::kWebSocketUpgrade);
    CHECK(admitted.lobby_id() == 1);
  }
  state.lobbies.room(1).count_session_out();
}

TEST_CASE("GameApiRouter never calls a room with no lobby full",
          "[unit][server][router][v3][lobbies]") {
  // `sandbox` publishes an empty roster: there is no seat for anybody to be refused from, so the
  // seat rule does not apply and only the connection caps bound the room.
  ReadyRouterFixture state;
  state.lobbies.room(1).count_session_in();
  state.lobbies.room(1).count_session_in();
  state.lobbies.room(1).count_session_in();
  server::GameApiRouteResult admitted = state.router.route(session_websocket_request(), "127.0.0.1",
                                                           server::PeerTrafficPolicy::Clock::now());
  REQUIRE(admitted.disposition() == server::GameApiRouteDisposition::kWebSocketUpgrade);
  CHECK(admitted.lobby_id() == 1);
  state.lobbies.room(1).count_session_out();
  state.lobbies.room(1).count_session_out();
  state.lobbies.room(1).count_session_out();
}

TEST_CASE("GameApiRouter retirement does not reveal full unavailable or absent rooms",
          "[unit][server][router][v3][retirement][lobbies]") {
  TwoRoomRouterFixture state{false};
  state.lobbies.room(1).count_session_in();
  state.lobbies.room(1).count_session_in();
  const auto first_next = state.first_runtime.command_sink().next_controller_id();
  const auto second_next = state.second_runtime.command_sink().next_controller_id();
  for (const std::string_view target :
       {"/api/v2/lobbies/1/session", "/api/v2/lobbies/2/session", "/api/v2/lobbies/999/session"}) {
    check_retired_response(state.refused(target), target);
  }
  CHECK(state.lobbies.room(1).session_count() == 2);
  CHECK(state.lobbies.room(2).session_count() == 0);
  CHECK(state.first_runtime.controller_directory().size() == 0);
  CHECK(state.second_runtime.controller_directory().size() == 0);
  CHECK(state.first_runtime.command_sink().next_controller_id() == first_next);
  CHECK(state.second_runtime.command_sink().next_controller_id() == second_next);
  CHECK(state.traffic_policy.active_websocket_count() == 0);
  check_v3_error(state.refused("/api/v3/lobbies/1/session"), http::status::conflict, "LOBBY.FULL");
  check_v3_error(state.refused("/api/v3/lobbies/2/session"), http::status::service_unavailable,
                 "LOBBY.UNAVAILABLE");
  state.lobbies.room(1).count_session_out();
  state.lobbies.room(1).count_session_out();
}
