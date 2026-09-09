#include "request_id.hpp"
#include "server_execution_context.hpp"
#include "server_test_fixture.hpp"
#include "snapshot_websocket_session.hpp"

#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace server = blob_royale::server;
namespace fixture = blob_royale::server::test_fixture;
namespace protocol = blob_royale::protocol;
namespace runtime = blob_royale::runtime;
using Tcp = boost::asio::ip::tcp;

namespace {

using namespace std::chrono_literals;

constexpr std::string_view kPeerAddress = "127.0.0.1";
constexpr std::string_view kSnapshotSubprotocol = "blob-royale.snapshot.v1";
constexpr auto kCompletionDeadline = 1s;

class WebSocketSessionHarness final {
public:
  WebSocketSessionHarness()
      : publication_(fixture::initial_publication()),
        acceptor_(server_io_context_, {boost::asio::ip::address_v4::loopback(), 0}),
        lobbies_(fixture::single_lobby(publication_, match_session_.context())),
        server_context_(std::make_shared<server::ServerExecutionContext>(
            server_io_context_, server_config(acceptor_.local_endpoint().port()), lobbies_,
            log_capture_.logger)),
        client_socket_(client_io_context_), server_socket_(server_io_context_) {
    client_socket_.connect(acceptor_.local_endpoint());
    acceptor_.accept(server_socket_);
  }

  WebSocketSessionHarness(const WebSocketSessionHarness&) = delete;
  WebSocketSessionHarness(WebSocketSessionHarness&&) = delete;
  WebSocketSessionHarness& operator=(const WebSocketSessionHarness&) = delete;
  WebSocketSessionHarness& operator=(WebSocketSessionHarness&&) = delete;
  ~WebSocketSessionHarness() = default;

  [[nodiscard]] std::shared_ptr<server::SnapshotWebSocketSession>
  make_session(const std::string_view request_id) {
    server::TcpReservationResult tcp_reservation =
        server_context_->traffic_policy().reserve_tcp_connection(
            kPeerAddress, server::PeerTrafficPolicy::Clock::now());
    server::WebSocketReservationResult websocket_reservation =
        server_context_->traffic_policy().reserve_websocket(
            kPeerAddress, server::PeerTrafficPolicy::Clock::now());
    if (!tcp_reservation.lease.has_value() || !websocket_reservation.lease.has_value()) {
      throw std::runtime_error{"test session could not reserve admission capacity"};
    }
    return std::make_shared<server::SnapshotWebSocketSession>(
        std::move(server_socket_), server_context_, std::string{kPeerAddress},
        protocol::RequestId::create(std::string{request_id}),
        std::move(*websocket_reservation.lease), std::move(*tcp_reservation.lease));
  }

  [[nodiscard]] server::GameApiHttpRequest request(const std::string_view request_id) const {
    server::GameApiHttpRequest result{boost::beast::http::verb::get, "/api/v1/snapshots", 11};
    result.set(boost::beast::http::field::host,
               std::string{"127.0.0.1:"}.append(std::to_string(acceptor_.local_endpoint().port())));
    result.set(boost::beast::http::field::connection, "Upgrade");
    result.set(boost::beast::http::field::upgrade, "websocket");
    result.set(boost::beast::http::field::sec_websocket_version, "13");
    result.set(boost::beast::http::field::sec_websocket_key, "dGhlIHNhbXBsZSBub25jZQ==");
    result.set(boost::beast::http::field::sec_websocket_protocol, kSnapshotSubprotocol);
    result.set("X-Request-ID", request_id);
    return result;
  }

  [[nodiscard]] boost::asio::io_context& server_io_context() noexcept { return server_io_context_; }
  [[nodiscard]] Tcp::socket& client_socket() noexcept { return client_socket_; }
  [[nodiscard]] std::shared_ptr<server::ServerExecutionContext> server_context() const noexcept {
    return server_context_;
  }
  [[nodiscard]] const fixture::LogCapture& log_capture() const noexcept { return log_capture_; }

private:
  [[nodiscard]] static server::ServerConfig server_config(const std::uint16_t port) {
    return server::ServerConfig::create(
        "127.0.0.1", port, 30, fixture::kWorldWidth, fixture::kWorldHeight, fixture::kPlayerRadius,
        {std::string{"127.0.0.1:"}.append(std::to_string(port))}, {}, {});
  }

  boost::asio::io_context server_io_context_{1};
  boost::asio::io_context client_io_context_{1};
  runtime::SnapshotPublication publication_;
  fixture::MatchSessionFixture match_session_;
  fixture::LogCapture log_capture_;
  Tcp::acceptor acceptor_;
  server::LobbyDirectory lobbies_;
  std::shared_ptr<server::ServerExecutionContext> server_context_;
  Tcp::socket client_socket_;
  Tcp::socket server_socket_;
};

} // namespace

TEST_CASE("SnapshotWebSocketSession reports handshake failure without a false close event",
          "[unit][server][websocket][observability][failure]") {
  constexpr std::string_view kRequestId = "unit.websocket.handshake-failure";
  WebSocketSessionHarness harness;
  const std::shared_ptr<server::SnapshotWebSocketSession> session =
      harness.make_session(kRequestId);

  harness.client_socket().set_option(boost::asio::socket_base::linger{true, 0});
  harness.client_socket().close();
  session->run(harness.request(kRequestId));
  REQUIRE(harness.server_io_context().run_one_for(kCompletionDeadline) == 1);

  const std::vector<blob_royale::test_support::CapturedStructuredLogEvent> events =
      harness.log_capture().events();
  REQUIRE(events.size() == 1);
  CHECK(events[0].severity == "warning");
  CHECK(events[0].event == "websocket.handshake_failed");
  CHECK(events[0].request_id == kRequestId);
  CHECK(events[0].connection_id == kRequestId);
  CHECK_FALSE(harness.log_capture().contains_event("websocket.opened"));
  CHECK_FALSE(harness.log_capture().contains_event("websocket.closed"));
  CHECK(harness.server_context()->traffic_policy().active_tcp_connection_count() == 0);
  CHECK(harness.server_context()->traffic_policy().active_websocket_count() == 0);
}
