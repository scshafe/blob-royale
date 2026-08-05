#ifndef BLOB_ROYALE_TESTS_INTEGRATION_SNAPSHOT_WEBSOCKET_CLIENT_HPP
#define BLOB_ROYALE_TESTS_INTEGRATION_SNAPSHOT_WEBSOCKET_CLIENT_HPP

#include <boost/asio/io_context.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/websocket/stream.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace blob_royale::integration_test {

// Owns one protocol-v1 WebSocket and exposes only the integration observations under test.
class SnapshotWebSocketClient final {
public:
  SnapshotWebSocketClient(std::uint16_t port, std::string origin, std::string request_id,
                          std::chrono::milliseconds operation_timeout);

  SnapshotWebSocketClient(const SnapshotWebSocketClient&) = delete;
  SnapshotWebSocketClient(SnapshotWebSocketClient&&) = delete;
  SnapshotWebSocketClient& operator=(const SnapshotWebSocketClient&) = delete;
  SnapshotWebSocketClient& operator=(SnapshotWebSocketClient&&) = delete;
  ~SnapshotWebSocketClient() noexcept;

  [[nodiscard]] std::string read_snapshot_message();
  void read_and_discard_snapshot_message();
  void close_with_application_code(std::uint16_t close_code);
  void send_forbidden_client_message_and_require_policy_close(std::string_view message);

  // Constrains the kernel receive window before a test intentionally stops reading frames.
  void constrain_receive_buffer_for_nonreading_test(std::size_t requested_byte_count);

  [[nodiscard]] const boost::beast::http::response<boost::beast::http::string_body>&
  handshake_response() const& noexcept {
    return handshake_response_;
  }
  [[nodiscard]] const boost::beast::http::response<boost::beast::http::string_body>&
  handshake_response() const&& = delete;

private:
  void configure_native_socket_timeout();
  void read_snapshot_frame();

  std::chrono::milliseconds operation_timeout_;
  boost::asio::io_context io_context_{1};
  boost::beast::websocket::stream<boost::beast::tcp_stream> websocket_;
  boost::beast::flat_buffer read_buffer_;
  boost::beast::http::response<boost::beast::http::string_body> handshake_response_;
};

} // namespace blob_royale::integration_test

#endif
