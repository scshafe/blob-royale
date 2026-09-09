#include "http_session.hpp"

#include "game_server_error.hpp"
#include "session_websocket_session.hpp"
#include "snapshot_websocket_session.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace blob_royale::server {
namespace {

namespace http = boost::beast::http;
using Tcp = boost::asio::ip::tcp;

} // namespace

HttpSession::HttpSession(Tcp::socket socket, std::shared_ptr<ServerExecutionContext> server_context,
                         std::string peer_address, TcpAdmissionLease tcp_lease)
    : stream_(std::move(socket)), read_buffer_(ServerLimits::kHeaderSectionMaximumByteCount),
      server_context_(std::move(server_context)), peer_address_(std::move(peer_address)),
      tcp_lease_(std::move(tcp_lease)) {}

HttpSession::~HttpSession() = default;

void HttpSession::run() {
  const std::weak_ptr<HttpSession> weak_self = shared_from_this();
  session_id_ = server_context_->register_session([weak_self](const SessionStopMode mode) noexcept {
    if (const auto self = weak_self.lock()) {
      self->request_stop(mode);
    }
  });
  try {
    boost::asio::dispatch(stream_.get_executor(),
                          [self = shared_from_this()] { self->read_next_request(); });
  } catch (...) {
    server_context_->unregister_session(*session_id_);
    session_id_.reset();
    throw;
  }
}

void HttpSession::read_next_request() {
  if (finished_ || read_active_ || pending_upgrade_request_.has_value() ||
      server_context_->stopping() ||
      response_queue_.size() >= ServerLimits::kPipelinedResponseMaximumCount) {
    return;
  }

  parser_.emplace();
  parser_->header_limit(ServerLimits::kHeaderSectionMaximumByteCount);
  parser_->body_limit(ServerLimits::kRequestBodyMaximumByteCount);
  // Protocol v1 rejects all body framing after the header; skipping prevents any body allocation.
  parser_->skip(true);
  read_active_ = true;
  stream_.expires_after(ServerLimits::kRequestHeaderDeadline);
  preflight_header();
}

void HttpSession::preflight_header() {
  const auto buffered = read_buffer_.data();
  const std::string_view raw_bytes{static_cast<const char*>(buffered.data()), buffered.size()};
  const HttpHeaderPreflightResult preflight = HttpHeaderPreflight::inspect(raw_bytes);
  switch (preflight.status) {
  case HttpHeaderPreflightStatus::kReady:
    parse_preflighted_header();
    return;
  case HttpHeaderPreflightStatus::kObsoleteLineFolding:
  case HttpHeaderPreflightStatus::kHeaderTooLarge:
    read_active_ = false;
    finish();
    return;
  case HttpHeaderPreflightStatus::kNeedsMoreData:
    break;
  }

  const std::size_t remaining_capacity = read_buffer_.max_size() - read_buffer_.size();
  if (remaining_capacity == 0) {
    read_active_ = false;
    finish();
    return;
  }
  const std::size_t read_size = std::min<std::size_t>(remaining_capacity, 4'096);
  stream_.async_read_some(read_buffer_.prepare(read_size),
                          [self = shared_from_this()](const boost::system::error_code& error,
                                                      const std::size_t transferred_byte_count) {
                            self->raw_header_bytes_read(error, transferred_byte_count);
                          });
}

void HttpSession::raw_header_bytes_read(const boost::system::error_code& error,
                                        const std::size_t transferred_byte_count) {
  read_buffer_.commit(transferred_byte_count);
  if (error == http::error::end_of_stream || error == boost::asio::error::operation_aborted ||
      error == boost::beast::error::timeout || error) {
    read_active_ = false;
    finish();
    return;
  }
  preflight_header();
}

void HttpSession::parse_preflighted_header() {
  http::async_read_header(stream_, read_buffer_, *parser_,
                          [self = shared_from_this()](const boost::system::error_code& error,
                                                      const std::size_t transferred_byte_count) {
                            self->header_read(error, transferred_byte_count);
                          });
}

void HttpSession::header_read(const boost::system::error_code& error,
                              const std::size_t transferred_byte_count) {
  static_cast<void>(transferred_byte_count);
  read_active_ = false;
  if (error == http::error::end_of_stream || error == boost::asio::error::operation_aborted ||
      error == boost::beast::error::timeout) {
    finish();
    return;
  }
  if (error) {
    // Malformed/oversized bytes may not provide a safe request-ID or response context.
    finish();
    return;
  }

  try {
    route_request();
  } catch (...) {
    server_context_->fail(std::current_exception());
    finish();
  }
}

void HttpSession::route_request() {
  ++parsed_request_count_;
  GameApiHttpRequest request = parser_->release();
  parser_.reset();
  const bool carries_forbidden_body =
      request.base().find(http::field::transfer_encoding) != request.base().end() ||
      (request.payload_size().has_value() && *request.payload_size() > 0);

  GameApiRouteResult result =
      server_context_->router().route(request, peer_address_, PeerTrafficPolicy::Clock::now());
  switch (result.disposition()) {
  case GameApiRouteDisposition::kCloseWithoutResponse:
    finish();
    return;
  case GameApiRouteDisposition::kWebSocketUpgrade:
    if (response_queue_.empty() && !write_active_) {
      begin_websocket_upgrade(std::move(request), result.request_id(),
                              result.take_websocket_lease(), result.upgrade_route(),
                              result.peer_identity(), result.lobby_id());
    } else {
      pending_upgrade_request_ = std::move(request);
      pending_upgrade_request_id_ = result.request_id();
      pending_upgrade_route_ = result.upgrade_route();
      pending_peer_identity_ = result.peer_identity();
      pending_lobby_id_ = result.lobby_id();
      pending_websocket_lease_ = result.take_websocket_lease();
    }
    return;
  case GameApiRouteDisposition::kHttpResponse:
    break;
  }

  GameApiHttpResponse response = result.take_response();
  if (carries_forbidden_body ||
      parsed_request_count_ >= ServerLimits::kRequestsPerConnectionMaximumCount) {
    response.keep_alive(false);
  }
  const bool read_another = response.keep_alive();
  enqueue_response(std::move(response));
  if (read_another) {
    read_next_request();
  }
}

void HttpSession::enqueue_response(GameApiHttpResponse response) {
  if (response_queue_.size() >= ServerLimits::kPipelinedResponseMaximumCount) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed, "http.response_queue",
                          "response queue exceeded its fixed bound"};
  }
  response_queue_.push_back(std::make_shared<GameApiHttpResponse>(std::move(response)));
  if (!write_active_) {
    write_next_response();
  }
}

void HttpSession::write_next_response() {
  if (finished_ || write_active_ || response_queue_.empty()) {
    return;
  }
  write_active_ = true;
  const auto response = response_queue_.front();
  const bool keep_alive = response->keep_alive();
  http::async_write(
      stream_, *response,
      [self = shared_from_this(), response, keep_alive](const boost::system::error_code& error,
                                                        const std::size_t transferred_byte_count) {
        self->response_written(response, keep_alive, error, transferred_byte_count);
      });
}

void HttpSession::response_written(const std::shared_ptr<GameApiHttpResponse>& response,
                                   const bool keep_alive, const boost::system::error_code& error,
                                   const std::size_t transferred_byte_count) {
  static_cast<void>(transferred_byte_count);
  write_active_ = false;
  std::optional<std::string_view> request_id;
  if (const auto request_id_field = response->base().find("X-Request-ID");
      request_id_field != response->base().end()) {
    request_id =
        std::string_view{request_id_field->value().data(), request_id_field->value().size()};
  }
  server_context_->logger().write(
      {.severity = error ? observability::LogSeverity::kWarning : observability::LogSeverity::kInfo,
       .event = error ? "http.response_failed" : "http.response_completed",
       .request_id = request_id,
       .http_status = static_cast<unsigned int>(response->result_int())});
  if (error) {
    finish();
    return;
  }
  response_queue_.pop_front();
  if (!keep_alive) {
    finish();
    return;
  }
  if (!response_queue_.empty()) {
    write_next_response();
    return;
  }
  if (pending_upgrade_request_.has_value()) {
    GameApiHttpRequest request = std::move(*pending_upgrade_request_);
    protocol::RequestId request_id = std::move(*pending_upgrade_request_id_);
    WebSocketAdmissionLease websocket_lease = std::move(*pending_websocket_lease_);
    const GameApiUpgradeRoute upgrade_route = *pending_upgrade_route_;
    PeerIdentity peer_identity = std::move(*pending_peer_identity_);
    const std::uint64_t lobby_id = *pending_lobby_id_;
    pending_upgrade_request_.reset();
    pending_upgrade_request_id_.reset();
    pending_websocket_lease_.reset();
    pending_upgrade_route_.reset();
    pending_peer_identity_.reset();
    pending_lobby_id_.reset();
    begin_websocket_upgrade(std::move(request), std::move(request_id), std::move(websocket_lease),
                            upgrade_route, std::move(peer_identity), lobby_id);
    return;
  }
  read_next_request();
}

void HttpSession::begin_websocket_upgrade(GameApiHttpRequest request,
                                          protocol::RequestId request_id,
                                          WebSocketAdmissionLease websocket_lease,
                                          const GameApiUpgradeRoute upgrade_route,
                                          PeerIdentity peer_identity,
                                          const std::uint64_t lobby_id) {
  try {
    // Two session classes, chosen by route and never by offered subprotocol: v1's stream accepts no
    // client data and v2's accepts commands, so the route is what decides which semantics run. A
    // v2 session is bound for its life to the room the router admitted it into.
    if (upgrade_route == GameApiUpgradeRoute::kSessionV2) {
      auto session = std::make_shared<SessionWebSocketSession>(
          stream_.release_socket(), server_context_, server_context_->lobbies().room(lobby_id),
          peer_address_, std::move(request_id), std::move(peer_identity),
          std::move(websocket_lease), std::move(tcp_lease_));
      if (session_id_.has_value()) {
        server_context_->unregister_session(*session_id_);
        session_id_.reset();
      }
      finished_ = true;
      session->run(std::move(request));
      return;
    }

    auto websocket_session = std::make_shared<SnapshotWebSocketSession>(
        stream_.release_socket(), server_context_, peer_address_, std::move(request_id),
        std::move(websocket_lease), std::move(tcp_lease_));

    if (session_id_.has_value()) {
      server_context_->unregister_session(*session_id_);
      session_id_.reset();
    }
    finished_ = true;
    websocket_session->run(std::move(request));
  } catch (...) {
    server_context_->fail(std::current_exception());
    finish();
  }
}

void HttpSession::request_stop(const SessionStopMode mode) noexcept {
  if (finished_) {
    return;
  }
  if (mode == SessionStopMode::kImmediate) {
    // Immediate shutdown is the terminal fallback used immediately before the
    // execution context stops dispatching handlers. Release registration and
    // admission ownership here rather than depending on a cancelled handler.
    finish();
    return;
  }
  boost::system::error_code ignored;
  stream_.socket().cancel(ignored);
  stream_.socket().shutdown(Tcp::socket::shutdown_both, ignored);
  stream_.socket().close(ignored);
}

void HttpSession::finish() noexcept {
  if (finished_) {
    return;
  }
  finished_ = true;
  pending_websocket_lease_.reset();
  close_socket();
  tcp_lease_.release();
  if (session_id_.has_value()) {
    server_context_->unregister_session(*session_id_);
    session_id_.reset();
  }
}

void HttpSession::close_socket() noexcept {
  boost::system::error_code ignored;
  stream_.socket().cancel(ignored);
  stream_.socket().shutdown(Tcp::socket::shutdown_both, ignored);
  stream_.socket().close(ignored);
}

} // namespace blob_royale::server
