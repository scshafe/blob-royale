#ifndef BLOB_ROYALE_SERVER_SNAPSHOT_WEBSOCKET_SESSION_HPP
#define BLOB_ROYALE_SERVER_SNAPSHOT_WEBSOCKET_SESSION_HPP

#include "game_api_router.hpp"
#include "peer_traffic_policy.hpp"
#include "request_id.hpp"
#include "server_execution_context.hpp"
#include "snapshot_delivery_state.hpp"
#include "snapshot_egress_budget.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace blob_royale::server {

// canonical: snapshot_websocket_session -- one read-only, server-paced snapshot stream.
class SnapshotWebSocketSession final
    : public std::enable_shared_from_this<SnapshotWebSocketSession> {
public:
  SnapshotWebSocketSession(boost::asio::ip::tcp::socket socket,
                           std::shared_ptr<ServerExecutionContext> server_context,
                           std::string peer_address, protocol::RequestId request_id,
                           WebSocketAdmissionLease websocket_lease, TcpAdmissionLease tcp_lease);

  SnapshotWebSocketSession(const SnapshotWebSocketSession&) = delete;
  SnapshotWebSocketSession(SnapshotWebSocketSession&&) = delete;
  SnapshotWebSocketSession& operator=(const SnapshotWebSocketSession&) = delete;
  SnapshotWebSocketSession& operator=(SnapshotWebSocketSession&&) = delete;
  ~SnapshotWebSocketSession();

  // Registers ownership and consumes the already-validated HTTP upgrade request.
  void run(GameApiHttpRequest request);

private:
  using Clock = std::chrono::steady_clock;
  using WebSocket = boost::beast::websocket::stream<boost::beast::tcp_stream>;

  enum class CloseIntent {
    kNormal,
    kServerShutdown,
    kPeerTimeout,
    kProtocolError,
    kInvalidUtf8,
    kClientDataForbidden,
    kControlRateExceeded,
    kClientMessageTooLarge,
    kInternalFailure,
    kSlowConsumer,
    kServiceNotReady,
  };

  void accepted(const boost::system::error_code& error);
  void read_client_frame();
  void client_frame_read(const boost::system::error_code& error,
                         std::size_t transferred_byte_count);
  void observe_control_frame(boost::beast::websocket::frame_type frame_type,
                             boost::beast::string_view payload);

  void initialize_presentation_cadence(Clock::time_point opened_at) noexcept;
  void advance_presentation_deadline() noexcept;
  void schedule_presentation_slot();
  void presentation_slot(const boost::system::error_code& error);
  void start_snapshot_write(SnapshotDelivery delivery, SnapshotEgressLease egress_lease);
  void snapshot_written(const boost::system::error_code& error, std::size_t transferred_byte_count);
  void snapshot_write_timed_out(const boost::system::error_code& error);

  void schedule_idle_check();
  void idle_check(const boost::system::error_code& error);
  void ping_written(const boost::system::error_code& error);
  void pong_timed_out(const boost::system::error_code& error);

  void request_close(CloseIntent close_intent) noexcept;
  void begin_close();
  void closed(const boost::system::error_code& error);
  void request_stop(SessionStopMode mode) noexcept;
  void release_active_payload() noexcept;
  void finish() noexcept;
  void close_socket() noexcept;
  void report_snapshot_failure(std::exception_ptr failure) noexcept;

  [[nodiscard]] static std::string current_utc_timestamp();
  [[nodiscard]] static std::uint16_t close_code_for(CloseIntent close_intent) noexcept;
  [[nodiscard]] static boost::beast::websocket::close_reason
  close_reason_for(CloseIntent close_intent);

  WebSocket websocket_;
  boost::beast::flat_buffer read_buffer_;
  std::shared_ptr<ServerExecutionContext> server_context_;
  std::string peer_address_;
  protocol::RequestId request_id_;
  WebSocketAdmissionLease websocket_lease_;
  TcpAdmissionLease tcp_lease_;
  std::optional<ServerExecutionContext::SessionId> session_id_;

  boost::asio::steady_timer presentation_timer_;
  boost::asio::steady_timer write_deadline_timer_;
  boost::asio::steady_timer idle_timer_;
  boost::asio::steady_timer pong_timer_;
  SnapshotDeliveryState delivery_state_;
  ControlFrameRatePolicy control_frame_rate_policy_;

  Clock::time_point next_presentation_deadline_;
  Clock::time_point last_received_frame_at_;
  std::chrono::nanoseconds presentation_base_period_{0};
  std::uint64_t presentation_period_remainder_{0};
  std::uint64_t presentation_remainder_carry_{0};

  std::string active_write_payload_;
  std::optional<SnapshotEgressLease> active_egress_lease_;
  std::optional<CloseIntent> requested_close_intent_;
  std::optional<std::uint16_t> observed_peer_close_code_;
  bool handshake_completed_{false};
  bool control_write_active_{false};
  bool snapshot_write_operation_active_{false};
  bool idle_wait_active_{false};
  bool waiting_for_pong_{false};
  bool close_requested_{false};
  bool close_started_{false};
  bool finished_{false};
};

} // namespace blob_royale::server

#endif
