#ifndef BLOB_ROYALE_SERVER_SESSION_WEBSOCKET_SESSION_HPP
#define BLOB_ROYALE_SERVER_SESSION_WEBSOCKET_SESSION_HPP

#include "game_api_router.hpp"
#include "peer_identity.hpp"
#include "peer_traffic_policy.hpp"
#include "request_id.hpp"
#include "server_execution_context.hpp"
#include "snapshot_delivery_state.hpp"
#include "snapshot_egress_budget.hpp"

#include "command_registry.hpp"
#include "command_submission_result.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "tick_sequence.hpp"

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

// canonical: session_websocket_session -- one protocol v2 `/api/v2/session` connection.
//
// **It is a controller, filling the same role a bot fills, without deriving from anything.** What
// the role requires is two capabilities and a command vocabulary, not an interface: a write-only
// `runtime::CommandSink&`, a `const runtime::SnapshotPublication&`, and the registered `Command`
// kinds (`docs/architecture/0004-gameplay-architecture.md` § "Controllers"). All three command
// sources arrive at the world as indistinguishable `Command` values in one `InputBatch`, which is
// what makes the simulation unable to tell a human from a bot.
//
// **The socket is the credential and the stamp is the ownership check.** A command envelope carries
// no entity id, so there is no field for a client to put someone else's id in; this session stamps
// its own current body onto every decoded command and there is exactly one line that does it. The
// consequence is stated plainly in `docs/protocol/v2.md` § "Abuse cases": a hijacked socket is
// total control of that player's entity, and no per-command check would catch it.
//
// **Every close path calls `close_session` exactly once.** Retirement happens in `finish()`, which
// is already the one idempotent terminal path for the transport, the leases, and the session
// registration -- an abnormal transport close, a rejected command, shutdown, a stalled write, and
// a handshake failure all reach it and none of them reaches it twice.
//
// **A lobby command is logged, a thrust is not.** Anyone in the lobby may resize it, seat or clear
// a bot, and press Start, and the tick logs nothing by contract, so without a line here nobody
// could say afterwards who started a match early or emptied a seat -- which is the whole cost of
// "anyone may" the moment a stranger reaches the listener
// (`docs/reviews/2026-09-08-lobby-and-hazard-review.md`, finding 6). The line carries the request
// id, the kind, the closed payload values, and the sink's answer; every value in it is a bounded
// integer or a registered kind name the boundary already validated, so no client-chosen byte
// reaches a log. A thrust arrives up to twenty times a second per session and says nothing a person
// would look up, so it stays unlogged.
//
// **Disconnect leaves.** `CommandSink::close_session` enqueues the controller's `leave` before
// retiring it, and the tick destroys everything the controller drove -- seated, pending, or still
// queued as a spawn -- and vacates its seat. This session submits nothing on the way out and never
// has to know its own entity to leave cleanly, which a session-side despawn could not promise: it
// despawned only what the last presentation slot had observed
// (`docs/reviews/2026-09-08-lobby-and-hazard-review.md`, finding 1).
//
// The presentation half is protocol v1's algorithm unchanged -- one active write, one replaceable
// pending immutable reference, replacement only by a newer tick, admission against the same global
// egress budget -- with one welcome frame ahead of it.
// related: snapshot_websocket_session.hpp -- the v1 twin, read-only and unchanged.
// related: match_session_context.hpp -- the capability this runs on.
// related: docs/protocol/v2.md -- the accepted contract.
class SessionWebSocketSession final : public std::enable_shared_from_this<SessionWebSocketSession> {
public:
  SessionWebSocketSession(boost::asio::ip::tcp::socket socket,
                          std::shared_ptr<ServerExecutionContext> server_context,
                          std::string peer_address, protocol::RequestId request_id,
                          PeerIdentity peer_identity, WebSocketAdmissionLease websocket_lease,
                          TcpAdmissionLease tcp_lease);

  SessionWebSocketSession(const SessionWebSocketSession&) = delete;
  SessionWebSocketSession(SessionWebSocketSession&&) = delete;
  SessionWebSocketSession& operator=(const SessionWebSocketSession&) = delete;
  SessionWebSocketSession& operator=(SessionWebSocketSession&&) = delete;
  ~SessionWebSocketSession();

  // Registers ownership and consumes the already-validated HTTP upgrade request.
  void run(GameApiHttpRequest request);

private:
  using Clock = std::chrono::steady_clock;
  using WebSocket = boost::beast::websocket::stream<boost::beast::tcp_stream>;

  // Every close this session can initiate or observe. The v2 rows carry the stable reasons of
  // `docs/protocol/v2.md` § "Close codes"; the rest are v1's, unchanged, minus
  // `client_data_forbidden`, which cannot occur on a route that accepts client data.
  enum class CloseIntent {
    kNormal,
    kServerShutdown,
    kPeerTimeout,
    kProtocolError,
    kInvalidUtf8,
    kControlRateExceeded,
    kClientMessageTooLarge,
    kCommandRateExceeded,
    kCommandMalformed,
    kCommandKindRejected,
    kCommandPayloadInvalid,
    kInternalFailure,
    kSlowConsumer,
    kServiceNotReady,
  };

  void accepted(const boost::system::error_code& error);
  void open_match_session();
  void read_client_frame();
  void client_frame_read(const boost::system::error_code& error,
                         std::size_t transferred_byte_count);
  void admit_client_command(std::string_view frame);
  // One `info` line per lobby command that reached the sink, naming the sender, the kind, the
  // closed payload, and what the sink said. Nothing for a kind that is not a lobby command.
  void log_lobby_command(const simulation::Command& command,
                         runtime::CommandSubmissionResult result) const;
  void observe_control_frame(boost::beast::websocket::frame_type frame_type,
                             boost::beast::string_view payload);

  void initialize_presentation_cadence(Clock::time_point opened_at) noexcept;
  void advance_presentation_deadline() noexcept;
  void schedule_presentation_slot();
  void presentation_slot(const boost::system::error_code& error);
  void observe_own_entity(const simulation::WorldSnapshot& snapshot) noexcept;
  void request_body_if_absent(simulation::TickSequence observed) noexcept;
  void start_welcome_write(simulation::EntityId entity, SnapshotEgressLease egress_lease);
  void welcome_written(const boost::system::error_code& error, std::size_t transferred_byte_count);
  void start_snapshot_write(SnapshotDelivery delivery, SnapshotEgressLease egress_lease);
  void snapshot_written(const boost::system::error_code& error, std::size_t transferred_byte_count);
  void data_write_timed_out(const boost::system::error_code& error);

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
  // Retires the controller through the sink, which leaves on its behalf. Idempotent and total;
  // never throws.
  void leave_match() noexcept;
  void close_socket() noexcept;
  void report_session_failure(std::exception_ptr failure) noexcept;
  [[nodiscard]] bool data_write_active() const noexcept;

  [[nodiscard]] static std::string current_utc_timestamp();
  [[nodiscard]] static std::uint16_t close_code_for(CloseIntent close_intent) noexcept;
  [[nodiscard]] static boost::beast::websocket::close_reason
  close_reason_for(CloseIntent close_intent);

  WebSocket websocket_;
  boost::beast::flat_buffer read_buffer_;
  std::shared_ptr<ServerExecutionContext> server_context_;
  std::string peer_address_;
  protocol::RequestId request_id_;
  PeerIdentity peer_identity_;
  WebSocketAdmissionLease websocket_lease_;
  TcpAdmissionLease tcp_lease_;
  std::optional<ServerExecutionContext::SessionId> session_id_;

  boost::asio::steady_timer presentation_timer_;
  boost::asio::steady_timer write_deadline_timer_;
  boost::asio::steady_timer idle_timer_;
  boost::asio::steady_timer pong_timer_;
  SnapshotDeliveryState delivery_state_;
  ControlFrameRatePolicy control_frame_rate_policy_;
  CommandRatePolicy command_rate_policy_;

  Clock::time_point next_presentation_deadline_;
  Clock::time_point last_received_frame_at_;
  std::chrono::nanoseconds presentation_base_period_{0};
  std::uint64_t presentation_period_remainder_{0};
  std::uint64_t presentation_remainder_carry_{0};

  std::string active_write_payload_;
  std::optional<SnapshotEgressLease> active_egress_lease_;
  std::optional<CloseIntent> requested_close_intent_;
  std::optional<std::uint16_t> observed_peer_close_code_;

  // The durable identity this connection was issued, present from admission until retirement.
  std::optional<simulation::ControllerId> controller_;
  // The body this session drives in the most recently observed snapshot. Absent is ordinary: a
  // spawn not yet seated, an elimination, and the lobby wipe all produce it.
  std::optional<simulation::EntityId> current_entity_;
  std::optional<simulation::TickSequence> last_spawn_request_tick_;

  bool handshake_completed_{false};
  bool control_write_active_{false};
  bool welcome_write_active_{false};
  bool welcome_delivered_{false};
  bool snapshot_write_operation_active_{false};
  bool idle_wait_active_{false};
  bool waiting_for_pong_{false};
  bool close_requested_{false};
  bool close_started_{false};
  bool left_match_{false};
  bool finished_{false};
};

} // namespace blob_royale::server

#endif
