#include "snapshot_websocket_client.hpp"

#include "integration_test_error.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/websocket/error.hpp>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/system/error_code.hpp>

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
#include <utility>

namespace blob_royale::integration_test {
namespace {

namespace websocket = boost::beast::websocket;
using Tcp = boost::asio::ip::tcp;

constexpr std::string_view kSnapshotTarget = "/api/v1/snapshots";
constexpr std::string_view kSnapshotSubprotocol = "blob-royale.snapshot.v1";
constexpr std::size_t kMaximumSnapshotFrameBytes = 2'097'152;
constexpr std::size_t kMaximumFramesBeforePolicyClose = 4;

[[noreturn]] void throw_transport_error(const std::string_view operation,
                                        const boost::system::error_code& error) {
  throw IntegrationTestError{IntegrationTestErrorCode::kTransportFailed, std::string{operation},
                             error.message()};
}

} // namespace

SnapshotWebSocketClient::SnapshotWebSocketClient(const std::uint16_t port, std::string origin,
                                                 std::string request_id,
                                                 const std::chrono::milliseconds operation_timeout)
    : operation_timeout_(operation_timeout), websocket_(io_context_) {
  if (port == 0 || origin.empty() || request_id.empty() ||
      operation_timeout <= std::chrono::milliseconds::zero() ||
      operation_timeout > std::chrono::minutes{1}) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid, "websocket.construct",
                               "connection input is invalid"};
  }

  boost::system::error_code operation_error;
  websocket_.next_layer().socket().open(Tcp::v4(), operation_error);
  if (operation_error) {
    throw_transport_error("websocket.open", operation_error);
  }
  configure_native_socket_timeout();
  websocket_.next_layer().expires_after(operation_timeout_);
  websocket_.next_layer().connect(Tcp::endpoint{boost::asio::ip::address_v4::loopback(), port},
                                  operation_error);
  if (operation_error) {
    throw_transport_error("websocket.connect", operation_error);
  }

  const std::string host_authority = std::string{"127.0.0.1:"}.append(std::to_string(port));
  websocket_.set_option(websocket::stream_base::decorator(
      [origin = std::move(origin),
       request_id = std::move(request_id)](websocket::request_type& request) {
        request.set(boost::beast::http::field::origin, origin);
        request.set(boost::beast::http::field::sec_websocket_protocol, kSnapshotSubprotocol);
        request.set("X-Request-ID", request_id);
        request.set(boost::beast::http::field::user_agent, "blob-royale-native-integration");
      }));
  websocket_.read_message_max(kMaximumSnapshotFrameBytes);
  websocket_.auto_fragment(false);
  websocket_.next_layer().expires_after(operation_timeout_);
  websocket_.handshake(handshake_response_, host_authority, kSnapshotTarget, operation_error);
  if (operation_error) {
    throw_transport_error("websocket.handshake", operation_error);
  }
}

SnapshotWebSocketClient::~SnapshotWebSocketClient() noexcept {
  boost::system::error_code ignored;
  websocket_.next_layer().socket().shutdown(Tcp::socket::shutdown_both, ignored);
  websocket_.next_layer().socket().close(ignored);
}

std::string SnapshotWebSocketClient::read_snapshot_message() {
  read_snapshot_frame();
  return boost::beast::buffers_to_string(read_buffer_.data());
}

void SnapshotWebSocketClient::read_and_discard_snapshot_message() {
  read_snapshot_frame();
  read_buffer_.consume(read_buffer_.size());
}

void SnapshotWebSocketClient::close_with_application_code(const std::uint16_t close_code) {
  if (close_code < 3'000 || close_code > 4'999) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                               "websocket.close_with_application_code",
                               "application close code must be within 3000 through 4999"};
  }
  websocket::close_reason reason;
  reason.code = static_cast<websocket::close_code>(close_code);
  reason.reason = "integration_peer_close";
  boost::system::error_code operation_error;
  websocket_.next_layer().expires_after(operation_timeout_);
  websocket_.close(reason, operation_error);
  if (operation_error) {
    throw_transport_error("websocket.close_with_application_code", operation_error);
  }
}

void SnapshotWebSocketClient::read_snapshot_frame() {
  read_buffer_.consume(read_buffer_.size());
  boost::system::error_code operation_error;
  websocket_.next_layer().expires_after(operation_timeout_);
  websocket_.read(read_buffer_, operation_error);
  if (operation_error) {
    throw_transport_error("websocket.read_snapshot", operation_error);
  }
  if (!websocket_.got_text()) {
    throw IntegrationTestError{IntegrationTestErrorCode::kContractViolation,
                               "websocket.read_snapshot", "server emitted a binary data frame"};
  }
  if (read_buffer_.size() > kMaximumSnapshotFrameBytes) {
    throw IntegrationTestError{IntegrationTestErrorCode::kContractViolation,
                               "websocket.read_snapshot", "server frame exceeded protocol limit"};
  }
}

void SnapshotWebSocketClient::send_forbidden_client_message_and_require_policy_close(
    const std::string_view message) {
  websocket_.text(true);
  boost::system::error_code operation_error;
  websocket_.next_layer().expires_after(operation_timeout_);
  websocket_.write(boost::asio::buffer(message), operation_error);
  if (operation_error) {
    throw_transport_error("websocket.write_forbidden_client_data", operation_error);
  }

  for (std::size_t received_frame_count = 0; received_frame_count < kMaximumFramesBeforePolicyClose;
       ++received_frame_count) {
    read_buffer_.consume(read_buffer_.size());
    websocket_.next_layer().expires_after(operation_timeout_);
    websocket_.read(read_buffer_, operation_error);
    if (operation_error == websocket::error::closed) {
      const websocket::close_reason reason = websocket_.reason();
      if (reason.code != websocket::close_code::policy_error ||
          reason.reason != "client_data_forbidden") {
        throw IntegrationTestError{IntegrationTestErrorCode::kContractViolation,
                                   "websocket.client_frame_policy",
                                   "client data produced the wrong WebSocket close code or reason"};
      }
      return;
    }
    if (operation_error) {
      throw_transport_error("websocket.await_policy_close", operation_error);
    }
  }
  throw IntegrationTestError{IntegrationTestErrorCode::kDeadlineExceeded,
                             "websocket.client_frame_policy",
                             "server continued sending snapshots after forbidden client data"};
}

void SnapshotWebSocketClient::constrain_receive_buffer_for_nonreading_test(
    const std::size_t requested_byte_count) {
  if (requested_byte_count == 0 || requested_byte_count > static_cast<std::size_t>(INT_MAX)) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                               "websocket.constrain_receive_buffer",
                               "requested receive buffer is out of range"};
  }
  const int native_byte_count = static_cast<int>(requested_byte_count);
  if (::setsockopt(websocket_.next_layer().socket().native_handle(), SOL_SOCKET, SO_RCVBUF,
                   &native_byte_count, sizeof(native_byte_count)) != 0) {
    throw IntegrationTestError{IntegrationTestErrorCode::kTransportFailed,
                               "websocket.constrain_receive_buffer",
                               "socket receive buffer setup failed"};
  }
}

void SnapshotWebSocketClient::configure_native_socket_timeout() {
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
                               "websocket.configure_deadline", "socket deadline setup failed"};
  }
}

} // namespace blob_royale::integration_test
