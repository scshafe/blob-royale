#ifndef BLOB_ROYALE_SERVER_TCP_LISTENER_HPP
#define BLOB_ROYALE_SERVER_TCP_LISTENER_HPP

#include "server_execution_context.hpp"

#include <boost/asio/ip/tcp.hpp>

#include <memory>

namespace blob_royale::server {

// canonical: tcp_listener -- owns the one protocol-v1 TCP acceptor and creates HTTP sessions.
class TcpListener final : public std::enable_shared_from_this<TcpListener> {
public:
  TcpListener(boost::asio::io_context& io_context,
              std::shared_ptr<ServerExecutionContext> server_context);

  TcpListener(const TcpListener&) = delete;
  TcpListener(TcpListener&&) = delete;
  TcpListener& operator=(const TcpListener&) = delete;
  TcpListener& operator=(TcpListener&&) = delete;
  ~TcpListener() = default;

  // Opens, binds, and begins acceptance. Throws GameServerError before traffic on failure.
  void start();

  // Idempotently closes the acceptor. Existing sessions remain owned by the server context.
  void stop() noexcept;

private:
  void accept_next();
  void accepted(const boost::system::error_code& error, boost::asio::ip::tcp::socket socket);

  boost::asio::io_context& io_context_;
  std::shared_ptr<ServerExecutionContext> server_context_;
  boost::asio::ip::tcp::acceptor acceptor_;
  bool started_{false};
};

} // namespace blob_royale::server

#endif
