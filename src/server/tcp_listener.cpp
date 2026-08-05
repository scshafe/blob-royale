#include "tcp_listener.hpp"

#include "game_server_error.hpp"
#include "http_session.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/error.hpp>

#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace blob_royale::server {
namespace {

using Tcp = boost::asio::ip::tcp;

[[nodiscard]] GameServerError listener_error(const GameServerErrorCode error_code,
                                             const std::string_view operation,
                                             const boost::system::error_code& error) {
  return GameServerError{error_code, std::string{"listener."}.append(operation), error.message()};
}

void close_socket(Tcp::socket& socket) noexcept {
  boost::system::error_code ignored;
  socket.shutdown(Tcp::socket::shutdown_both, ignored);
  socket.close(ignored);
}

} // namespace

TcpListener::TcpListener(boost::asio::io_context& io_context,
                         std::shared_ptr<ServerExecutionContext> server_context)
    : io_context_(io_context), server_context_(std::move(server_context)),
      acceptor_(boost::asio::make_strand(io_context_)) {}

void TcpListener::start() {
  if (started_) {
    throw GameServerError{GameServerErrorCode::kLifecycleInvalid, "listener.start",
                          "listener may be started exactly once"};
  }

  boost::system::error_code parse_error;
  const auto address =
      boost::asio::ip::make_address(server_context_->config().bind_address(), parse_error);
  if (parse_error) {
    throw listener_error(GameServerErrorCode::kListenerOpenFailed, "address", parse_error);
  }
  const Tcp::endpoint endpoint{address, server_context_->config().port()};

  boost::system::error_code operation_error;
  acceptor_.open(endpoint.protocol(), operation_error);
  if (operation_error) {
    throw listener_error(GameServerErrorCode::kListenerOpenFailed, "open", operation_error);
  }
  acceptor_.set_option(boost::asio::socket_base::reuse_address{true}, operation_error);
  if (operation_error) {
    throw listener_error(GameServerErrorCode::kListenerOpenFailed, "set_option", operation_error);
  }
  acceptor_.bind(endpoint, operation_error);
  if (operation_error) {
    throw listener_error(GameServerErrorCode::kListenerBindFailed, "bind", operation_error);
  }
  acceptor_.listen(static_cast<int>(ServerLimits::kConcurrentTcpConnectionMaximumCount),
                   operation_error);
  if (operation_error) {
    throw listener_error(GameServerErrorCode::kListenerListenFailed, "listen", operation_error);
  }

  started_ = true;
  accept_next();
}

void TcpListener::stop() noexcept {
  if (!started_) {
    return;
  }
  started_ = false;
  boost::system::error_code ignored;
  acceptor_.cancel(ignored);
  acceptor_.close(ignored);
}

void TcpListener::accept_next() {
  if (!started_ || server_context_->stopping()) {
    return;
  }
  acceptor_.async_accept(
      boost::asio::make_strand(io_context_),
      [self = shared_from_this()](const boost::system::error_code& error, Tcp::socket socket) {
        self->accepted(error, std::move(socket));
      });
}

void TcpListener::accepted(const boost::system::error_code& error, Tcp::socket socket) {
  if (error) {
    if (error == boost::asio::error::operation_aborted || server_context_->stopping()) {
      return;
    }
    if (error == boost::asio::error::connection_aborted) {
      accept_next();
      return;
    }
    server_context_->fail(std::make_exception_ptr(
        listener_error(GameServerErrorCode::kListenerAcceptFailed, "accept", error)));
    return;
  }

  boost::system::error_code endpoint_error;
  const auto remote_endpoint = socket.remote_endpoint(endpoint_error);
  if (endpoint_error) {
    close_socket(socket);
    accept_next();
    return;
  }
  const std::string peer_address = remote_endpoint.address().to_string();
  if (!remote_endpoint.address().is_loopback() &&
      !server_context_->config().trusts_proxy_address(peer_address)) {
    close_socket(socket);
    accept_next();
    return;
  }

  TcpReservationResult reservation = server_context_->traffic_policy().reserve_tcp_connection(
      peer_address, PeerTrafficPolicy::Clock::now());
  if (!reservation.admission.allowed) {
    close_socket(socket);
    accept_next();
    return;
  }

  try {
    std::make_shared<HttpSession>(std::move(socket), server_context_, peer_address,
                                  std::move(*reservation.lease))
        ->run();
  } catch (...) {
    close_socket(socket);
    server_context_->fail(std::current_exception());
    return;
  }
  accept_next();
}

} // namespace blob_royale::server
