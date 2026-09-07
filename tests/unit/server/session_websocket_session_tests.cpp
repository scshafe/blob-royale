#include "peer_identity.hpp"
#include "request_id.hpp"
#include "server_execution_context.hpp"
#include "server_test_fixture.hpp"
#include "session_websocket_session.hpp"

#include "controller_directory.hpp"
#include "controller_id.hpp"

#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/websocket/stream.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace server = blob_royale::server;
namespace fixture = blob_royale::server::test_fixture;
namespace protocol = blob_royale::protocol;
namespace runtime = blob_royale::runtime;
namespace simulation = blob_royale::simulation;
using Tcp = boost::asio::ip::tcp;

namespace {

using namespace std::chrono_literals;

constexpr std::string_view kPeerAddress = "127.0.0.1";
constexpr std::string_view kSessionSubprotocol = "blob-royale.session.v2";
constexpr auto kCompletionDeadline = 1s;

class SessionHarness final {
public:
  SessionHarness()
      : publication_(fixture::initial_publication()),
        acceptor_(server_io_context_, {boost::asio::ip::address_v4::loopback(), 0}),
        server_context_(std::make_shared<server::ServerExecutionContext>(
            server_io_context_, server_config(acceptor_.local_endpoint().port()), publication_,
            match_session_.context(), log_capture_.logger)),
        client_socket_(client_io_context_), server_socket_(server_io_context_) {
    client_socket_.connect(acceptor_.local_endpoint());
    acceptor_.accept(server_socket_);
  }

  SessionHarness(const SessionHarness&) = delete;
  SessionHarness(SessionHarness&&) = delete;
  SessionHarness& operator=(const SessionHarness&) = delete;
  SessionHarness& operator=(SessionHarness&&) = delete;
  ~SessionHarness() = default;

  [[nodiscard]] std::shared_ptr<server::SessionWebSocketSession>
  make_session(const std::string_view request_id, server::PeerIdentity peer_identity) {
    server::TcpReservationResult tcp_reservation =
        server_context_->traffic_policy().reserve_tcp_connection(
            kPeerAddress, server::PeerTrafficPolicy::Clock::now());
    server::WebSocketReservationResult websocket_reservation =
        server_context_->traffic_policy().reserve_websocket(
            kPeerAddress, server::PeerTrafficPolicy::Clock::now());
    if (!tcp_reservation.lease.has_value() || !websocket_reservation.lease.has_value()) {
      throw std::runtime_error{"test session could not reserve admission capacity"};
    }
    return std::make_shared<server::SessionWebSocketSession>(
        std::move(server_socket_), server_context_, std::string{kPeerAddress},
        protocol::RequestId::create(std::string{request_id}), std::move(peer_identity),
        std::move(*websocket_reservation.lease), std::move(*tcp_reservation.lease));
  }

  [[nodiscard]] server::GameApiHttpRequest request(const std::string_view request_id) const {
    server::GameApiHttpRequest result{boost::beast::http::verb::get, "/api/v2/session", 11};
    result.set(boost::beast::http::field::host,
               std::string{"127.0.0.1:"}.append(std::to_string(acceptor_.local_endpoint().port())));
    result.set(boost::beast::http::field::connection, "Upgrade");
    result.set(boost::beast::http::field::upgrade, "websocket");
    result.set(boost::beast::http::field::sec_websocket_version, "13");
    result.set(boost::beast::http::field::sec_websocket_key, "dGhlIHNhbXBsZSBub25jZQ==");
    result.set(boost::beast::http::field::sec_websocket_protocol, kSessionSubprotocol);
    result.set("X-Request-ID", request_id);
    return result;
  }

  [[nodiscard]] boost::asio::io_context& server_io_context() noexcept { return server_io_context_; }
  [[nodiscard]] Tcp::socket& client_socket() noexcept { return client_socket_; }
  [[nodiscard]] std::shared_ptr<server::ServerExecutionContext> server_context() const noexcept {
    return server_context_;
  }
  [[nodiscard]] const runtime::ControllerDirectory& controller_directory() const noexcept {
    return match_session_.controller_directory();
  }
  [[nodiscard]] runtime::CommandSink& command_sink() const noexcept {
    return match_session_.command_sink();
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
  std::shared_ptr<server::ServerExecutionContext> server_context_;
  Tcp::socket client_socket_;
  Tcp::socket server_socket_;
};

// One WebSocket client that completes the handshake against the harness's server socket.
class SessionClient final {
public:
  explicit SessionClient(Tcp::socket socket) : websocket_(std::move(socket)) {}

  void handshake(const std::uint16_t port, const std::string_view request_id) {
    websocket_.set_option(boost::beast::websocket::stream_base::decorator(
        [request_id = std::string{request_id}](boost::beast::websocket::request_type& request) {
          request.set(boost::beast::http::field::sec_websocket_protocol, kSessionSubprotocol);
          request.set("X-Request-ID", request_id);
        }));
    websocket_.handshake(std::string{"127.0.0.1:"}.append(std::to_string(port)), "/api/v2/session");
  }

  [[nodiscard]] boost::beast::websocket::stream<Tcp::socket>& stream() noexcept {
    return websocket_;
  }

private:
  boost::beast::websocket::stream<Tcp::socket> websocket_;
};

[[nodiscard]] server::PeerIdentity direct_identity() {
  return server::PeerIdentity::direct(std::string{kPeerAddress});
}

// Drives the server loop until a condition holds or the deadline passes.
//
// A single `poll` is not enough and the difference is a real race rather than a style preference:
// a handshake response completes into the kernel buffer, so its completion handler may not yet be
// queued when `poll` finds nothing runnable and returns.
template <typename Predicate>
void run_until(boost::asio::io_context& io_context, Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + kCompletionDeadline;
  while (std::chrono::steady_clock::now() < deadline) {
    io_context.restart();
    static_cast<void>(io_context.poll());
    if (predicate()) {
      return;
    }
  }
}

// Drives the server loop until it has no immediately runnable work left. Used only where the test
// asserts that nothing further happens.
void drain(boost::asio::io_context& io_context) {
  io_context.restart();
  static_cast<void>(io_context.poll());
}

[[nodiscard]] std::size_t count_events(const SessionHarness& harness,
                                       const std::string_view event) {
  std::size_t count = 0;
  for (const blob_royale::test_support::CapturedStructuredLogEvent& record :
       harness.log_capture().events()) {
    count += record.event == event ? 1U : 0U;
  }
  return count;
}

} // namespace

TEST_CASE("SessionWebSocketSession reports handshake failure and retires nothing it never opened",
          "[unit][server][v2][session][failure]") {
  constexpr std::string_view kRequestId = "unit.session.handshake-failure";
  SessionHarness harness;
  const std::shared_ptr<server::SessionWebSocketSession> session =
      harness.make_session(kRequestId, direct_identity());

  harness.client_socket().set_option(boost::asio::socket_base::linger{true, 0});
  harness.client_socket().close();
  session->run(harness.request(kRequestId));
  REQUIRE(harness.server_io_context().run_one_for(kCompletionDeadline) == 1);

  const std::vector<blob_royale::test_support::CapturedStructuredLogEvent> events =
      harness.log_capture().events();
  REQUIRE(events.size() == 1);
  CHECK(events[0].severity == "warning");
  CHECK(events[0].event == "session.handshake_failed");
  CHECK_FALSE(harness.log_capture().contains_event("session.opened"));
  CHECK_FALSE(harness.log_capture().contains_event("session.closed"));
  // A handshake that never completed opened no controller, so retirement has nothing to do and
  // the directory never held an entry.
  CHECK(harness.controller_directory().size() == 0);
  CHECK(harness.server_context()->traffic_policy().active_tcp_connection_count() == 0);
  CHECK(harness.server_context()->traffic_policy().active_websocket_count() == 0);
}

TEST_CASE("SessionWebSocketSession opens exactly one controller and retires it exactly once",
          "[unit][server][v2][session][ownership]") {
  constexpr std::string_view kRequestId = "unit.session.close-once";
  SessionHarness harness;
  const std::shared_ptr<server::SessionWebSocketSession> session =
      harness.make_session(kRequestId, direct_identity());
  SessionClient client{std::move(harness.client_socket())};

  // The id this session is about to be issued. A live runtime opens its controller cursor above
  // every id the loaded world already carries, so the first session is not necessarily one.
  const simulation::ControllerId retired_controller_id =
      simulation::ControllerId::create(harness.command_sink().next_controller_id());

  session->run(harness.request(kRequestId));
  run_until(harness.server_io_context(),
            [&harness] { return harness.controller_directory().size() == 1; });

  REQUIRE(harness.controller_directory().size() == 1);
  CHECK(harness.log_capture().contains_event("session.opened"));

  // Leaving retires the controller as soon as leaving is decided, rather than when the socket
  // finally dies, so a disconnected player's blob is not left in the arena while a close frame is
  // in flight.
  harness.server_context()->request_stop();
  run_until(harness.server_io_context(),
            [&harness] { return harness.controller_directory().size() == 0; });
  CHECK(harness.controller_directory().size() == 0);

  // Every terminal path funnels through one idempotent `finish`. Repeating the stop request and
  // then failing the in-flight close both reach it, and neither retires a second time.
  harness.server_context()->request_stop();
  drain(harness.server_io_context());
  client.stream().next_layer().close();
  run_until(harness.server_io_context(),
            [&harness] { return count_events(harness, "session.closed") == 1; });

  CHECK(harness.controller_directory().size() == 0);
  CHECK(count_events(harness, "session.closed") == 1);

  // The `ControllerId` this session held was retired exactly once: the next identity the sink
  // issues is a *different* one, and it is the only entry the directory now holds. The retired id
  // is read from the sink rather than written as a literal, because a live runtime opens its
  // controller cursor above every id the loaded world already carries and the first session is
  // therefore not necessarily one (`simulation_runtime.cpp`, first_session_controller_id).
  const simulation::ControllerId retired = retired_controller_id;
  const simulation::ControllerId later =
      harness.command_sink().open_session("session", "player-later");
  CHECK(later != retired);
  CHECK(harness.controller_directory().size() == 1);
  CHECK(harness.command_sink().close_session(retired) ==
        runtime::ControllerCloseResult::kUnknownControllerId);
  CHECK(harness.command_sink().close_session(later) == runtime::ControllerCloseResult::kClosed);
}

TEST_CASE("SessionWebSocketSession publishes the fallback display name for a direct peer",
          "[unit][server][v2][session][identity]") {
  constexpr std::string_view kRequestId = "unit.session.fallback-name";
  SessionHarness harness;
  const std::shared_ptr<server::SessionWebSocketSession> session =
      harness.make_session(kRequestId, direct_identity());
  SessionClient client{std::move(harness.client_socket())};
  // The id this session will be issued, read before it opens. A literal would assume the sink
  // starts at one, which a live runtime deliberately does not.
  const simulation::ControllerId issued =
      simulation::ControllerId::create(harness.command_sink().next_controller_id());

  session->run(harness.request(kRequestId));
  run_until(harness.server_io_context(),
            [&harness] { return harness.controller_directory().size() == 1; });

  REQUIRE(harness.controller_directory().size() == 1);
  const std::optional<runtime::ControllerPresentation> presentation =
      harness.controller_directory().find(issued);
  REQUIRE(presentation.has_value());
  CHECK(presentation->controller_kind == "session");
  CHECK(presentation->display_name.starts_with("player-"));
  CHECK(harness.log_capture().contains_event("session.display_name_fallback"));
  static_cast<void>(client);
}

TEST_CASE("SessionWebSocketSession publishes an accepted proxy display name byte for byte",
          "[unit][server][v2][session][identity]") {
  constexpr std::string_view kRequestId = "unit.session.proxy-name";
  SessionHarness harness;
  const std::shared_ptr<server::SessionWebSocketSession> session = harness.make_session(
      kRequestId,
      server::PeerIdentity::proxy_forwarded("100.101.102.103", std::string{"Cole Shaffer"},
                                            server::DisplayNameOutcome::kProxySupplied, 12));
  SessionClient client{std::move(harness.client_socket())};
  const simulation::ControllerId issued =
      simulation::ControllerId::create(harness.command_sink().next_controller_id());

  session->run(harness.request(kRequestId));
  run_until(harness.server_io_context(),
            [&harness] { return harness.controller_directory().size() == 1; });

  const std::optional<runtime::ControllerPresentation> presentation =
      harness.controller_directory().find(issued);
  REQUIRE(presentation.has_value());
  CHECK(presentation->display_name == "Cole Shaffer");
  // A published name is never logged, and an accepted one produces no fallback line at all.
  CHECK_FALSE(harness.log_capture().contains_event("session.display_name_fallback"));
  static_cast<void>(client);
}

TEST_CASE("CommandRatePolicy admits a burst of thirty and refills at twenty per second",
          "[unit][server][v2][rate]") {
  const auto opened_at = server::CommandRatePolicy::Clock::time_point{};
  server::CommandRatePolicy policy{opened_at};

  for (std::size_t command = 0;
       command < static_cast<std::size_t>(server::ServerLimits::kSessionCommandBucketCapacity);
       ++command) {
    INFO("command " << command);
    CHECK(policy.consume(opened_at));
  }
  // The thirty-first command in the same instant is the one that closes the connection.
  CHECK_FALSE(policy.consume(opened_at));

  // Twenty tokens per second, never negative, and never above the burst.
  CHECK(policy.consume(opened_at + 50ms));
  CHECK_FALSE(policy.consume(opened_at + 50ms));
  CHECK(policy.consume(opened_at + 100ms));
  for (std::size_t command = 0;
       command < static_cast<std::size_t>(server::ServerLimits::kSessionCommandBucketCapacity);
       ++command) {
    CHECK(policy.consume(opened_at + 10s));
  }
  CHECK_FALSE(policy.consume(opened_at + 10s));
}

TEST_CASE("The command budget and the control budget are separate ledgers",
          "[unit][server][v2][rate]") {
  const auto opened_at = server::CommandRatePolicy::Clock::time_point{};
  server::CommandRatePolicy commands{opened_at};
  server::ControlFrameRatePolicy control{opened_at};

  for (std::size_t command = 0;
       command < static_cast<std::size_t>(server::ServerLimits::kSessionCommandBucketCapacity);
       ++command) {
    CHECK(commands.consume(opened_at));
  }
  CHECK_FALSE(commands.consume(opened_at));
  // Exhausting one ledger must not spend the other: a peer cannot buy control frames with command
  // tokens or the reverse.
  for (std::size_t frame = 0; frame < server::ServerLimits::kControlFrameBurstMaximumCount;
       ++frame) {
    CHECK(control.consume(opened_at));
  }
  CHECK_FALSE(control.consume(opened_at));
}
