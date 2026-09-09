#include "peer_identity.hpp"
#include "request_id.hpp"
#include "server_execution_context.hpp"
#include "server_test_fixture.hpp"
#include "session_websocket_session.hpp"

#include "command_kind_mask.hpp"
#include "command_mailbox.hpp"
#include "components/controllable_component.hpp"
#include "controller_directory.hpp"
#include "controller_id.hpp"
#include "game_simulation.hpp"
#include "game_world.hpp"
#include "match_session_context.hpp"
#include "seat_roster.hpp"
#include "server_config.hpp"
#include "simulation_runtime.hpp"
#include "world_snapshot.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/write.hpp>
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
#include <thread>
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
        lobbies_(fixture::single_lobby(publication_, match_session_.context())),
        server_context_(std::make_shared<server::ServerExecutionContext>(
            server_io_context_, server_config(acceptor_.local_endpoint().port()), lobbies_,
            log_capture_.logger)),
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
        std::move(server_socket_), server_context_, lobbies_.room(1), std::string{kPeerAddress},
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
  server::LobbyDirectory lobbies_;
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

TEST_CASE("SessionWebSocketSession logs every lobby command it submits, with the sink's answer",
          "[unit][server][v2][session][lobby]") {
  constexpr std::string_view kRequestId = "unit.session.lobby-command-log";
  SessionHarness harness;
  const std::shared_ptr<server::SessionWebSocketSession> session =
      harness.make_session(kRequestId, direct_identity());
  session->run(harness.request(kRequestId));
  run_until(harness.server_io_context(),
            [&harness] { return harness.controller_directory().size() == 1; });
  REQUIRE(harness.controller_directory().size() == 1);

  // One masked text frame, written raw. The server accepted a pre-parsed upgrade and never reads a
  // client handshake from this socket, so the first bytes it reads may be a frame; a client MUST
  // mask (RFC 6455 § 5.1), and an all-zero key is a legal key that leaves the payload readable
  // here. The frame is 34 bytes of payload, well inside the 1,024-byte inbound bound.
  const std::string envelope = R"({"kind":"start_match","payload":{}})";
  std::string frame;
  frame.push_back(static_cast<char>(0x81));
  frame.push_back(static_cast<char>(0x80U | static_cast<unsigned>(envelope.size())));
  frame.append(4, '\0');
  frame.append(envelope);
  boost::asio::write(harness.client_socket(), boost::asio::buffer(frame));

  run_until(harness.server_io_context(),
            [&harness] { return harness.log_capture().contains_event("session.lobby_command"); });
  const std::optional<blob_royale::test_support::CapturedStructuredLogEvent> logged =
      harness.log_capture().find_event("session.lobby_command");
  REQUIRE(logged.has_value());
  CHECK(logged->severity == "info");
  CHECK(logged->request_id == std::string{kRequestId});
  CHECK(logged->connection_id == std::string{kRequestId});
  REQUIRE(logged->detail.has_value());
  CHECK(logged->detail->find("kind=start_match") != std::string::npos);
  CHECK(logged->detail->find("result=accepted") != std::string::npos);
}

namespace {

// The same harness shape as `SessionHarness`, except that the publication the server reads is a
// **live** runtime's and that runtime is ticking, so a spawn the session submits is actually
// applied. Presentation runs at one slot per second, which turns the window between "spawn
// submitted" and "body observed" from ~33 ms into ~1 s so the test can close inside it reliably.
class LiveRuntimeHarness final {
public:
  explicit LiveRuntimeHarness(simulation::GameSimulation game = fixture::game_simulation())
      : simulation_runtime_(std::move(game)),
        acceptor_(server_io_context_, {boost::asio::ip::address_v4::loopback(), 0}),
        lobbies_(
            fixture::single_lobby(simulation_runtime_.snapshot_publication(), match_context())),
        server_context_(std::make_shared<server::ServerExecutionContext>(
            server_io_context_, server_config(acceptor_.local_endpoint().port()), lobbies_,
            log_capture_.logger)),
        client_socket_(client_io_context_), server_socket_(server_io_context_) {
    simulation_runtime_.start();
    client_socket_.connect(acceptor_.local_endpoint());
    acceptor_.accept(server_socket_);
  }

  LiveRuntimeHarness(const LiveRuntimeHarness&) = delete;
  LiveRuntimeHarness(LiveRuntimeHarness&&) = delete;
  LiveRuntimeHarness& operator=(const LiveRuntimeHarness&) = delete;
  LiveRuntimeHarness& operator=(LiveRuntimeHarness&&) = delete;
  ~LiveRuntimeHarness() { simulation_runtime_.stop(); }

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
        std::move(server_socket_), server_context_, lobbies_.room(1), std::string{kPeerAddress},
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
  [[nodiscard]] runtime::SimulationRuntime& simulation_runtime() noexcept {
    return simulation_runtime_;
  }
  [[nodiscard]] const fixture::LogCapture& log_capture() const noexcept { return log_capture_; }
  // The one room this harness serves, as the control loop would read it.
  [[nodiscard]] const server::LobbyEntry& lobby() const { return lobbies_.room(1); }

private:
  [[nodiscard]] server::MatchSessionContext match_context() {
    return server::MatchSessionContext::create(
        1, simulation_runtime_.command_sink(), simulation_runtime_.controller_directory(),
        std::string{fixture::kFixtureMapName}, fixture::kFixtureSeatCountMaximum,
        simulation::CommandKindMask::all(), std::vector<std::string>{"wanderer"});
  }

  [[nodiscard]] static server::ServerConfig server_config(const std::uint16_t port) {
    return server::ServerConfig::create(
        "127.0.0.1", port, 1, fixture::kWorldWidth, fixture::kWorldHeight, fixture::kPlayerRadius,
        {std::string{"127.0.0.1:"}.append(std::to_string(port))}, {}, {});
  }

  boost::asio::io_context server_io_context_{1};
  boost::asio::io_context client_io_context_{1};
  runtime::SimulationRuntime simulation_runtime_;
  fixture::LogCapture log_capture_;
  Tcp::acceptor acceptor_;
  server::LobbyDirectory lobbies_;
  std::shared_ptr<server::ServerExecutionContext> server_context_;
  Tcp::socket client_socket_;
  Tcp::socket server_socket_;
};

template <typename Predicate>
void run_until_within(boost::asio::io_context& io_context,
                      const std::chrono::steady_clock::duration budget, Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + budget;
  while (std::chrono::steady_clock::now() < deadline) {
    io_context.restart();
    static_cast<void>(io_context.poll());
    if (predicate()) {
      return;
    }
    std::this_thread::sleep_for(1ms);
  }
}

[[nodiscard]] std::size_t count_events(const LiveRuntimeHarness& harness,
                                       const std::string_view event) {
  std::size_t count = 0;
  for (const blob_royale::test_support::CapturedStructuredLogEvent& record :
       harness.log_capture().events()) {
    count += record.event == event ? 1U : 0U;
  }
  return count;
}

[[nodiscard]] bool world_holds_controller(const simulation::WorldSnapshot& snapshot,
                                          const simulation::ControllerId controller) {
  for (const auto& entry : snapshot.components<simulation::Controllable>()) {
    if (entry.value.controller_id == controller) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] simulation::SeatRoster latest_seats(LiveRuntimeHarness& harness) {
  const std::shared_ptr<const simulation::WorldSnapshot> latest =
      harness.simulation_runtime().snapshot_publication().latest();
  return latest->match().seats();
}

} // namespace

TEST_CASE("SessionWebSocketSession asks for a seat it does not hold and leaves it on close",
          "[unit][server][v2][session][lobby][join]") {
  // The seat is taken the way the body is: the first presentation slot observes a lobby in which
  // this controller sits nowhere and submits a server-issued join naming no seat, and the tick
  // gives it the lowest empty one. Nothing on the wire chose it.
  constexpr std::string_view kRequestId = "unit.session.seat-request";
  LiveRuntimeHarness harness{fixture::lobby_game_simulation(2)};
  const simulation::ControllerId issued = simulation::ControllerId::create(
      harness.simulation_runtime().command_sink().next_controller_id());
  const std::shared_ptr<server::SessionWebSocketSession> session =
      harness.make_session(kRequestId, direct_identity());
  SessionClient client{std::move(harness.client_socket())};

  session->run(harness.request(kRequestId));
  run_until_within(harness.server_io_context(), 2s, [&harness] {
    return harness.simulation_runtime().controller_directory().size() == 1;
  });
  REQUIRE(harness.simulation_runtime().controller_directory().contains(issued));
  // Counted into its room the moment it holds a controller, which is what the control loop reads
  // to know the room is not abandoned.
  CHECK(harness.lobby().session_count() == 1);

  // One slot, two asks: the spawn for the missing body and the join for the missing seat, and one
  // logged seat request whose answer is the sink's acceptance.
  run_until_within(harness.server_io_context(), 3s, [&harness] {
    return harness.simulation_runtime().command_mailbox_statistics().submitted_command_count >= 2;
  });
  REQUIRE(harness.simulation_runtime().command_mailbox_statistics().submitted_command_count == 2);
  REQUIRE(count_events(harness, "session.seat_requested") == 1);
  run_until_within(harness.server_io_context(), 2s, [&harness, issued] {
    const simulation::SeatRoster seats = latest_seats(harness);
    return seats.seats()[0] == simulation::Seat{simulation::ControllerSeat{issued}};
  });
  const simulation::SeatRoster seated = latest_seats(harness);
  REQUIRE(seated.seats()[0] == simulation::Seat{simulation::ControllerSeat{issued}});
  CHECK(seated.seats()[1] == simulation::Seat{simulation::EmptySeat{}});

  // The next slot observes the seat and asks for no other. (It does ask for a body again: this
  // world has no mode, so the engine's idle policy never seats the spawned entity, and a session
  // whose body is not seated keeps asking for one -- which is why the seat request is counted
  // through its own log line rather than through the mailbox.)
  run_until_within(harness.server_io_context(), 1500ms, [] { return false; });
  CHECK(count_events(harness, "session.seat_requested") == 1);

  // Closing leaves: the seat is empty again without the session having submitted anything.
  client.stream().next_layer().close();
  run_until_within(harness.server_io_context(), 2s,
                   [&harness] { return count_events(harness, "session.closed") == 1; });
  REQUIRE(count_events(harness, "session.closed") == 1);
  run_until_within(harness.server_io_context(), 2s, [&harness] {
    return latest_seats(harness) == simulation::SeatRoster::of_size(2);
  });
  CHECK(latest_seats(harness) == simulation::SeatRoster::of_size(2));
  CHECK_FALSE(harness.simulation_runtime().controller_directory().contains(issued));
  CHECK(count_events(harness, "session.seat_requested") == 1);
  CHECK(harness.lobby().session_count() == 0);
  // Every line the session wrote names its room.
  for (const auto& record : harness.log_capture().events()) {
    if (record.event.starts_with("session.")) {
      CHECK(record.lobby_id == 1);
    }
  }
}

TEST_CASE(
    "SessionWebSocketSession leaves nothing behind when it closes while its spawn is in flight",
    "[unit][server][v2][session][ownership][leave]") {
  // Review finding 1, inverted. The socket closes between the presentation slot that submitted the
  // spawn and the slot that would have observed the body; the leave the sink enqueues at close is
  // what destroys the entity the queued spawn goes on to create.
  constexpr std::string_view kRequestId = "unit.session.spawn-in-flight";
  LiveRuntimeHarness harness;
  const simulation::ControllerId issued = simulation::ControllerId::create(
      harness.simulation_runtime().command_sink().next_controller_id());
  const std::shared_ptr<server::SessionWebSocketSession> session =
      harness.make_session(kRequestId, direct_identity());
  SessionClient client{std::move(harness.client_socket())};

  session->run(harness.request(kRequestId));
  run_until_within(harness.server_io_context(), 2s, [&harness] {
    return harness.simulation_runtime().controller_directory().size() == 1;
  });
  REQUIRE(harness.simulation_runtime().controller_directory().contains(issued));

  // The first presentation slot, one second after the open, observes no body and submits the
  // spawn. Wait for exactly that submission and then take the socket away before the next slot.
  run_until_within(harness.server_io_context(), 3s, [&harness] {
    return harness.simulation_runtime().command_mailbox_statistics().submitted_command_count >= 1;
  });
  REQUIRE(harness.simulation_runtime().command_mailbox_statistics().submitted_command_count == 1);
  client.stream().next_layer().close();
  run_until_within(harness.server_io_context(), 2s,
                   [&harness] { return count_events(harness, "session.closed") == 1; });
  REQUIRE(count_events(harness, "session.closed") == 1);
  REQUIRE_FALSE(harness.simulation_runtime().controller_directory().contains(issued));

  // Let the runtime commit the spawn and the leave, then look at what the world holds.
  std::this_thread::sleep_for(100ms);
  const std::shared_ptr<const simulation::WorldSnapshot> latest =
      harness.simulation_runtime().snapshot_publication().latest();
  const runtime::CommandMailbox::Statistics statistics =
      harness.simulation_runtime().command_mailbox_statistics();

  // Two submissions -- the spawn and the leave -- and no entity for the retired controller.
  CHECK_FALSE(world_holds_controller(*latest, issued));
  CHECK(statistics.submitted_command_count == 2);
  CHECK(statistics.dropped_command_count == 0);
}

TEST_CASE("SessionWebSocketSession closes lobby_full when the roster it observes has no seat its "
          "join could take",
          "[unit][server][v2][session][lobby][join]") {
  // The last-seat race, lost: by the time this session's first presentation slot looks, the one
  // seat belongs to somebody else and the match has not started, so there is no empty seat and no
  // declared bot to displace. The rule is the tick's own (`first_joinable_seat`), the answer is
  // `1013 lobby_full` before any welcome, and the session leaves the room it was counted into.
  constexpr std::string_view kRequestId = "unit.session.lobby-full";
  const simulation::ControllerId occupant = simulation::ControllerId::create(77);
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_match().seats = simulation::SeatRoster::of_size(1);
  world.mutable_match().seats.assign_seat(0,
                                          simulation::Seat{simulation::ControllerSeat{occupant}});
  LiveRuntimeHarness harness{
      simulation::GameSimulation::create(fixture::simulation_config(), std::move(world))};
  const std::shared_ptr<server::SessionWebSocketSession> session =
      harness.make_session(kRequestId, direct_identity());
  SessionClient client{std::move(harness.client_socket())};

  session->run(harness.request(kRequestId));
  run_until_within(harness.server_io_context(), 2s,
                   [&harness] { return count_events(harness, "session.lobby_full") == 1; });
  REQUIRE(count_events(harness, "session.lobby_full") == 1);
  const auto logged = harness.log_capture().find_event("session.lobby_full");
  REQUIRE(logged.has_value());
  CHECK(logged->severity == "warning");
  CHECK(logged->request_id == std::string{kRequestId});
  CHECK(logged->lobby_id == 1);
  REQUIRE(logged->detail.has_value());
  CHECK(*logged->detail == "seat_count=1 phase=lobby");
  // No join was submitted for a roster that could not take one, the room no longer counts this
  // session, and the seat still belongs to its occupant.
  CHECK(count_events(harness, "session.seat_requested") == 0);
  CHECK(harness.lobby().session_count() == 0);
  const simulation::SeatRoster seats = latest_seats(harness);
  CHECK(seats.seats()[0] == simulation::Seat{simulation::ControllerSeat{occupant}});

  // The close frame is on the wire. This passive client answers nothing, so its socket closing is
  // what completes the close here; the recorded code is the one this session requested.
  client.stream().next_layer().close();
  run_until_within(harness.server_io_context(), 2s,
                   [&harness] { return count_events(harness, "session.closed") == 1; });
  const auto closed = harness.log_capture().find_event("session.closed");
  REQUIRE(closed.has_value());
  CHECK(closed->close_code == 1013);
}
