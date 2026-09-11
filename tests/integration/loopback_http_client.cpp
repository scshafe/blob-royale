#include "loopback_http_client.hpp"

#include "integration_test_error.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
#include <utility>

namespace blob_royale::integration_test {
namespace {

namespace http = boost::beast::http;
using Tcp = boost::asio::ip::tcp;

constexpr std::uint64_t kMaximumHttpResponseBytes = 65'536;
constexpr std::size_t kMaximumConnectionChurnCount = 1'024;
constexpr std::size_t kMaximumPipelineRequestCount = 4;

void configure_native_socket_timeout(Tcp::socket& socket, const std::chrono::milliseconds timeout) {
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
  const auto remaining_microseconds =
      std::chrono::duration_cast<std::chrono::microseconds>(timeout - seconds);
  timeval native_timeout{};
  native_timeout.tv_sec = static_cast<decltype(native_timeout.tv_sec)>(seconds.count());
  native_timeout.tv_usec =
      static_cast<decltype(native_timeout.tv_usec)>(remaining_microseconds.count());
  if (::setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &native_timeout,
                   sizeof(native_timeout)) != 0 ||
      ::setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &native_timeout,
                   sizeof(native_timeout)) != 0) {
    throw IntegrationTestError{IntegrationTestErrorCode::kTransportFailed,
                               "http.configure_deadline", "socket deadline setup failed"};
  }
}

[[noreturn]] void throw_transport_error(const std::string_view operation,
                                        const boost::system::error_code& error) {
  throw IntegrationTestError{IntegrationTestErrorCode::kTransportFailed, std::string{operation},
                             error.message()};
}

[[nodiscard]] bool is_orderly_peer_close(const boost::system::error_code& error) noexcept {
  return error == boost::asio::error::eof || error == boost::asio::error::connection_reset ||
         error == boost::asio::error::connection_aborted;
}

void open_and_connect(boost::beast::tcp_stream& stream, const std::uint16_t port,
                      const std::chrono::milliseconds operation_timeout,
                      const std::string_view operation_prefix) {
  boost::system::error_code operation_error;
  stream.socket().open(Tcp::v4(), operation_error);
  if (operation_error) {
    throw_transport_error(std::string{operation_prefix}.append(".open"), operation_error);
  }
  configure_native_socket_timeout(stream.socket(), operation_timeout);
  stream.expires_after(operation_timeout);
  stream.connect(Tcp::endpoint{boost::asio::ip::address_v4::loopback(), port}, operation_error);
  if (operation_error) {
    throw_transport_error(std::string{operation_prefix}.append(".connect"), operation_error);
  }
}

} // namespace

LoopbackHttpClient::LoopbackHttpClient(const std::uint16_t port,
                                       const std::chrono::milliseconds operation_timeout)
    : port_(port), operation_timeout_(operation_timeout),
      host_authority_(std::string{"127.0.0.1:"}.append(std::to_string(port))) {
  if (port == 0 || operation_timeout <= std::chrono::milliseconds::zero() ||
      operation_timeout > std::chrono::minutes{1}) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid, "http_client.construct",
                               "port or operation timeout is invalid"};
  }
}

IntegrationHttpResponse
LoopbackHttpClient::request(const http::verb method, const std::string_view target,
                            const std::string_view request_id,
                            const std::optional<std::string_view> origin,
                            const std::optional<std::string_view> forwarded_client) const {
  http::request<http::empty_body> request{method, target, 11};
  request.set(http::field::host, host_authority_);
  request.set(http::field::user_agent, "blob-royale-native-integration");
  request.set("X-Request-ID", request_id);
  if (origin.has_value()) {
    request.set(http::field::origin, *origin);
  }
  if (forwarded_client.has_value()) {
    request.set("X-Forwarded-For", *forwarded_client);
  }
  request.keep_alive(false);

  const std::array requests{std::move(request)};
  std::vector<IntegrationHttpResponse> responses = request_pipeline(requests);
  return std::move(responses.front());
}

std::vector<IntegrationHttpResponse>
LoopbackHttpClient::request_pipeline(const std::span<const IntegrationHttpRequest> requests) const {
  if (requests.empty() || requests.size() > kMaximumPipelineRequestCount) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid, "http.pipeline",
                               "pipeline request count is outside 1..4"};
  }
  for (std::size_t index = 0; index + 1 < requests.size(); ++index) {
    if (!requests[index].keep_alive()) {
      throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid, "http.pipeline",
                                 "a request closes the socket before the pipeline ends"};
    }
  }
  boost::asio::io_context io_context{1};
  boost::beast::tcp_stream stream{io_context};
  open_and_connect(stream, port_, operation_timeout_, "http");
  boost::system::error_code operation_error;
  // All requests are sent before any read, exercising the server's pending-response/upgrade
  // boundary without a separate transport implementation or an unbounded raw-byte interface.
  for (const IntegrationHttpRequest& request : requests) {
    stream.expires_after(operation_timeout_);
    http::write(stream, request, operation_error);
    if (operation_error) {
      throw_transport_error("http.write", operation_error);
    }
  }

  boost::beast::flat_buffer read_buffer;
  std::vector<IntegrationHttpResponse> responses;
  responses.reserve(requests.size());
  for (std::size_t index = 0; index < requests.size(); ++index) {
    http::response_parser<http::string_body> parser;
    parser.body_limit(kMaximumHttpResponseBytes);
    stream.expires_after(operation_timeout_);
    http::read(stream, read_buffer, parser, operation_error);
    if (operation_error) {
      throw_transport_error("http.read", operation_error);
    }
    responses.push_back(parser.release());
  }

  boost::system::error_code ignored;
  stream.socket().shutdown(Tcp::socket::shutdown_both, ignored);
  stream.socket().close(ignored);
  return responses;
}

void LoopbackHttpClient::exercise_closed_connection_churn(
    const std::size_t connection_count) const {
  if (connection_count == 0 || connection_count > kMaximumConnectionChurnCount) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid, "http.connection_churn",
                               "connection count is out of range"};
  }

  for (std::size_t connection_index = 0; connection_index < connection_count; ++connection_index) {
    boost::asio::io_context io_context{1};
    boost::beast::tcp_stream stream{io_context};
    open_and_connect(stream, port_, operation_timeout_, "http.connection_churn");

    boost::system::error_code operation_error;
    stream.socket().shutdown(Tcp::socket::shutdown_send, operation_error);
    if (operation_error) {
      throw_transport_error("http.connection_churn.shutdown_send", operation_error);
    }

    std::array<char, 1> unexpected_response{};
    stream.expires_after(operation_timeout_);
    const std::size_t received_byte_count =
        stream.socket().read_some(boost::asio::buffer(unexpected_response), operation_error);
    if (received_byte_count != 0) {
      throw IntegrationTestError{IntegrationTestErrorCode::kContractViolation,
                                 "http.connection_churn.await_close",
                                 "empty connection received an unexpected response"};
    }
    if (!is_orderly_peer_close(operation_error)) {
      throw_transport_error("http.connection_churn.await_close", operation_error);
    }
  }
}

void LoopbackHttpClient::require_obsolete_line_fold_rejection(
    const std::string_view request_id) const {
  if (request_id.empty() || request_id.size() > 64) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid, "http.obs_fold",
                               "request ID is out of range"};
  }

  std::string raw_request{"GET /api/v1/health/ready HTTP/1.1\r\nHost: "};
  raw_request.append(host_authority_)
      .append("\r\nX-Request-ID: ")
      .append(request_id)
      .append("\r\nX-Integration-Probe: accepted-value\r\n rejected-continuation\r\n")
      .append("Connection: close\r\n\r\n");

  boost::asio::io_context io_context{1};
  boost::beast::tcp_stream stream{io_context};
  open_and_connect(stream, port_, operation_timeout_, "http.obs_fold");

  boost::system::error_code operation_error;
  stream.expires_after(operation_timeout_);
  boost::asio::write(stream.socket(), boost::asio::buffer(raw_request), operation_error);
  if (operation_error) {
    throw_transport_error("http.obs_fold.write", operation_error);
  }

  boost::beast::flat_buffer read_buffer;
  http::response_parser<http::string_body> parser;
  parser.body_limit(kMaximumHttpResponseBytes);
  stream.expires_after(operation_timeout_);
  http::read(stream, read_buffer, parser, operation_error);
  if (is_orderly_peer_close(operation_error) || operation_error == http::error::end_of_stream) {
    return;
  }
  if (operation_error) {
    throw_transport_error("http.obs_fold.reject", operation_error);
  }
  const IntegrationHttpResponse response = parser.release();
  if (response.result() != http::status::bad_request || response.keep_alive()) {
    throw IntegrationTestError{IntegrationTestErrorCode::kContractViolation, "http.obs_fold.reject",
                               "obsolete folded header reached successful HTTP routing"};
  }
}

} // namespace blob_royale::integration_test
