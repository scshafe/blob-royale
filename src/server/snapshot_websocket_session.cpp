#include "snapshot_websocket_session.hpp"

#include "game_server_error.hpp"
#include "protocol_encoding_error.hpp"
#include "protocol_json_encoding.hpp"
#include "server_limits.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/websocket/error.hpp>
#include <boost/beast/websocket/option.hpp>
#include <boost/beast/websocket/rfc6455.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace blob_royale::server {
namespace {

namespace websocket = boost::beast::websocket;
using Tcp = boost::asio::ip::tcp;

void cancel_timer_noexcept(boost::asio::steady_timer& timer) noexcept {
  try {
    static_cast<void>(timer.cancel());
  } catch (...) {
    // Session teardown is terminal; cancellation handlers are best effort.
  }
}

inline constexpr std::string_view kSnapshotSubprotocol = "blob-royale.snapshot.v1";

} // namespace

SnapshotWebSocketSession::SnapshotWebSocketSession(
    Tcp::socket socket, std::shared_ptr<ServerExecutionContext> server_context,
    std::string peer_address, protocol::RequestId request_id,
    WebSocketAdmissionLease websocket_lease, TcpAdmissionLease tcp_lease)
    : websocket_(std::move(socket)),
      read_buffer_(ServerLimits::kInboundWebSocketMessageMaximumByteCount),
      server_context_(std::move(server_context)), peer_address_(std::move(peer_address)),
      request_id_(std::move(request_id)), websocket_lease_(std::move(websocket_lease)),
      tcp_lease_(std::move(tcp_lease)), presentation_timer_(websocket_.get_executor()),
      write_deadline_timer_(websocket_.get_executor()), idle_timer_(websocket_.get_executor()),
      pong_timer_(websocket_.get_executor()), control_frame_rate_policy_(Clock::now()),
      next_presentation_deadline_(Clock::now()), last_received_frame_at_(Clock::now()) {}

SnapshotWebSocketSession::~SnapshotWebSocketSession() = default;

void SnapshotWebSocketSession::run(GameApiHttpRequest request) {
  const std::weak_ptr<SnapshotWebSocketSession> weak_self = shared_from_this();
  session_id_ = server_context_->register_session([weak_self](const SessionStopMode mode) noexcept {
    if (const auto self = weak_self.lock()) {
      self->request_stop(mode);
    }
  });
  try {
    websocket_.read_message_max(ServerLimits::kInboundWebSocketMessageMaximumByteCount);
    websocket_.auto_fragment(false);
    websocket_.text(true);
    websocket_.set_option(websocket::stream_base::timeout{ServerLimits::kRequestHeaderDeadline,
                                                          websocket::stream_base::none(), false});
    websocket_.set_option(websocket::stream_base::decorator(
        [request_id = request_id_.value()](websocket::response_type& response) {
          response.set(boost::beast::http::field::server, "blob-royale");
          response.set(boost::beast::http::field::sec_websocket_protocol, kSnapshotSubprotocol);
          response.set("X-Request-ID", request_id);
        }));
    websocket_.control_callback([weak_self](const websocket::frame_type frame_type,
                                            const boost::beast::string_view payload) {
      if (const auto self = weak_self.lock()) {
        self->observe_control_frame(frame_type, payload);
      }
    });

    boost::beast::get_lowest_layer(websocket_).expires_after(ServerLimits::kRequestHeaderDeadline);
    websocket_.async_accept(request,
                            [self = shared_from_this()](const boost::system::error_code& error) {
                              self->accepted(error);
                            });
  } catch (...) {
    server_context_->unregister_session(*session_id_);
    session_id_.reset();
    throw;
  }
}

void SnapshotWebSocketSession::accepted(const boost::system::error_code& error) {
  if (error) {
    const std::string error_detail = error.message();
    server_context_->logger().write({.severity = observability::LogSeverity::kWarning,
                                     .event = "websocket.handshake_failed",
                                     .request_id = request_id_.value(),
                                     .connection_id = request_id_.value(),
                                     .context = "websocket.accept",
                                     .detail = error_detail});
    finish();
    return;
  }
  handshake_completed_ = true;
  boost::beast::get_lowest_layer(websocket_).expires_never();
  const Clock::time_point opened_at = Clock::now();
  last_received_frame_at_ = opened_at;
  initialize_presentation_cadence(opened_at);
  server_context_->logger().write({.severity = observability::LogSeverity::kInfo,
                                   .event = "websocket.opened",
                                   .request_id = request_id_.value(),
                                   .connection_id = request_id_.value()});
  read_client_frame();
  schedule_idle_check();
  schedule_presentation_slot();
}

void SnapshotWebSocketSession::read_client_frame() {
  if (finished_ || close_requested_) {
    return;
  }
  websocket_.async_read(read_buffer_,
                        [self = shared_from_this()](const boost::system::error_code& error,
                                                    const std::size_t transferred_byte_count) {
                          self->client_frame_read(error, transferred_byte_count);
                        });
}

void SnapshotWebSocketSession::client_frame_read(const boost::system::error_code& error,
                                                 const std::size_t transferred_byte_count) {
  static_cast<void>(transferred_byte_count);
  if (error == websocket::error::closed) {
    const websocket::close_reason& peer_reason = websocket_.reason();
    if (peer_reason.code != websocket::close_code::none) {
      observed_peer_close_code_ = static_cast<std::uint16_t>(peer_reason.code);
    }
    finish();
    return;
  }
  if (error == websocket::error::message_too_big) {
    request_close(CloseIntent::kClientMessageTooLarge);
    return;
  }
  if (error == websocket::error::bad_frame_payload) {
    request_close(CloseIntent::kInvalidUtf8);
    return;
  }
  if (error) {
    request_close(CloseIntent::kProtocolError);
    return;
  }

  last_received_frame_at_ = Clock::now();
  schedule_idle_check();
  read_buffer_.consume(read_buffer_.size());
  request_close(CloseIntent::kClientDataForbidden);
}

void SnapshotWebSocketSession::observe_control_frame(const websocket::frame_type frame_type,
                                                     const boost::beast::string_view payload) {
  static_cast<void>(payload);
  if (finished_) {
    return;
  }
  const Clock::time_point now = Clock::now();
  last_received_frame_at_ = now;
  if (frame_type == websocket::frame_type::pong && waiting_for_pong_) {
    waiting_for_pong_ = false;
    cancel_timer_noexcept(pong_timer_);
  }
  if (!control_frame_rate_policy_.consume(now)) {
    boost::asio::post(websocket_.get_executor(), [self = shared_from_this()] {
      self->request_close(CloseIntent::kControlRateExceeded);
    });
  }
  schedule_idle_check();
}

void SnapshotWebSocketSession::initialize_presentation_cadence(
    const Clock::time_point opened_at) noexcept {
  constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000ULL;
  const std::uint64_t rate = server_context_->config().snapshots_per_second();
  presentation_base_period_ = std::chrono::nanoseconds{kNanosecondsPerSecond / rate};
  presentation_period_remainder_ = kNanosecondsPerSecond % rate;
  presentation_remainder_carry_ = 0;
  next_presentation_deadline_ = opened_at;
  advance_presentation_deadline();
}

void SnapshotWebSocketSession::advance_presentation_deadline() noexcept {
  next_presentation_deadline_ += presentation_base_period_;
  presentation_remainder_carry_ += presentation_period_remainder_;
  const std::uint64_t rate = server_context_->config().snapshots_per_second();
  if (presentation_remainder_carry_ >= rate) {
    next_presentation_deadline_ += std::chrono::nanoseconds{1};
    presentation_remainder_carry_ -= rate;
  }
}

void SnapshotWebSocketSession::schedule_presentation_slot() {
  if (finished_ || close_requested_) {
    return;
  }
  presentation_timer_.expires_at(next_presentation_deadline_);
  presentation_timer_.async_wait(
      [self = shared_from_this()](const boost::system::error_code& error) {
        self->presentation_slot(error);
      });
}

void SnapshotWebSocketSession::presentation_slot(const boost::system::error_code& error) {
  if (error == boost::asio::error::operation_aborted || finished_ || close_requested_) {
    return;
  }
  if (error) {
    finish();
    return;
  }

  const Clock::time_point now = Clock::now();
  do {
    advance_presentation_deadline();
  } while (next_presentation_deadline_ <= now);

  if (!server_context_->publication().is_ready()) {
    request_close(CloseIntent::kServiceNotReady);
    return;
  }
  try {
    if (!control_write_active_) {
      delivery_state_.observe(server_context_->publication().latest());
      if (delivery_state_.ready_to_write()) {
        std::optional<SnapshotEgressLease> egress_lease =
            server_context_->snapshot_egress_budget().try_reserve(*session_id_, now);
        if (egress_lease.has_value()) {
          const std::optional<SnapshotDelivery> delivery = delivery_state_.begin_active_write();
          if (!delivery.has_value()) {
            throw GameServerError{GameServerErrorCode::kSessionInvariantFailed,
                                  "snapshot_delivery.admission",
                                  "admitted egress work has no pending snapshot"};
          }
          start_snapshot_write(*delivery, std::move(*egress_lease));
        }
      }
    }
  } catch (...) {
    report_snapshot_failure(std::current_exception());
    return;
  }
  schedule_presentation_slot();
}

void SnapshotWebSocketSession::start_snapshot_write(SnapshotDelivery delivery,
                                                    SnapshotEgressLease egress_lease) {
  active_egress_lease_.emplace(std::move(egress_lease));
  try {
    active_write_payload_ = protocol::encode_snapshot_message(
        *delivery.snapshot, request_id_, delivery.message_sequence, current_utc_timestamp(),
        active_egress_lease_->owned_byte_count());
  } catch (const protocol::ProtocolEncodingError& error) {
    throw GameServerError{GameServerErrorCode::kSnapshotEncodingFailed, error.context(),
                          error.detail()};
  }
  active_egress_lease_->commit_encoded_bytes(active_write_payload_.size());
  websocket_.text(true);
  websocket_.auto_fragment(false);
  write_deadline_timer_.expires_after(ServerLimits::kWebSocketWriteDeadline);
  write_deadline_timer_.async_wait(
      [self = shared_from_this()](const boost::system::error_code& error) {
        self->snapshot_write_timed_out(error);
      });
  snapshot_write_operation_active_ = true;
  try {
    websocket_.async_write(boost::asio::buffer(active_write_payload_),
                           [self = shared_from_this()](const boost::system::error_code& error,
                                                       const std::size_t transferred_byte_count) {
                             self->snapshot_written(error, transferred_byte_count);
                           });
  } catch (...) {
    snapshot_write_operation_active_ = false;
    throw;
  }
}

void SnapshotWebSocketSession::snapshot_written(const boost::system::error_code& error,
                                                const std::size_t transferred_byte_count) {
  static_cast<void>(transferred_byte_count);
  snapshot_write_operation_active_ = false;
  cancel_timer_noexcept(write_deadline_timer_);
  release_active_payload();
  if (finished_) {
    return;
  }
  if (error) {
    finish();
    return;
  }
  try {
    delivery_state_.complete_active_write();
  } catch (...) {
    report_snapshot_failure(std::current_exception());
    return;
  }
  if (close_requested_) {
    begin_close();
  }
}

void SnapshotWebSocketSession::snapshot_write_timed_out(const boost::system::error_code& error) {
  if (error == boost::asio::error::operation_aborted || finished_) {
    return;
  }
  requested_close_intent_ = CloseIntent::kSlowConsumer;
  close_socket();
  finish();
}

void SnapshotWebSocketSession::schedule_idle_check() {
  if (finished_ || close_requested_ || idle_wait_active_) {
    return;
  }
  idle_wait_active_ = true;
  idle_timer_.expires_at(last_received_frame_at_ + ServerLimits::kWebSocketIdlePingInterval);
  idle_timer_.async_wait([self = shared_from_this()](const boost::system::error_code& error) {
    self->idle_check(error);
  });
}

void SnapshotWebSocketSession::idle_check(const boost::system::error_code& error) {
  idle_wait_active_ = false;
  if (error == boost::asio::error::operation_aborted || finished_ || close_requested_) {
    return;
  }
  if (error) {
    finish();
    return;
  }
  const Clock::time_point required_idle_deadline =
      last_received_frame_at_ + ServerLimits::kWebSocketIdlePingInterval;
  if (Clock::now() < required_idle_deadline) {
    idle_wait_active_ = true;
    idle_timer_.expires_at(required_idle_deadline);
    idle_timer_.async_wait(
        [self = shared_from_this()](const boost::system::error_code& next_error) {
          self->idle_check(next_error);
        });
    return;
  }
  if (delivery_state_.write_active() || control_write_active_) {
    idle_wait_active_ = true;
    idle_timer_.expires_after(std::chrono::milliseconds{100});
    idle_timer_.async_wait(
        [self = shared_from_this()](const boost::system::error_code& next_error) {
          self->idle_check(next_error);
        });
    return;
  }

  server_context_->snapshot_egress_budget().cancel_waiter(*session_id_);
  control_write_active_ = true;
  websocket_.async_ping(websocket::ping_data{},
                        [self = shared_from_this()](const boost::system::error_code& ping_error) {
                          self->ping_written(ping_error);
                        });
}

void SnapshotWebSocketSession::ping_written(const boost::system::error_code& error) {
  control_write_active_ = false;
  if (finished_) {
    return;
  }
  if (error) {
    finish();
    return;
  }
  if (close_requested_) {
    begin_close();
    return;
  }
  waiting_for_pong_ = true;
  pong_timer_.expires_after(ServerLimits::kWebSocketPongDeadline);
  pong_timer_.async_wait([self = shared_from_this()](const boost::system::error_code& pong_error) {
    self->pong_timed_out(pong_error);
  });
}

void SnapshotWebSocketSession::pong_timed_out(const boost::system::error_code& error) {
  if (error == boost::asio::error::operation_aborted || finished_ || !waiting_for_pong_) {
    return;
  }
  request_close(CloseIntent::kPeerTimeout);
}

void SnapshotWebSocketSession::request_close(const CloseIntent close_intent) noexcept {
  if (finished_ || close_requested_) {
    return;
  }
  close_requested_ = true;
  if (session_id_.has_value()) {
    server_context_->snapshot_egress_budget().cancel_waiter(*session_id_);
  }
  requested_close_intent_ = close_intent;
  delivery_state_.discard_pending();
  cancel_timer_noexcept(presentation_timer_);
  cancel_timer_noexcept(idle_timer_);
  cancel_timer_noexcept(pong_timer_);
  if (!delivery_state_.write_active() && !control_write_active_) {
    try {
      begin_close();
    } catch (...) {
      server_context_->fail(std::current_exception());
      finish();
    }
  }
}

void SnapshotWebSocketSession::begin_close() {
  if (finished_ || close_started_ || delivery_state_.write_active() || control_write_active_) {
    return;
  }
  if (!requested_close_intent_.has_value()) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed, "websocket.close",
                          "close began without a requested close intent"};
  }
  close_started_ = true;
  websocket_.async_close(
      close_reason_for(*requested_close_intent_),
      [self = shared_from_this()](const boost::system::error_code& error) { self->closed(error); });
}

void SnapshotWebSocketSession::closed(const boost::system::error_code& error) {
  static_cast<void>(error);
  finish();
}

void SnapshotWebSocketSession::request_stop(const SessionStopMode mode) noexcept {
  if (mode == SessionStopMode::kImmediate) {
    close_socket();
    finish();
    return;
  }
  request_close(CloseIntent::kServerShutdown);
}

void SnapshotWebSocketSession::release_active_payload() noexcept {
  std::string{}.swap(active_write_payload_);
  active_egress_lease_.reset();
}

void SnapshotWebSocketSession::finish() noexcept {
  if (finished_) {
    return;
  }
  finished_ = true;
  if (handshake_completed_) {
    std::optional<std::uint16_t> close_code = observed_peer_close_code_;
    if (!close_code.has_value() && requested_close_intent_.has_value()) {
      close_code = close_code_for(*requested_close_intent_);
    }
    server_context_->logger().write({.severity = observability::LogSeverity::kInfo,
                                     .event = "websocket.closed",
                                     .request_id = request_id_.value(),
                                     .connection_id = request_id_.value(),
                                     .tick_sequence = delivery_state_.last_delivered_tick(),
                                     .close_code = close_code});
  }
  cancel_timer_noexcept(presentation_timer_);
  cancel_timer_noexcept(write_deadline_timer_);
  cancel_timer_noexcept(idle_timer_);
  cancel_timer_noexcept(pong_timer_);
  close_socket();
  delivery_state_.discard();
  if (session_id_.has_value()) {
    server_context_->snapshot_egress_budget().cancel_waiter(*session_id_);
  }
  if (!snapshot_write_operation_active_) {
    release_active_payload();
  }
  websocket_lease_.release();
  tcp_lease_.release();
  if (session_id_.has_value()) {
    server_context_->unregister_session(*session_id_);
    session_id_.reset();
  }
}

void SnapshotWebSocketSession::close_socket() noexcept {
  boost::system::error_code ignored;
  Tcp::socket& socket = boost::beast::get_lowest_layer(websocket_).socket();
  socket.cancel(ignored);
  socket.shutdown(Tcp::socket::shutdown_both, ignored);
  socket.close(ignored);
}

void SnapshotWebSocketSession::report_snapshot_failure(std::exception_ptr failure) noexcept {
  request_close(CloseIntent::kInternalFailure);
  server_context_->fail(failure);
}

std::uint16_t SnapshotWebSocketSession::close_code_for(const CloseIntent close_intent) noexcept {
  switch (close_intent) {
  case CloseIntent::kNormal:
    return static_cast<std::uint16_t>(websocket::close_code::normal);
  case CloseIntent::kServerShutdown:
  case CloseIntent::kPeerTimeout:
    return static_cast<std::uint16_t>(websocket::close_code::going_away);
  case CloseIntent::kProtocolError:
    return static_cast<std::uint16_t>(websocket::close_code::protocol_error);
  case CloseIntent::kInvalidUtf8:
    return static_cast<std::uint16_t>(websocket::close_code::bad_payload);
  case CloseIntent::kClientDataForbidden:
  case CloseIntent::kControlRateExceeded:
    return static_cast<std::uint16_t>(websocket::close_code::policy_error);
  case CloseIntent::kClientMessageTooLarge:
    return static_cast<std::uint16_t>(websocket::close_code::too_big);
  case CloseIntent::kInternalFailure:
    return static_cast<std::uint16_t>(websocket::close_code::internal_error);
  case CloseIntent::kSlowConsumer:
  case CloseIntent::kServiceNotReady:
    return 1013;
  }
  return static_cast<std::uint16_t>(websocket::close_code::internal_error);
}

websocket::close_reason SnapshotWebSocketSession::close_reason_for(const CloseIntent close_intent) {
  websocket::close_reason result;
  result.code = static_cast<websocket::close_code>(close_code_for(close_intent));
  switch (close_intent) {
  case CloseIntent::kNormal:
    result.reason = "normal";
    break;
  case CloseIntent::kServerShutdown:
    result.reason = "server_shutdown";
    break;
  case CloseIntent::kPeerTimeout:
    result.reason = "peer_timeout";
    break;
  case CloseIntent::kProtocolError:
    result.reason = "protocol_error";
    break;
  case CloseIntent::kInvalidUtf8:
    result.reason = "invalid_utf8";
    break;
  case CloseIntent::kClientDataForbidden:
    result.reason = "client_data_forbidden";
    break;
  case CloseIntent::kControlRateExceeded:
    result.reason = "control_rate_exceeded";
    break;
  case CloseIntent::kClientMessageTooLarge:
    result.reason = "client_message_too_large";
    break;
  case CloseIntent::kInternalFailure:
    result.reason = "internal_failure";
    break;
  case CloseIntent::kSlowConsumer:
    result.reason = "slow_consumer";
    break;
  case CloseIntent::kServiceNotReady:
    result.reason = "service_not_ready";
    break;
  }
  return result;
}

std::string SnapshotWebSocketSession::current_utc_timestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
  if (gmtime_r(&time, &utc) == nullptr) {
    throw GameServerError{GameServerErrorCode::kSnapshotEncodingFailed,
                          "snapshot_message.meta.sent_at_utc", "UTC timestamp conversion failed"};
  }
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1'000;
  std::array<char, 32> encoded{};
  const int count =
      std::snprintf(encoded.data(), encoded.size(), "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
                    utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min,
                    utc.tm_sec, static_cast<long long>(milliseconds));
  if (count <= 0 || static_cast<std::size_t>(count) >= encoded.size()) {
    throw GameServerError{GameServerErrorCode::kSnapshotEncodingFailed,
                          "snapshot_message.meta.sent_at_utc", "UTC timestamp formatting failed"};
  }
  return {encoded.data(), static_cast<std::size_t>(count)};
}

} // namespace blob_royale::server
