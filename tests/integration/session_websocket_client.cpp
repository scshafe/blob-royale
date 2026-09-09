#include "session_websocket_client.hpp"

#include "integration_test_error.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/websocket/error.hpp>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
#include <utility>

namespace blob_royale::integration_test {
namespace {

namespace json = boost::json;
namespace websocket = boost::beast::websocket;
using Tcp = boost::asio::ip::tcp;

constexpr std::string_view kSessionSubprotocol = "blob-royale.session.v2";
constexpr std::size_t kMaximumSessionFrameBytes = 2'097'152;
constexpr std::size_t kMaximumFramesBeforeClose = 8;
constexpr std::size_t kMaximumSnapshotFramesAwaited = 4'096;

[[noreturn]] void throw_transport_error(const std::string_view operation,
                                        const boost::system::error_code& error) {
  throw IntegrationTestError{IntegrationTestErrorCode::kTransportFailed, std::string{operation},
                             error.message()};
}

[[noreturn]] void throw_contract_violation(const std::string_view operation,
                                           const std::string_view detail) {
  throw IntegrationTestError{IntegrationTestErrorCode::kContractViolation, std::string{operation},
                             std::string{detail}};
}

[[nodiscard]] std::uint64_t required_unsigned(const json::object& object,
                                              const std::string_view name,
                                              const std::string_view operation) {
  const json::value* const member = object.if_contains(name);
  if (member == nullptr || !member->is_int64() || member->as_int64() < 0) {
    throw_contract_violation(operation, "frame is missing a required unsigned member");
  }
  return static_cast<std::uint64_t>(member->as_int64());
}

} // namespace

SessionWebSocketClient::SessionWebSocketClient(const std::uint16_t port, std::string origin,
                                               std::string request_id,
                                               const std::chrono::milliseconds operation_timeout,
                                               std::string target, std::string forwarded_client)
    : operation_timeout_(operation_timeout), websocket_(io_context_) {
  if (port == 0 || origin.empty() || request_id.empty() || target.empty() ||
      operation_timeout <= std::chrono::milliseconds::zero() ||
      operation_timeout > std::chrono::minutes{1}) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid, "session.construct",
                               "connection input is invalid"};
  }

  boost::system::error_code operation_error;
  websocket_.next_layer().socket().open(Tcp::v4(), operation_error);
  if (operation_error) {
    throw_transport_error("session.open", operation_error);
  }
  configure_native_socket_timeout();
  websocket_.next_layer().expires_after(operation_timeout_);
  websocket_.next_layer().connect(Tcp::endpoint{boost::asio::ip::address_v4::loopback(), port},
                                  operation_error);
  if (operation_error) {
    throw_transport_error("session.connect", operation_error);
  }

  const std::string host_authority = std::string{"127.0.0.1:"}.append(std::to_string(port));
  websocket_.set_option(websocket::stream_base::decorator(
      [origin = std::move(origin), request_id = std::move(request_id),
       forwarded_client = std::move(forwarded_client)](websocket::request_type& request) {
        request.set(boost::beast::http::field::origin, origin);
        request.set(boost::beast::http::field::sec_websocket_protocol, kSessionSubprotocol);
        request.set("X-Request-ID", request_id);
        request.set(boost::beast::http::field::user_agent, "blob-royale-native-integration");
        if (!forwarded_client.empty()) {
          request.set("X-Forwarded-For", forwarded_client);
        }
      }));
  websocket_.read_message_max(kMaximumSessionFrameBytes);
  websocket_.auto_fragment(false);
  websocket_.next_layer().expires_after(operation_timeout_);
  websocket_.handshake(handshake_response_, host_authority, target, operation_error);
  if (operation_error) {
    throw_transport_error("session.handshake", operation_error);
  }
}

boost::beast::http::response<boost::beast::http::string_body>
declined_session_upgrade(const std::uint16_t port, std::string origin, std::string request_id,
                         const std::chrono::milliseconds operation_timeout, std::string target,
                         std::string forwarded_client) {
  if (port == 0 || origin.empty() || request_id.empty() || target.empty() ||
      operation_timeout <= std::chrono::milliseconds::zero() ||
      operation_timeout > std::chrono::minutes{1}) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                               "session.declined_upgrade", "connection input is invalid"};
  }
  boost::asio::io_context io_context{1};
  websocket::stream<boost::beast::tcp_stream> stream{io_context};
  boost::system::error_code operation_error;
  stream.next_layer().socket().open(Tcp::v4(), operation_error);
  if (operation_error) {
    throw_transport_error("session.declined_upgrade.open", operation_error);
  }
  stream.next_layer().expires_after(operation_timeout);
  stream.next_layer().connect(Tcp::endpoint{boost::asio::ip::address_v4::loopback(), port},
                              operation_error);
  if (operation_error) {
    throw_transport_error("session.declined_upgrade.connect", operation_error);
  }
  const std::string host_authority = std::string{"127.0.0.1:"}.append(std::to_string(port));
  stream.set_option(websocket::stream_base::decorator(
      [origin = std::move(origin), request_id = std::move(request_id),
       forwarded_client = std::move(forwarded_client)](websocket::request_type& request) {
        request.set(boost::beast::http::field::origin, origin);
        request.set(boost::beast::http::field::sec_websocket_protocol, kSessionSubprotocol);
        request.set("X-Request-ID", request_id);
        request.set(boost::beast::http::field::user_agent, "blob-royale-native-integration");
        if (!forwarded_client.empty()) {
          request.set("X-Forwarded-For", forwarded_client);
        }
      }));
  boost::beast::http::response<boost::beast::http::string_body> response;
  stream.next_layer().expires_after(operation_timeout);
  stream.handshake(response, host_authority, target, operation_error);
  boost::system::error_code ignored;
  stream.next_layer().socket().shutdown(Tcp::socket::shutdown_both, ignored);
  stream.next_layer().socket().close(ignored);
  if (operation_error != websocket::error::upgrade_declined) {
    if (operation_error) {
      throw_transport_error("session.declined_upgrade.handshake", operation_error);
    }
    throw_contract_violation("session.declined_upgrade",
                             "the server admitted an upgrade the contract expected it to refuse");
  }
  return response;
}

SessionWebSocketClient::~SessionWebSocketClient() noexcept {
  boost::system::error_code ignored;
  websocket_.next_layer().socket().shutdown(Tcp::socket::shutdown_both, ignored);
  websocket_.next_layer().socket().close(ignored);
}

std::string SessionWebSocketClient::read_welcome_message() {
  read_text_frame();
  std::string frame = boost::beast::buffers_to_string(read_buffer_.data());
  const json::value document = json::parse(frame);
  if (!document.is_object()) {
    throw_contract_violation("session.welcome", "welcome frame is not a JSON object");
  }
  const json::object& envelope = document.as_object();
  const json::value* const meta = envelope.if_contains("meta");
  const json::value* const data = envelope.if_contains("data");
  if (meta == nullptr || !meta->is_object() || data == nullptr || !data->is_object()) {
    throw_contract_violation("session.welcome", "welcome frame is missing data or meta");
  }
  // A client that receives a snapshot as its first frame has lost the welcome and must close
  // rather than proceed with an unknown controller_id.
  if (required_unsigned(meta->as_object(), "message_sequence", "session.welcome") != 1) {
    throw_contract_violation("session.welcome", "the first frame was not message sequence 1");
  }
  entity_id_ = required_unsigned(data->as_object(), "entity_id", "session.welcome");
  controller_id_ = required_unsigned(data->as_object(), "controller_id", "session.welcome");
  lobby_id_ = required_unsigned(data->as_object(), "lobby_id", "session.welcome");
  seat_count_maximum_ =
      required_unsigned(data->as_object(), "seat_count_maximum", "session.welcome");
  const json::value* const display_name = data->as_object().if_contains("display_name");
  if (display_name == nullptr || !display_name->is_string()) {
    throw_contract_violation("session.welcome", "welcome frame is missing a display name");
  }
  display_name_ = std::string{display_name->as_string()};
  return frame;
}

std::string SessionWebSocketClient::read_snapshot_message() {
  read_text_frame();
  return boost::beast::buffers_to_string(read_buffer_.data());
}

std::string
SessionWebSocketClient::read_snapshot_at_or_after_tick(const std::uint64_t minimum_tick) {
  for (std::size_t frame_count = 0; frame_count < kMaximumSnapshotFramesAwaited; ++frame_count) {
    std::string frame = read_snapshot_message();
    const json::value document = json::parse(frame);
    const json::value* const data = document.as_object().if_contains("data");
    if (data == nullptr || !data->is_object()) {
      throw_contract_violation("session.snapshot", "snapshot frame is missing data");
    }
    if (required_unsigned(data->as_object(), "tick_sequence", "session.snapshot") >= minimum_tick) {
      return frame;
    }
  }
  throw IntegrationTestError{IntegrationTestErrorCode::kDeadlineExceeded, "session.snapshot",
                             "the awaited committed tick never arrived"};
}

void SessionWebSocketClient::send_command(const std::string_view envelope) {
  websocket_.text(true);
  boost::system::error_code operation_error;
  websocket_.next_layer().expires_after(operation_timeout_);
  websocket_.write(boost::asio::buffer(envelope), operation_error);
  if (operation_error) {
    throw_transport_error("session.send_command", operation_error);
  }
}

std::optional<std::uint16_t>
SessionWebSocketClient::send_command_and_await_close(const std::string_view envelope,
                                                     std::string& close_reason_out) {
  send_command(envelope);
  boost::system::error_code operation_error;
  for (std::size_t frame_count = 0; frame_count < kMaximumFramesBeforeClose; ++frame_count) {
    read_buffer_.consume(read_buffer_.size());
    websocket_.next_layer().expires_after(operation_timeout_);
    websocket_.read(read_buffer_, operation_error);
    if (operation_error == websocket::error::closed) {
      const websocket::close_reason& reason = websocket_.reason();
      close_reason_out = std::string{reason.reason.data(), reason.reason.size()};
      return static_cast<std::uint16_t>(reason.code);
    }
    if (operation_error) {
      throw_transport_error("session.await_close", operation_error);
    }
  }
  return std::nullopt;
}

void SessionWebSocketClient::close_normally() {
  websocket::close_reason reason;
  reason.code = websocket::close_code::normal;
  reason.reason = "integration_peer_close";
  boost::system::error_code ignored;
  websocket_.next_layer().expires_after(operation_timeout_);
  websocket_.close(reason, ignored);
  // A client-initiated close is an ordinary leave; the server never parses the reason and a
  // transport race on the response is not a contract failure.
}

void SessionWebSocketClient::read_text_frame() {
  read_buffer_.consume(read_buffer_.size());
  boost::system::error_code operation_error;
  websocket_.next_layer().expires_after(operation_timeout_);
  websocket_.read(read_buffer_, operation_error);
  if (operation_error) {
    throw_transport_error("session.read", operation_error);
  }
  if (!websocket_.got_text()) {
    throw_contract_violation("session.read", "server emitted a binary data frame");
  }
  if (read_buffer_.size() > kMaximumSessionFrameBytes) {
    throw_contract_violation("session.read", "server frame exceeded the protocol limit");
  }
}

void SessionWebSocketClient::configure_native_socket_timeout() {
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(operation_timeout_);
  const auto remaining_microseconds =
      std::chrono::duration_cast<std::chrono::microseconds>(operation_timeout_ - seconds);
  timeval native_timeout{};
  native_timeout.tv_sec = static_cast<decltype(native_timeout.tv_sec)>(seconds.count());
  native_timeout.tv_usec =
      static_cast<decltype(native_timeout.tv_usec)>(remaining_microseconds.count());
  if (::setsockopt(websocket_.next_layer().socket().native_handle(), SOL_SOCKET, SO_RCVTIMEO,
                   &native_timeout, sizeof(native_timeout)) != 0 ||
      ::setsockopt(websocket_.next_layer().socket().native_handle(), SOL_SOCKET, SO_SNDTIMEO,
                   &native_timeout, sizeof(native_timeout)) != 0) {
    throw IntegrationTestError{IntegrationTestErrorCode::kTransportFailed,
                               "session.configure_deadline", "socket deadline setup failed"};
  }
}

} // namespace blob_royale::integration_test
