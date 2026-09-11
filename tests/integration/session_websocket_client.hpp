#ifndef BLOB_ROYALE_TESTS_INTEGRATION_SESSION_WEBSOCKET_CLIENT_HPP
#define BLOB_ROYALE_TESTS_INTEGRATION_SESSION_WEBSOCKET_CLIENT_HPP

#include <boost/asio/io_context.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/websocket/stream.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace blob_royale::integration_test {

inline constexpr std::string_view kDefaultSessionTarget = "/api/v3/lobbies/1/session";

// One protocol v3 session connection, exposing only the observations these contracts assert. The
// target defaults to `/api/v3/lobbies/1/session` (room 1) and may name a room,
// `/api/v3/lobbies/<lobby_id>/session`; `forwarded_client`, when non-empty, is sent as
// `X-Forwarded-For` so a fixture that trusts the loopback proxy accounts each client separately.
//
// It is a separate client from `SnapshotWebSocketClient` because the two speak different
// protocols: v1's stream is read-only and closes on any client data, and v3's is bidirectional and
// begins with one welcome frame. Sharing one client would mean one class whose behavior depended on
// which subprotocol it happened to offer, which is exactly the route/subprotocol confusion the
// server refuses.
// related: docs/protocol/v3.md -- the contract this exercises.
class SessionWebSocketClient final {
public:
  SessionWebSocketClient(std::uint16_t port, std::string origin, std::string request_id,
                         std::chrono::milliseconds operation_timeout,
                         std::string target = std::string{kDefaultSessionTarget},
                         std::string forwarded_client = {});

  SessionWebSocketClient(const SessionWebSocketClient&) = delete;
  SessionWebSocketClient(SessionWebSocketClient&&) = delete;
  SessionWebSocketClient& operator=(const SessionWebSocketClient&) = delete;
  SessionWebSocketClient& operator=(SessionWebSocketClient&&) = delete;
  ~SessionWebSocketClient() noexcept;

  // Reads the one welcome frame and retains its identities. Throws unless the first frame is a
  // welcome with `message_sequence` exactly 1.
  [[nodiscard]] std::string read_welcome_message();

  [[nodiscard]] std::string read_snapshot_message();

  // Reads snapshots until one carries `tick_sequence` at or beyond `minimum_tick`, then returns it.
  [[nodiscard]] std::string read_snapshot_at_or_after_tick(std::uint64_t minimum_tick);

  void send_command(std::string_view envelope);

  // Sends one frame and returns the close code the server answered with, or nullopt when the
  // server kept the connection open across `frame_budget` further server frames.
  [[nodiscard]] std::optional<std::uint16_t>
  send_command_and_await_close(std::string_view envelope, std::string& close_reason_out);

  void close_normally();

  [[nodiscard]] std::uint64_t entity_id() const noexcept { return entity_id_; }
  [[nodiscard]] std::uint64_t controller_id() const noexcept { return controller_id_; }
  // `welcome.lobby_id` and `welcome.seat_count_maximum`, protocol 2.4's two room facts.
  [[nodiscard]] std::uint64_t lobby_id() const noexcept { return lobby_id_; }
  [[nodiscard]] std::uint64_t seat_count_maximum() const noexcept { return seat_count_maximum_; }
  [[nodiscard]] const std::string& display_name() const& noexcept { return display_name_; }
  [[nodiscard]] const std::string& display_name() const&& = delete;

  [[nodiscard]] const boost::beast::http::response<boost::beast::http::string_body>&
  handshake_response() const& noexcept {
    return handshake_response_;
  }
  [[nodiscard]] const boost::beast::http::response<boost::beast::http::string_body>&
  handshake_response() const&& = delete;

private:
  void configure_native_socket_timeout();
  void read_text_frame();

  std::chrono::milliseconds operation_timeout_;
  boost::asio::io_context io_context_{1};
  boost::beast::websocket::stream<boost::beast::tcp_stream> websocket_;
  boost::beast::flat_buffer read_buffer_;
  boost::beast::http::response<boost::beast::http::string_body> handshake_response_;
  std::uint64_t entity_id_{0};
  std::uint64_t controller_id_{0};
  std::uint64_t lobby_id_{0};
  std::uint64_t seat_count_maximum_{0};
  std::string display_name_;
};

// Offers a complete, well-formed v3 session upgrade to `target` and returns the HTTP response the
// server declined it with. Throws unless the server declined: a `101` here is the contract
// violation, because the caller is asserting a refusal (`409`, `404`, `503`).
[[nodiscard]] boost::beast::http::response<boost::beast::http::string_body>
declined_session_upgrade(std::uint16_t port, std::string origin, std::string request_id,
                         std::chrono::milliseconds operation_timeout, std::string target,
                         std::string forwarded_client = {});

} // namespace blob_royale::integration_test

#endif
