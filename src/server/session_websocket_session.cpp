#include "session_websocket_session.hpp"

#include "game_server_error.hpp"
#include "match_session_context.hpp"
#include "server_limits.hpp"

#include "command_decoding.hpp"
#include "protocol_encoding_error.hpp"
#include "protocol_v2_constants.hpp"
#include "protocol_v2_json_encoding.hpp"
#include "session_welcome.hpp"

#include "command_sink.hpp"
#include "command_sink_error.hpp"

#include "command_registry.hpp"
#include "commands/despawn_command.hpp"
#include "commands/spawn_command.hpp"
#include "controller_id.hpp"
#include "world_snapshot.hpp"

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
#include <span>
#include <string>
#include <utility>
#include <vector>

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

// The controller kind every networked player publishes. It is the one value a client uses to tell
// a player from a bot, and the simulation never branches on it
// (`docs/protocol/v2.md` § "Entities, controllers, and what survives what").
inline constexpr std::string_view kSessionControllerKind = "session";

} // namespace

SessionWebSocketSession::SessionWebSocketSession(
    Tcp::socket socket, std::shared_ptr<ServerExecutionContext> server_context,
    std::string peer_address, protocol::RequestId request_id, PeerIdentity peer_identity,
    WebSocketAdmissionLease websocket_lease, TcpAdmissionLease tcp_lease)
    : websocket_(std::move(socket)),
      read_buffer_(ServerLimits::kInboundWebSocketMessageMaximumByteCount),
      server_context_(std::move(server_context)), peer_address_(std::move(peer_address)),
      request_id_(std::move(request_id)), peer_identity_(std::move(peer_identity)),
      websocket_lease_(std::move(websocket_lease)), tcp_lease_(std::move(tcp_lease)),
      presentation_timer_(websocket_.get_executor()),
      write_deadline_timer_(websocket_.get_executor()), idle_timer_(websocket_.get_executor()),
      pong_timer_(websocket_.get_executor()), control_frame_rate_policy_(Clock::now()),
      command_rate_policy_(Clock::now()), next_presentation_deadline_(Clock::now()),
      last_received_frame_at_(Clock::now()) {}

SessionWebSocketSession::~SessionWebSocketSession() = default;

void SessionWebSocketSession::run(GameApiHttpRequest request) {
  const std::weak_ptr<SessionWebSocketSession> weak_self = shared_from_this();
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
          // The route selected this token; the client's offer list never did.
          response.set(boost::beast::http::field::sec_websocket_protocol,
                       game_api_upgrade_subprotocol(GameApiUpgradeRoute::kSessionV2));
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

void SessionWebSocketSession::accepted(const boost::system::error_code& error) {
  if (error) {
    const std::string error_detail = error.message();
    server_context_->logger().write({.severity = observability::LogSeverity::kWarning,
                                     .event = "session.handshake_failed",
                                     .request_id = request_id_.value(),
                                     .connection_id = request_id_.value(),
                                     .context = "session.accept",
                                     .detail = error_detail});
    finish();
    return;
  }
  handshake_completed_ = true;
  boost::beast::get_lowest_layer(websocket_).expires_never();
  const Clock::time_point opened_at = Clock::now();
  last_received_frame_at_ = opened_at;
  initialize_presentation_cadence(opened_at);
  try {
    open_match_session();
  } catch (const runtime::CommandSinkError& sink_error) {
    // A full controller directory or an exhausted id space refuses this one join and nothing else.
    // It is a capacity answer, not a server failure, so the connection closes and the process
    // keeps serving every other session.
    const std::string detail = sink_error.what();
    server_context_->logger().write({.severity = observability::LogSeverity::kWarning,
                                     .event = "session.join_refused",
                                     .request_id = request_id_.value(),
                                     .connection_id = request_id_.value(),
                                     .error_code = sink_error.code(),
                                     .context = sink_error.context(),
                                     .detail = detail});
    request_close(CloseIntent::kServiceNotReady);
    return;
  } catch (...) {
    // Any other failure is the server's, not this peer's. It is retained and propagated to the
    // composition root rather than escaping an asio handler, which would terminate the process.
    report_session_failure(std::current_exception());
    return;
  }
  server_context_->logger().write({.severity = observability::LogSeverity::kInfo,
                                   .event = "session.opened",
                                   .request_id = request_id_.value(),
                                   .connection_id = request_id_.value()});
  read_client_frame();
  schedule_idle_check();
  schedule_presentation_slot();
}

void SessionWebSocketSession::open_match_session() {
  // The display name is decided once, here, and the same string is what the welcome carries and
  // what the directory publishes to every peer. Deciding it later would be renaming a player
  // mid-match, which `runtime::ControllerDirectory` refuses by design.
  const std::string display_name = peer_identity_.display_name_for(*session_id_);
  if (peer_identity_.display_name_outcome() != DisplayNameOutcome::kProxySupplied) {
    // The outcome and the received length, and never the received value. A byte of a forwarding
    // header must not reach a log line any more than it may reach a response body.
    const std::string detail =
        std::string{"display_name_outcome="}
            .append(display_name_outcome_name(peer_identity_.display_name_outcome()))
            .append(" received_display_name_length=")
            .append(std::to_string(peer_identity_.received_display_name_length()))
            .append(" peer_classification=")
            .append(peer_classification_name(peer_identity_.classification()));
    server_context_->logger().write({.severity = observability::LogSeverity::kWarning,
                                     .event = "session.display_name_fallback",
                                     .request_id = request_id_.value(),
                                     .connection_id = request_id_.value(),
                                     .context = "session.display_name",
                                     .detail = detail});
  }
  controller_ = server_context_->match_session().command_sink().open_session(kSessionControllerKind,
                                                                             display_name);
}

void SessionWebSocketSession::read_client_frame() {
  if (finished_ || close_requested_) {
    return;
  }
  websocket_.async_read(read_buffer_,
                        [self = shared_from_this()](const boost::system::error_code& error,
                                                    const std::size_t transferred_byte_count) {
                          self->client_frame_read(error, transferred_byte_count);
                        });
}

void SessionWebSocketSession::client_frame_read(const boost::system::error_code& error,
                                                const std::size_t transferred_byte_count) {
  static_cast<void>(transferred_byte_count);
  if (error == websocket::error::closed) {
    // Any client-initiated close is an ordinary leave and the reason string is never parsed.
    const websocket::close_reason& peer_reason = websocket_.reason();
    if (peer_reason.code != websocket::close_code::none) {
      observed_peer_close_code_ = static_cast<std::uint16_t>(peer_reason.code);
    }
    finish();
    return;
  }
  // Admission-order steps 1 and 2: the size limit is enforced by `read_message_max` before
  // buffering completes, and UTF-8 and framing are the transport's own answers.
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
  const auto buffered = read_buffer_.data();
  const std::string_view frame{static_cast<const char*>(buffered.data()), buffered.size()};
  const bool is_text_frame = websocket_.got_text();
  admit_client_command(is_text_frame ? frame : std::string_view{});
  read_buffer_.consume(read_buffer_.size());
  if (!close_requested_ && !finished_) {
    read_client_frame();
  }
}

void SessionWebSocketSession::admit_client_command(const std::string_view frame) {
  // Admission-order step 3, and its position is the control: the token is charged before any JSON
  // parsing, so one token can never buy unbounded parser work.
  if (!command_rate_policy_.consume(Clock::now())) {
    request_close(CloseIntent::kCommandRateExceeded);
    return;
  }

  // Steps 4 through 8 belong to the decoder, which is total and never throws. `stamped_entity` is
  // this session's own current body and is the only entity a decoded command can address: the
  // envelope has no entity field, so entity spoofing is a shape that does not exist rather than a
  // check a refactor could drop.
  //
  // When the session currently owns no body the decode still runs, because a malformed frame must
  // fail identically before and after a spawn -- a session whose failure mode changed with its
  // liveness would be a probing signal. The placeholder below is never submitted.
  const simulation::EntityId stamped_entity =
      current_entity_.value_or(simulation::EntityId::create(1));
  const simulation::ControllerId stamped_controller =
      controller_.value_or(simulation::ControllerId::create(1));
  const protocol::CommandDecodeResult decoded = protocol::decode_command_envelope(
      frame, server_context_->match_session().accepted_command_kinds(), stamped_entity,
      stamped_controller, server_context_->match_session().npc_controller_kinds());
  if (!decoded.is_accepted()) {
    switch (decoded.rejection()) {
    case protocol::CommandDecodeRejection::kMessageTooLarge:
      request_close(CloseIntent::kClientMessageTooLarge);
      return;
    case protocol::CommandDecodeRejection::kKindRejected:
      request_close(CloseIntent::kCommandKindRejected);
      return;
    case protocol::CommandDecodeRejection::kPayloadInvalid:
      request_close(CloseIntent::kCommandPayloadInvalid);
      return;
    case protocol::CommandDecodeRejection::kMalformed:
    case protocol::CommandDecodeRejection::kAccepted:
      request_close(CloseIntent::kCommandMalformed);
      return;
    }
    request_close(CloseIntent::kCommandMalformed);
    return;
  }

  if (!controller_.has_value()) {
    // No open session, so there is no identity to submit under and nothing to submit to.
    return;
  }
  // **What a command needs in order to be submittable is which identity it addresses**, and the
  // command registry already answers that: a kind that names an entity needs this session to own
  // one, and a kind that names only its sender does not
  // (`src/simulation/command_registry.hpp`, AddressedIdentity).
  //
  // The distinction is not academic. A session owns no body for the whole window between the lobby
  // wipe and its reseat, and it may own none at all while a full spawn ring defers it -- and both
  // are exactly the moments a player is *looking at the lobby*, deciding a seat count and pressing
  // Start. Gating every kind on a live body, which is what this did before the lobby existed, would
  // have made the Start button dead precisely when it matters and would have been invisible in
  // every test that owned a blob.
  if (simulation::addressed_identity_of(*decoded.command()).entity().has_value() &&
      !current_entity_.has_value()) {
    // Elimination and an in-flight keystroke race by construction. The command is charged and
    // validated and then has nothing to address, which is not an error and must not close the
    // connection: closing here would disconnect honest clients at the most visible moment.
    return;
  }
  static_cast<void>(
      server_context_->match_session().command_sink().submit(*controller_, *decoded.command()));
}

void SessionWebSocketSession::observe_control_frame(const websocket::frame_type frame_type,
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
  // The control budget and the command budget are separate ledgers; a peer cannot spend one on
  // the other.
  if (!control_frame_rate_policy_.consume(now)) {
    boost::asio::post(websocket_.get_executor(), [self = shared_from_this()] {
      self->request_close(CloseIntent::kControlRateExceeded);
    });
  }
  schedule_idle_check();
}

void SessionWebSocketSession::initialize_presentation_cadence(
    const Clock::time_point opened_at) noexcept {
  constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000ULL;
  const std::uint64_t rate = server_context_->config().snapshots_per_second();
  presentation_base_period_ = std::chrono::nanoseconds{kNanosecondsPerSecond / rate};
  presentation_period_remainder_ = kNanosecondsPerSecond % rate;
  presentation_remainder_carry_ = 0;
  next_presentation_deadline_ = opened_at;
  advance_presentation_deadline();
}

void SessionWebSocketSession::advance_presentation_deadline() noexcept {
  next_presentation_deadline_ += presentation_base_period_;
  presentation_remainder_carry_ += presentation_period_remainder_;
  const std::uint64_t rate = server_context_->config().snapshots_per_second();
  if (presentation_remainder_carry_ >= rate) {
    next_presentation_deadline_ += std::chrono::nanoseconds{1};
    presentation_remainder_carry_ -= rate;
  }
}

void SessionWebSocketSession::schedule_presentation_slot() {
  if (finished_ || close_requested_) {
    return;
  }
  presentation_timer_.expires_at(next_presentation_deadline_);
  presentation_timer_.async_wait(
      [self = shared_from_this()](const boost::system::error_code& error) {
        self->presentation_slot(error);
      });
}

void SessionWebSocketSession::presentation_slot(const boost::system::error_code& error) {
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
      const std::shared_ptr<const simulation::WorldSnapshot> latest =
          server_context_->publication().latest();
      observe_own_entity(*latest);
      request_body_if_absent(latest->tick_sequence());

      if (!welcome_delivered_) {
        // The welcome precedes every snapshot, and it cannot be built before this session has a
        // body to name. Until the write completes the session holds no pending snapshot and
        // retains no snapshot reference, so a client never sees world state before it knows which
        // blob is its own -- and `data_write_active()` is what keeps a second welcome from being
        // started while the first is still on the wire.
        if (!data_write_active() && current_entity_.has_value()) {
          std::optional<SnapshotEgressLease> egress_lease =
              server_context_->snapshot_egress_budget().try_reserve(*session_id_, now);
          if (egress_lease.has_value()) {
            start_welcome_write(*current_entity_, std::move(*egress_lease));
          }
        }
      } else {
        delivery_state_.observe(latest);
        if (delivery_state_.ready_to_write()) {
          std::optional<SnapshotEgressLease> egress_lease =
              server_context_->snapshot_egress_budget().try_reserve(*session_id_, now);
          if (egress_lease.has_value()) {
            const std::optional<SnapshotDelivery> delivery = delivery_state_.begin_active_write();
            if (!delivery.has_value()) {
              throw GameServerError{GameServerErrorCode::kSessionInvariantFailed,
                                    "session_delivery.admission",
                                    "admitted egress work has no pending snapshot"};
            }
            start_snapshot_write(*delivery, std::move(*egress_lease));
          }
        }
      }
    }
  } catch (...) {
    report_session_failure(std::current_exception());
    return;
  }
  schedule_presentation_slot();
}

void SessionWebSocketSession::observe_own_entity(
    const simulation::WorldSnapshot& snapshot) noexcept {
  if (!controller_.has_value()) {
    return;
  }
  // The client's own rule, run once on this side too: resolve the body by controller id every
  // frame. An absent answer is the ordinary state of a player who is eliminated, waiting for the
  // next match, or deferred by the mode's spawn policy.
  current_entity_ = protocol::find_controlled_body(snapshot, *controller_);
  // And, separately, what this session *owns*. A deferred or unseated session owns an entity and
  // has no body, and the close path has to despawn the one it owns or leave it in the world
  // forever.
  current_controlled_entity_ = protocol::find_controlled_entity(snapshot, *controller_);
  if (current_entity_.has_value()) {
    last_spawn_request_tick_.reset();
  }
}

void SessionWebSocketSession::request_body_if_absent(
    const simulation::TickSequence observed) noexcept {
  if (!controller_.has_value() || current_entity_.has_value()) {
    return;
  }
  if (last_spawn_request_tick_.has_value() &&
      observed.value() <
          last_spawn_request_tick_->value() + ServerLimits::kSessionSpawnRequestRetryTicks) {
    // A request is still plausibly in flight. Asking again here is what would give one session two
    // blobs.
    return;
  }
  last_spawn_request_tick_ = observed;
  static_cast<void>(server_context_->match_session().command_sink().submit(
      *controller_, simulation::Command{simulation::SpawnCommand{*controller_}}));
}

void SessionWebSocketSession::start_welcome_write(const simulation::EntityId entity,
                                                  SnapshotEgressLease egress_lease) {
  active_egress_lease_.emplace(std::move(egress_lease));
  const MatchSessionContext& match_session = server_context_->match_session();
  try {
    const std::span<const std::string> npc_controller_kinds = match_session.npc_controller_kinds();
    const protocol::SessionWelcome welcome = protocol::SessionWelcome::create(
        entity, *controller_, peer_identity_.display_name_for(*session_id_),
        std::string{server_context_->publication().latest()->match().mode_name()},
        match_session.map_name(), match_session.accepted_command_kinds(),
        std::vector<std::string>{npc_controller_kinds.begin(), npc_controller_kinds.end()});
    active_write_payload_ = protocol::encode_welcome_message(
        welcome, request_id_, current_utc_timestamp(), active_egress_lease_->owned_byte_count());
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
        self->data_write_timed_out(error);
      });
  welcome_write_active_ = true;
  snapshot_write_operation_active_ = true;
  try {
    websocket_.async_write(boost::asio::buffer(active_write_payload_),
                           [self = shared_from_this()](const boost::system::error_code& error,
                                                       const std::size_t transferred_byte_count) {
                             self->welcome_written(error, transferred_byte_count);
                           });
  } catch (...) {
    welcome_write_active_ = false;
    snapshot_write_operation_active_ = false;
    throw;
  }
}

void SessionWebSocketSession::welcome_written(const boost::system::error_code& error,
                                              const std::size_t transferred_byte_count) {
  static_cast<void>(transferred_byte_count);
  welcome_write_active_ = false;
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
  welcome_delivered_ = true;
  if (close_requested_) {
    begin_close();
  }
}

void SessionWebSocketSession::start_snapshot_write(SnapshotDelivery delivery,
                                                   SnapshotEgressLease egress_lease) {
  active_egress_lease_.emplace(std::move(egress_lease));
  try {
    // `SnapshotDeliveryState` counts delivered snapshots from one; on a v2 session the welcome
    // already spent message sequence one, so a snapshot's sequence is its delivery number plus the
    // welcome's. The first snapshot is therefore two, which is what the schema pins.
    active_write_payload_ = protocol::encode_snapshot_message_v2(
        *delivery.snapshot, server_context_->match_session().directory_view(), request_id_,
        delivery.message_sequence + protocol::kWelcomeMessageSequence, current_utc_timestamp(),
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
        self->data_write_timed_out(error);
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

void SessionWebSocketSession::snapshot_written(const boost::system::error_code& error,
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
    report_session_failure(std::current_exception());
    return;
  }
  if (close_requested_) {
    begin_close();
  }
}

void SessionWebSocketSession::data_write_timed_out(const boost::system::error_code& error) {
  if (error == boost::asio::error::operation_aborted || finished_) {
    return;
  }
  requested_close_intent_ = CloseIntent::kSlowConsumer;
  close_socket();
  finish();
}

bool SessionWebSocketSession::data_write_active() const noexcept {
  return welcome_write_active_ || delivery_state_.write_active();
}

void SessionWebSocketSession::schedule_idle_check() {
  if (finished_ || close_requested_ || idle_wait_active_) {
    return;
  }
  idle_wait_active_ = true;
  idle_timer_.expires_at(last_received_frame_at_ + ServerLimits::kWebSocketIdlePingInterval);
  idle_timer_.async_wait([self = shared_from_this()](const boost::system::error_code& error) {
    self->idle_check(error);
  });
}

void SessionWebSocketSession::idle_check(const boost::system::error_code& error) {
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
  if (data_write_active() || control_write_active_) {
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

void SessionWebSocketSession::ping_written(const boost::system::error_code& error) {
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

void SessionWebSocketSession::pong_timed_out(const boost::system::error_code& error) {
  if (error == boost::asio::error::operation_aborted || finished_ || !waiting_for_pong_) {
    return;
  }
  request_close(CloseIntent::kPeerTimeout);
}

void SessionWebSocketSession::request_close(const CloseIntent close_intent) noexcept {
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
  // The arena must not keep a disconnected player's blob while a close frame is in flight, so the
  // despawn is submitted as soon as leaving is decided rather than when the socket finally dies.
  leave_match();
  if (!data_write_active() && !control_write_active_) {
    try {
      begin_close();
    } catch (...) {
      server_context_->fail(std::current_exception());
      finish();
    }
  }
}

void SessionWebSocketSession::begin_close() {
  if (finished_ || close_started_ || data_write_active() || control_write_active_) {
    return;
  }
  if (!requested_close_intent_.has_value()) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed, "session.close",
                          "close began without a requested close intent"};
  }
  close_started_ = true;
  websocket_.async_close(
      close_reason_for(*requested_close_intent_),
      [self = shared_from_this()](const boost::system::error_code& error) { self->closed(error); });
}

void SessionWebSocketSession::closed(const boost::system::error_code& error) {
  static_cast<void>(error);
  finish();
}

void SessionWebSocketSession::request_stop(const SessionStopMode mode) noexcept {
  if (mode == SessionStopMode::kImmediate) {
    close_socket();
    finish();
    return;
  }
  request_close(CloseIntent::kServerShutdown);
}

void SessionWebSocketSession::release_active_payload() noexcept {
  std::string{}.swap(active_write_payload_);
  active_egress_lease_.reset();
}

void SessionWebSocketSession::leave_match() noexcept {
  if (left_match_ || !controller_.has_value()) {
    return;
  }
  left_match_ = true;
  try {
    runtime::CommandSink& command_sink = server_context_->match_session().command_sink();
    // **Ownership, not a body.** A session that was never seated -- deferred by a full spawn ring,
    // or still sitting in a lobby -- owns an entity carrying only its `Controllable`, and
    // despawning only bodies left that entity in the world for the rest of the process:
    // unrenderable, uncounted as a player, and destroyed by nothing. It was reachable before the
    // lobby existed and the lobby makes it the normal case, because sitting in a lobby without a
    // body is what a player about to play is doing.
    if (current_controlled_entity_.has_value()) {
      // The despawn precedes the retirement, because `CommandSink::submit` refuses a closed
      // session. A despawn naming a body the tick has already destroyed is ignored by contract,
      // so racing an elimination is benign; a despawn the mailbox drops is counted and the
      // composition root reports it at error severity, because the roster itself lost a change.
      static_cast<void>(command_sink.submit(
          *controller_,
          simulation::Command{simulation::DespawnCommand{*current_controlled_entity_}}));
    }
    static_cast<void>(command_sink.close_session(*controller_));
  } catch (...) {
    // Neither operation throws by contract; a terminal path may not propagate regardless.
  }
}

void SessionWebSocketSession::finish() noexcept {
  if (finished_) {
    return;
  }
  finished_ = true;
  // Exactly once on every close path, including an abnormal transport close, a rejected command,
  // shutdown, a stalled write, and a handshake that never completed.
  leave_match();
  if (handshake_completed_) {
    std::optional<std::uint16_t> close_code = observed_peer_close_code_;
    if (!close_code.has_value() && requested_close_intent_.has_value()) {
      close_code = close_code_for(*requested_close_intent_);
    }
    server_context_->logger().write({.severity = observability::LogSeverity::kInfo,
                                     .event = "session.closed",
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

void SessionWebSocketSession::close_socket() noexcept {
  boost::system::error_code ignored;
  Tcp::socket& socket = boost::beast::get_lowest_layer(websocket_).socket();
  socket.cancel(ignored);
  socket.shutdown(Tcp::socket::shutdown_both, ignored);
  socket.close(ignored);
}

void SessionWebSocketSession::report_session_failure(std::exception_ptr failure) noexcept {
  request_close(CloseIntent::kInternalFailure);
  server_context_->fail(failure);
}

std::uint16_t SessionWebSocketSession::close_code_for(const CloseIntent close_intent) noexcept {
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
  case CloseIntent::kControlRateExceeded:
  case CloseIntent::kCommandRateExceeded:
  case CloseIntent::kCommandMalformed:
  case CloseIntent::kCommandKindRejected:
  case CloseIntent::kCommandPayloadInvalid:
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

websocket::close_reason SessionWebSocketSession::close_reason_for(const CloseIntent close_intent) {
  websocket::close_reason result;
  result.code = static_cast<websocket::close_code>(close_code_for(close_intent));
  // Every reason below is a fixed ASCII diagnostic. None is derived from client input.
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
  case CloseIntent::kControlRateExceeded:
    result.reason = "control_rate_exceeded";
    break;
  case CloseIntent::kClientMessageTooLarge:
    result.reason = "client_message_too_large";
    break;
  case CloseIntent::kCommandRateExceeded:
    result.reason = "command_rate_exceeded";
    break;
  case CloseIntent::kCommandMalformed:
    result.reason = "command_malformed";
    break;
  case CloseIntent::kCommandKindRejected:
    result.reason = "command_kind_rejected";
    break;
  case CloseIntent::kCommandPayloadInvalid:
    result.reason = "command_payload_invalid";
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

std::string SessionWebSocketSession::current_utc_timestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
  if (gmtime_r(&time, &utc) == nullptr) {
    throw GameServerError{GameServerErrorCode::kSnapshotEncodingFailed,
                          "session_message.meta.sent_at_utc", "UTC timestamp conversion failed"};
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
                          "session_message.meta.sent_at_utc", "UTC timestamp formatting failed"};
  }
  return {encoded.data(), static_cast<std::size_t>(count)};
}

} // namespace blob_royale::server
