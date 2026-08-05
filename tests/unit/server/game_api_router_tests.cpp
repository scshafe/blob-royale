#include "game_api_router.hpp"
#include "peer_traffic_policy.hpp"
#include "request_id_generator.hpp"
#include "server_test_fixture.hpp"
#include "simulation_runtime.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <thread>

namespace server = blob_royale::server;
namespace fixture = blob_royale::server::test_fixture;
namespace runtime = blob_royale::runtime;
namespace http = boost::beast::http;

namespace {

class RouterFixture final {
public:
  RouterFixture()
      : config(fixture::loopback_server_config()),
        publication(fixture::game_simulation().snapshot()),
        router(config, publication, traffic_policy, request_id_generator) {}

  server::ServerConfig config;
  runtime::SnapshotPublication publication;
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

[[nodiscard]] server::GameApiHttpRequest websocket_request() {
  server::GameApiHttpRequest request = fixture::request(http::verb::get, "/api/v1/snapshots");
  request.set(http::field::connection, "keep-alive, Upgrade");
  request.set(http::field::upgrade, "websocket");
  request.set(http::field::sec_websocket_version, "13");
  request.set(http::field::sec_websocket_key, "dGhlIHNhbXBsZSBub25jZQ==");
  request.set(http::field::sec_websocket_protocol, "blob-royale.snapshot.v1");
  return request;
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
  request.set(http::field::sec_websocket_protocol, "blob-royale.snapshot.v2");
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
  server::PeerTrafficPolicy traffic_policy;
  server::RequestIdGenerator request_id_generator;
  server::GameApiRouter router(config, simulation_runtime.snapshot_publication(), traffic_policy,
                               request_id_generator);
  {
    server::GameApiRouteResult result =
        router.route(websocket_request(), "127.0.0.1", server::PeerTrafficPolicy::Clock::now());
    REQUIRE(result.disposition() == server::GameApiRouteDisposition::kWebSocketUpgrade);
    CHECK(traffic_policy.active_websocket_count() == 1);
  }
  CHECK(traffic_policy.active_websocket_count() == 0);
  simulation_runtime.stop();
}
