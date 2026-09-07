#ifndef BLOB_ROYALE_SERVER_HTTP_SESSION_HPP
#define BLOB_ROYALE_SERVER_HTTP_SESSION_HPP

#include "game_api_router.hpp"
#include "http_header_preflight.hpp"
#include "server_execution_context.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/parser.hpp>

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <string>

namespace blob_royale::server {

// canonical: http_session -- owns one bounded HTTP/1.1 connection until close or WS handoff.
class HttpSession final : public std::enable_shared_from_this<HttpSession> {
public:
  HttpSession(boost::asio::ip::tcp::socket socket,
              std::shared_ptr<ServerExecutionContext> server_context, std::string peer_address,
              TcpAdmissionLease tcp_lease);

  HttpSession(const HttpSession&) = delete;
  HttpSession(HttpSession&&) = delete;
  HttpSession& operator=(const HttpSession&) = delete;
  HttpSession& operator=(HttpSession&&) = delete;
  ~HttpSession();

  // Registers ownership and starts bounded header parsing on the socket strand.
  void run();

private:
  void read_next_request();
  void preflight_header();
  void raw_header_bytes_read(const boost::system::error_code& error,
                             std::size_t transferred_byte_count);
  void parse_preflighted_header();
  void header_read(const boost::system::error_code& error, std::size_t transferred_byte_count);
  void route_request();
  void enqueue_response(GameApiHttpResponse response);
  void write_next_response();
  void response_written(const std::shared_ptr<GameApiHttpResponse>& response, bool keep_alive,
                        const boost::system::error_code& error, std::size_t transferred_byte_count);
  // Constructs the session class the admitted route selected. The route is the router's decision
  // and is carried here rather than re-derived, so the target, the selected subprotocol, and the
  // semantics the connection then runs under cannot disagree.
  void begin_websocket_upgrade(GameApiHttpRequest request, protocol::RequestId request_id,
                               WebSocketAdmissionLease websocket_lease,
                               GameApiUpgradeRoute upgrade_route, PeerIdentity peer_identity);
  void request_stop(SessionStopMode mode) noexcept;
  void finish() noexcept;
  void close_socket() noexcept;

  boost::beast::tcp_stream stream_;
  boost::beast::flat_buffer read_buffer_;
  std::shared_ptr<ServerExecutionContext> server_context_;
  std::string peer_address_;
  TcpAdmissionLease tcp_lease_;
  std::optional<ServerExecutionContext::SessionId> session_id_;

  std::optional<boost::beast::http::request_parser<boost::beast::http::string_body>> parser_;
  std::deque<std::shared_ptr<GameApiHttpResponse>> response_queue_;
  std::optional<GameApiHttpRequest> pending_upgrade_request_;
  std::optional<protocol::RequestId> pending_upgrade_request_id_;
  std::optional<WebSocketAdmissionLease> pending_websocket_lease_;
  std::optional<GameApiUpgradeRoute> pending_upgrade_route_;
  std::optional<PeerIdentity> pending_peer_identity_;
  std::size_t parsed_request_count_{0};
  bool read_active_{false};
  bool write_active_{false};
  bool finished_{false};
};

} // namespace blob_royale::server

#endif
