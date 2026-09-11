#include "game_api_router.hpp"

#include "http_error.hpp"
#include "lobby_directory.hpp"
#include "lobby_listing.hpp"
#include "peer_identity.hpp"
#include "protocol_constants.hpp"
#include "protocol_encoding_error.hpp"
#include "protocol_json_encoding.hpp"
#include "protocol_v3_json_encoding.hpp"
#include "server_limits.hpp"
#include "v3_http_error.hpp"

#include "match_snapshot.hpp"
#include "seat_roster.hpp"
#include "world_snapshot.hpp"

#include <boost/asio/ip/address.hpp>
#include <boost/beast/core/string.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/websocket/rfc6455.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace blob_royale::server {
namespace {

namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;

inline constexpr std::string_view kConfigTarget = "/api/v1/config";
inline constexpr std::string_view kLivenessTarget = "/api/v1/health/live";
inline constexpr std::string_view kReadinessTarget = "/api/v1/health/ready";
inline constexpr std::string_view kSnapshotsTarget = "/api/v1/snapshots";
inline constexpr std::string_view kLobbyDirectoryTarget = "/api/v3/lobbies";
inline constexpr std::string_view kRetiredSessionTarget = "/api/v2/session";
inline constexpr std::string_view kRetiredLobbyDirectoryTarget = "/api/v2/lobbies";
inline constexpr std::string_view kRetiredLobbyRoomTargetPrefix = "/api/v2/lobbies/";
// The two exact halves of the one parametric target, `/api/v3/lobbies/<lobby_id>/session`.
inline constexpr std::string_view kLobbyRoomTargetPrefix = "/api/v3/lobbies/";
inline constexpr std::string_view kLobbyRoomTargetSuffix = "/session";
inline constexpr std::size_t kLobbyIdSegmentMaximumDigits = 3;
inline constexpr std::string_view kRequestIdHeader = "X-Request-ID";
// Parsed current and retired session prefixes select the one current envelope even when their
// target is malformed or an earlier global security check fails. Route retirement is separate.
inline constexpr std::string_view kProtocolV3TargetPrefix = "/api/v3/";
inline constexpr std::string_view kRetiredProtocolTargetPrefix = "/api/v2/";
// The room every retained v1 route serves.
inline constexpr std::uint64_t kRoomOne = 1;

// What one parsed target names. Route selection is by exact target and never by offered
// subprotocol, and the one parametric segment is matched by grammar and resolved through the
// directory rather than parsed as a number and trusted.
enum class TargetKind : std::uint8_t {
  kConfig,
  kLiveness,
  kReadiness,
  kLobbyDirectory,
  kSnapshotsV1Upgrade,
  kSessionV3Upgrade,
  kRetiredSessionVersion,
  // `/api/v3/lobbies/<something>` where the something is not a room: outside the grammar, or a
  // well-formed id the directory does not hold. One answer for both, `404 LOBBY.NOT_FOUND`.
  kLobbyNotFound,
  kUnknown,
};

struct ResolvedTarget final {
  TargetKind kind;
  // The room an upgrade admits into; meaningful for the two upgrade kinds only.
  std::uint64_t lobby_id;
};

// `<lobby_id>` from the bytes after `/api/v3/lobbies/`, when those bytes are exactly
// `[1-9][0-9]{0,2}` followed by `/session` and nothing else. A leading zero, a fourth digit, a
// trailing slash, a percent-encoded digit, a query string, and an empty segment all fail here.
[[nodiscard]] std::optional<std::uint64_t>
parse_lobby_room_target(const std::string_view after_prefix) noexcept {
  const std::size_t slash = after_prefix.find('/');
  if (slash == std::string_view::npos || slash == 0 || slash > kLobbyIdSegmentMaximumDigits) {
    return std::nullopt;
  }
  if (after_prefix.substr(slash) != kLobbyRoomTargetSuffix) {
    return std::nullopt;
  }
  const std::string_view segment = after_prefix.substr(0, slash);
  if (segment.front() < '1' || segment.front() > '9') {
    return std::nullopt;
  }
  std::uint64_t lobby_id = 0;
  for (const char character : segment) {
    if (character < '0' || character > '9') {
      return std::nullopt;
    }
    lobby_id = (lobby_id * 10) + static_cast<std::uint64_t>(character - '0');
  }
  return lobby_id;
}

[[nodiscard]] ResolvedTarget resolve_target(const std::string_view target,
                                            const LobbyDirectory& lobbies) noexcept {
  if (target == kConfigTarget) {
    return {TargetKind::kConfig, kRoomOne};
  }
  if (target == kLivenessTarget) {
    return {TargetKind::kLiveness, kRoomOne};
  }
  if (target == kReadinessTarget) {
    return {TargetKind::kReadiness, kRoomOne};
  }
  if (target == kSnapshotsTarget) {
    return {TargetKind::kSnapshotsV1Upgrade, kRoomOne};
  }
  // Retirement is classified by exact grammar before looking up any room. A valid old id must
  // not probe whether that room exists or reach handshake/admission resource consumption.
  if (target == kRetiredSessionTarget || target == kRetiredLobbyDirectoryTarget) {
    return {TargetKind::kRetiredSessionVersion, 0};
  }
  if (target.starts_with(kRetiredLobbyRoomTargetPrefix)) {
    const std::optional<std::uint64_t> lobby_id =
        parse_lobby_room_target(target.substr(kRetiredLobbyRoomTargetPrefix.size()));
    return {lobby_id.has_value() ? TargetKind::kRetiredSessionVersion : TargetKind::kLobbyNotFound,
            0};
  }
  if (target == kLobbyDirectoryTarget) {
    return {TargetKind::kLobbyDirectory, kRoomOne};
  }
  if (target.starts_with(kLobbyRoomTargetPrefix)) {
    const std::optional<std::uint64_t> lobby_id =
        parse_lobby_room_target(target.substr(kLobbyRoomTargetPrefix.size()));
    if (lobby_id.has_value() && lobbies.find(*lobby_id) != nullptr) {
      return {TargetKind::kSessionV3Upgrade, *lobby_id};
    }
    return {TargetKind::kLobbyNotFound, 0};
  }
  return {TargetKind::kUnknown, 0};
}

// v1's readiness rule, per room: the runtime is active and has committed a tick. It is what the
// readiness probe asks of room 1, what a session upgrade asks of its room, and what the directory
// publishes as `healthy`, from one predicate.
[[nodiscard]] bool room_is_serving(const LobbyEntry& room) noexcept {
  const runtime::SnapshotPublication& publication = room.snapshot_publication();
  return publication.is_ready() && publication.latest()->tick_sequence().value() != 0;
}

// `409 LOBBY.FULL`'s condition: the sessions admitted to the room already number its seats. A
// world with no lobby publishes an empty roster and is never full this way, because it has no
// seats for anybody to be refused from (`docs/protocol/v3.md` § "The lobby directory").
[[nodiscard]] bool room_is_full(const LobbyEntry& room) noexcept {
  const std::size_t seat_count = room.snapshot_publication().latest()->match().seats().seat_count();
  return seat_count != 0 && room.session_count() >= seat_count;
}

// One directory row, read from the room's latest committed snapshot and the server's own admission
// count. The two are read at different instants and the directory says so: it is advice, and
// admission is the decision.
[[nodiscard]] protocol::LobbyListing lobby_listing_of(const LobbyEntry& room) {
  const std::shared_ptr<const simulation::WorldSnapshot> latest =
      room.snapshot_publication().latest();
  const simulation::MatchSnapshot& match = latest->match();
  const simulation::SeatRoster& seats = match.seats();
  std::uint64_t filled_seat_count = 0;
  std::uint64_t npc_seat_count = 0;
  for (const simulation::Seat& seat : seats.seats()) {
    filled_seat_count += simulation::seat_is_filled(seat) ? 1U : 0U;
    npc_seat_count += std::holds_alternative<simulation::NpcSeat>(seat) ? 1U : 0U;
  }
  return protocol::LobbyListing{.lobby_id = room.lobby_id(),
                                .mode_name = std::string{match.mode_name()},
                                .map_name = room.match_session().map_name(),
                                .phase = match.phase(),
                                .phase_started_tick = match.phase_started_tick().value(),
                                .tick_sequence = latest->tick_sequence().value(),
                                .seat_count = seats.seat_count(),
                                .seat_count_maximum = room.match_session().seat_count_maximum(),
                                .filled_seat_count = filled_seat_count,
                                .npc_seat_count = npc_seat_count,
                                .session_count = room.session_count(),
                                .healthy = room_is_serving(room)};
}

[[nodiscard]] bool target_selects_v3_envelope(const std::string_view target) noexcept {
  return target.starts_with(kProtocolV3TargetPrefix) ||
         target.starts_with(kRetiredProtocolTargetPrefix);
}

[[nodiscard]] std::string_view target_of(const GameApiHttpRequest& request) noexcept {
  return {request.target().data(), request.target().size()};
}

// The bounded diagnostic a subprotocol rejection carries. It names the route that was requested,
// not the token that was offered, so a client learns which pairing it violated without any byte of
// its own offer list reaching the response.
[[nodiscard]] std::string_view
subprotocol_required_reason(const GameApiUpgradeRoute route) noexcept {
  switch (route) {
  case GameApiUpgradeRoute::kSnapshotsV1:
    return "snapshot_subprotocol_required";
  case GameApiUpgradeRoute::kSessionV3:
    return "session_subprotocol_required";
  }
  return "subprotocol_required";
}

// The v1 `reason` detail that carries a forwarded-client rejection on a v1 target. It is derived
// from the same closed enum the v3 detail uses, so the two versions cannot describe one refusal
// differently and neither can carry an attacker-supplied byte.
[[nodiscard]] std::string
forwarded_client_reason_detail(const protocol::ForwardedClientReason reason) {
  return std::string{"forwarded_client_"}.append(protocol::forwarded_client_reason_name(reason));
}

[[nodiscard]] std::size_t header_count(const GameApiHttpRequest& request,
                                       const std::string_view name) {
  return request.base().count(name);
}

[[nodiscard]] std::size_t approximate_header_bytes(const GameApiHttpRequest& request) noexcept {
  std::size_t byte_count = request.method_string().size() + request.target().size() + 16;
  for (const auto& field : request.base()) {
    byte_count += field.name_string().size() + field.value().size() + 4;
  }
  return byte_count + 2;
}

[[nodiscard]] std::size_t header_field_count(const GameApiHttpRequest& request) noexcept {
  return static_cast<std::size_t>(std::distance(request.base().begin(), request.base().end()));
}

[[nodiscard]] std::optional<bool>
content_length_declares_body(std::string_view content_length) noexcept {
  while (!content_length.empty() &&
         (content_length.front() == ' ' || content_length.front() == '\t')) {
    content_length.remove_prefix(1);
  }
  while (!content_length.empty() &&
         (content_length.back() == ' ' || content_length.back() == '\t')) {
    content_length.remove_suffix(1);
  }
  if (content_length.empty()) {
    return std::nullopt;
  }

  bool nonzero_digit = false;
  for (const char character : content_length) {
    if (character < '0' || character > '9') {
      return std::nullopt;
    }
    nonzero_digit = nonzero_digit || character != '0';
  }
  return nonzero_digit;
}

[[nodiscard]] bool token_list_contains(const std::string_view value,
                                       const std::string_view expected,
                                       const bool case_sensitive) noexcept {
  std::size_t offset = 0;
  while (offset <= value.size()) {
    const std::size_t comma = value.find(',', offset);
    const std::size_t end = comma == std::string_view::npos ? value.size() : comma;
    std::string_view token = value.substr(offset, end - offset);
    while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) {
      token.remove_prefix(1);
    }
    while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
      token.remove_suffix(1);
    }
    const bool matches =
        case_sensitive ? token == expected : boost::beast::iequals(token, expected);
    if (matches) {
      return true;
    }
    if (comma == std::string_view::npos) {
      break;
    }
    offset = comma + 1;
  }
  return false;
}

[[nodiscard]] bool has_valid_websocket_key(const std::string_view key) noexcept {
  if (key.size() != 24 || key[22] != '=' || key[23] != '=') {
    return false;
  }
  const auto base64_value = [](const char character) noexcept -> std::optional<unsigned> {
    if (character >= 'A' && character <= 'Z') {
      return static_cast<unsigned>(character - 'A');
    }
    if (character >= 'a' && character <= 'z') {
      return static_cast<unsigned>(character - 'a') + 26U;
    }
    if (character >= '0' && character <= '9') {
      return static_cast<unsigned>(character - '0') + 52U;
    }
    if (character == '+') {
      return 62U;
    }
    if (character == '/') {
      return 63U;
    }
    return std::nullopt;
  };
  for (std::size_t index = 0; index < 22; ++index) {
    if (!base64_value(key[index]).has_value()) {
      return false;
    }
  }
  // Sixteen decoded bytes leave four zero padding bits in the final base64 symbol.
  return (*base64_value(key[21]) & 0x0FU) == 0U;
}

[[nodiscard]] bool is_websocket_attempt(const GameApiHttpRequest& request) {
  return websocket::is_upgrade(request) ||
         request.base().find(http::field::upgrade) != request.base().end() ||
         request.base().find(http::field::sec_websocket_key) != request.base().end() ||
         request.base().find(http::field::sec_websocket_version) != request.base().end() ||
         request.base().find(http::field::sec_websocket_protocol) != request.base().end();
}

[[nodiscard]] protocol::HttpError invalid_request_error(const std::string_view reason) {
  protocol::HttpError::Parameters parameters;
  parameters.reason = std::string{reason};
  return protocol::HttpError::create(protocol::HttpErrorCode::kInvalidRequest,
                                     "Request violates protocol v1.", std::move(parameters));
}

[[nodiscard]] protocol::HttpError rate_error(const AdmissionResult& admission) {
  protocol::HttpError::Parameters parameters;
  parameters.retry_after_ms = admission.retry_after_ms;
  if (admission.denial == AdmissionDenial::kConnectionLimit) {
    parameters.connection_limit = admission.connection_limit;
    return protocol::HttpError::create(protocol::HttpErrorCode::kConnectionLimitReached,
                                       "Connection capacity is exhausted.", std::move(parameters));
  }
  return protocol::HttpError::create(protocol::HttpErrorCode::kRateLimited,
                                     "Request rate limit is exhausted.", std::move(parameters));
}

[[nodiscard]] std::uint64_t retry_after_seconds(const std::uint64_t retry_after_ms) noexcept {
  return (retry_after_ms + 999U) / 1'000U;
}

void set_common_response_headers(GameApiHttpResponse& response,
                                 const protocol::RequestId& request_id,
                                 const std::optional<std::string_view> allowed_origin) {
  response.set(http::field::server, "blob-royale");
  response.set(http::field::content_type, "application/json; charset=utf-8");
  response.set(http::field::cache_control, "no-store");
  response.set("X-Content-Type-Options", "nosniff");
  response.set(kRequestIdHeader, request_id.value());
  if (allowed_origin.has_value()) {
    response.set(http::field::access_control_allow_origin, *allowed_origin);
    response.set(http::field::vary, "Origin");
  }
}

void set_error_specific_headers(GameApiHttpResponse& response, const protocol::HttpError& error) {
  switch (error.error_code()) {
  case protocol::HttpErrorCode::kMethodNotAllowed:
    response.set(http::field::allow, "GET");
    break;
  case protocol::HttpErrorCode::kUpgradeRequired:
    response.set(http::field::upgrade, "websocket");
    break;
  case protocol::HttpErrorCode::kWebsocketVersionUnsupported:
    response.set(http::field::sec_websocket_version, "13");
    break;
  case protocol::HttpErrorCode::kRateLimited:
  case protocol::HttpErrorCode::kConnectionLimitReached:
  case protocol::HttpErrorCode::kNotReady:
    response.set(http::field::retry_after,
                 std::to_string(retry_after_seconds(*error.retry_after_ms())));
    break;
  default:
    break;
  }
}

[[nodiscard]] protocol::RequestId request_id_or_generated(const GameApiHttpRequest& request,
                                                          RequestIdGenerator& generator,
                                                          bool& request_id_valid) {
  const std::size_t count = header_count(request, kRequestIdHeader);
  if (count == 0) {
    request_id_valid = true;
    return generator.next();
  }
  if (count != 1) {
    request_id_valid = false;
    return generator.next();
  }
  try {
    request_id_valid = true;
    return protocol::decode_request_id(request.base().at(kRequestIdHeader));
  } catch (const protocol::ProtocolEncodingError&) {
    request_id_valid = false;
    return generator.next();
  }
}

} // namespace

GameApiRouteResult GameApiRouteResult::http_response(GameApiHttpResponse response,
                                                     protocol::RequestId request_id) {
  return {GameApiRouteDisposition::kHttpResponse,
          std::move(response),
          std::move(request_id),
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt};
}

GameApiRouteResult GameApiRouteResult::websocket_upgrade(protocol::RequestId request_id,
                                                         WebSocketAdmissionLease websocket_lease,
                                                         const GameApiUpgradeRoute upgrade_route,
                                                         PeerIdentity peer_identity,
                                                         const std::uint64_t lobby_id) {
  return {GameApiRouteDisposition::kWebSocketUpgrade,
          std::nullopt,
          std::move(request_id),
          std::move(websocket_lease),
          upgrade_route,
          std::move(peer_identity),
          lobby_id};
}

GameApiRouteResult GameApiRouteResult::close_without_response() {
  return {GameApiRouteDisposition::kCloseWithoutResponse,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt};
}

GameApiRouteResult::GameApiRouteResult(const GameApiRouteDisposition disposition,
                                       std::optional<GameApiHttpResponse> response,
                                       std::optional<protocol::RequestId> request_id,
                                       std::optional<WebSocketAdmissionLease> websocket_lease,
                                       std::optional<GameApiUpgradeRoute> upgrade_route,
                                       std::optional<PeerIdentity> peer_identity,
                                       std::optional<std::uint64_t> lobby_id) noexcept
    : disposition_(disposition), response_(std::move(response)), request_id_(std::move(request_id)),
      websocket_lease_(std::move(websocket_lease)), upgrade_route_(upgrade_route),
      peer_identity_(std::move(peer_identity)), lobby_id_(lobby_id) {}

GameApiHttpResponse GameApiRouteResult::take_response() {
  if (!response_.has_value()) {
    throw std::logic_error{"route result does not contain an HTTP response"};
  }
  GameApiHttpResponse response = std::move(*response_);
  response_.reset();
  return response;
}

WebSocketAdmissionLease GameApiRouteResult::take_websocket_lease() {
  if (!websocket_lease_.has_value()) {
    throw std::logic_error{"route result does not contain a WebSocket reservation"};
  }
  WebSocketAdmissionLease lease = std::move(*websocket_lease_);
  websocket_lease_.reset();
  return lease;
}

const protocol::RequestId& GameApiRouteResult::request_id() const& {
  if (!request_id_.has_value()) {
    throw std::logic_error{"route result does not contain a request ID"};
  }
  return *request_id_;
}

GameApiUpgradeRoute GameApiRouteResult::upgrade_route() const {
  if (!upgrade_route_.has_value()) {
    throw std::logic_error{"route result does not contain a WebSocket upgrade route"};
  }
  return *upgrade_route_;
}

const PeerIdentity& GameApiRouteResult::peer_identity() const& {
  if (!peer_identity_.has_value()) {
    throw std::logic_error{"route result does not contain a derived peer identity"};
  }
  return *peer_identity_;
}

std::uint64_t GameApiRouteResult::lobby_id() const {
  if (!lobby_id_.has_value()) {
    throw std::logic_error{"route result does not contain an admitted room"};
  }
  return *lobby_id_;
}

GameApiRouter::GameApiRouter(const ServerConfig& server_config, const LobbyDirectory& lobbies,
                             PeerTrafficPolicy& peer_traffic_policy,
                             RequestIdGenerator& request_id_generator) noexcept
    : server_config_(server_config), lobbies_(lobbies), peer_traffic_policy_(peer_traffic_policy),
      request_id_generator_(request_id_generator) {}

GameApiRouteResult GameApiRouter::route(const GameApiHttpRequest& request,
                                        const std::string_view peer_address,
                                        const PeerTrafficPolicy::Clock::time_point now) {
  if (request.target().size() > ServerLimits::kRequestTargetMaximumByteCount) {
    static_cast<void>(peer_traffic_policy_.consume_http_request(peer_address, now));
    return GameApiRouteResult::close_without_response();
  }

  // Identity before any route runs. The classification is computed from the socket alone, so no
  // header can move a connection into the trusted arm, and the derived principal -- not the socket
  // address -- is what every request, upgrade, and connection bound below is charged to
  // (`docs/protocol/v3.md` § "Identity"). A rejected forwarded address has no principal to charge,
  // so its refusal is accounted to the socket peer, which is the only identity still provable.
  const PeerIdentityResolution identity =
      derive_peer_identity(server_config_, peer_address, request);
  const std::string accounting_principal =
      identity.accepted() ? identity.identity->accounting_principal() : std::string{peer_address};

  bool request_id_valid = false;
  const protocol::RequestId request_id =
      request_id_or_generated(request, request_id_generator_, request_id_valid);
  const AdmissionResult request_admission =
      peer_traffic_policy_.consume_http_request(accounting_principal, now);
  if (!request_admission.allowed) {
    return error_response(request, request_id, rate_error(request_admission));
  }
  if (request.version() != 11) {
    GameApiHttpResponse response{http::status::http_version_not_supported, 11};
    response.set(http::field::server, "blob-royale");
    response.set("X-Request-ID", request_id.value());
    response.set(http::field::connection, "close");
    response.content_length(0);
    response.keep_alive(false);
    return GameApiRouteResult::http_response(std::move(response), request_id);
  }
  if (!request_id_valid) {
    return error_response(request, request_id,
                          protocol::HttpError::create(protocol::HttpErrorCode::kInvalidRequestId,
                                                      "X-Request-ID is invalid."));
  }
  if (!identity.accepted()) {
    return forwarded_client_error_response(request, request_id,
                                           *identity.forwarded_client_rejection);
  }

  if (approximate_header_bytes(request) > ServerLimits::kHeaderSectionMaximumByteCount ||
      header_field_count(request) > ServerLimits::kHeaderFieldMaximumCount) {
    return error_response(
        request, request_id,
        protocol::HttpError::create(protocol::HttpErrorCode::kHeaderTooLarge,
                                    "Request headers exceed the accepted bound."));
  }

  if (header_count(request, "Host") != 1) {
    return error_response(request, request_id, invalid_request_error("host_required"));
  }
  const std::string normalized_host =
      normalize_host_authority(request.base().at("Host"), server_config_.port());
  if (normalized_host.empty() || !server_config_.allows_host(normalized_host)) {
    return error_response(request, request_id, invalid_request_error("host_rejected"));
  }

  if (header_count(request, "Origin") > 1 || header_count(request, "Content-Length") > 1 ||
      header_count(request, "Transfer-Encoding") > 1) {
    return error_response(request, request_id, invalid_request_error("duplicate_singleton_header"));
  }
  const bool has_content_length =
      request.base().find(http::field::content_length) != request.base().end();
  const bool has_transfer_encoding =
      request.base().find(http::field::transfer_encoding) != request.base().end();
  if (has_content_length && has_transfer_encoding) {
    return error_response(request, request_id,
                          invalid_request_error("conflicting_message_framing"));
  }
  bool declared_body = false;
  if (has_content_length) {
    const std::optional<bool> parsed_content_length =
        content_length_declares_body(request.base().at(http::field::content_length));
    if (!parsed_content_length.has_value()) {
      return error_response(request, request_id, invalid_request_error("content_length_invalid"));
    }
    declared_body = *parsed_content_length;
  }
  if (!request.body().empty() || has_transfer_encoding || declared_body ||
      (request.payload_size().has_value() && *request.payload_size() > 0)) {
    return error_response(request, request_id,
                          protocol::HttpError::create(protocol::HttpErrorCode::kPayloadTooLarge,
                                                      "Request bodies are forbidden."));
  }

  std::optional<std::string> allowed_origin;
  if (request.base().find(http::field::origin) != request.base().end()) {
    const std::string normalized_origin =
        normalize_serialized_origin(request.base().at(http::field::origin));
    if (normalized_origin.empty() || !server_config_.allows_origin(normalized_origin)) {
      return error_response(request, request_id,
                            protocol::HttpError::create(protocol::HttpErrorCode::kOriginRejected,
                                                        "Origin is not allowed."));
    }
    allowed_origin = std::string{request.base().at(http::field::origin)};
  }

  const std::string_view target = target_of(request);
  const ResolvedTarget resolved = resolve_target(target, lobbies_);
  if (resolved.kind == TargetKind::kRetiredSessionVersion) {
    return v3_error_response(request, request_id,
                             protocol::V3HttpError::session_version_upgrade_required(),
                             allowed_origin);
  }
  if (resolved.kind == TargetKind::kUnknown) {
    return error_response(request, request_id,
                          protocol::HttpError::create(protocol::HttpErrorCode::kRouteNotFound,
                                                      target_selects_v3_envelope(target)
                                                          ? "Route is not part of protocol v3."
                                                          : "Route is not part of protocol v1."),
                          allowed_origin);
  }
  if (resolved.kind == TargetKind::kLobbyNotFound) {
    return v3_error_response(request, request_id, protocol::V3HttpError::lobby_not_found(),
                             allowed_origin);
  }
  if (request.method() != http::verb::get) {
    return error_response(request, request_id,
                          protocol::HttpError::create(protocol::HttpErrorCode::kMethodNotAllowed,
                                                      "Only GET is allowed for this route."),
                          allowed_origin);
  }

  if (resolved.kind == TargetKind::kConfig) {
    GameApiHttpResponse response{http::status::ok, 11};
    response.body() =
        protocol::encode_configuration_response(server_config_.public_configuration(), request_id);
    set_common_response_headers(response, request_id, allowed_origin);
    response.keep_alive(request.keep_alive());
    response.prepare_payload();
    return GameApiRouteResult::http_response(std::move(response), request_id);
  }
  if (resolved.kind == TargetKind::kLiveness) {
    GameApiHttpResponse response{http::status::ok, 11};
    response.body() = protocol::encode_liveness_response(request_id);
    set_common_response_headers(response, request_id, allowed_origin);
    response.keep_alive(request.keep_alive());
    response.prepare_payload();
    return GameApiRouteResult::http_response(std::move(response), request_id);
  }
  if (resolved.kind == TargetKind::kReadiness) {
    if (!room_is_serving(lobbies_.room(kRoomOne))) {
      protocol::HttpError::Parameters parameters;
      parameters.retry_after_ms = 1'000;
      return error_response(request, request_id,
                            protocol::HttpError::create(protocol::HttpErrorCode::kNotReady,
                                                        "Snapshot publication is not ready.",
                                                        std::move(parameters)),
                            allowed_origin);
    }
    GameApiHttpResponse response{http::status::ok, 11};
    response.body() = protocol::encode_readiness_response(request_id);
    set_common_response_headers(response, request_id, allowed_origin);
    response.keep_alive(request.keep_alive());
    response.prepare_payload();
    return GameApiRouteResult::http_response(std::move(response), request_id);
  }
  if (resolved.kind == TargetKind::kLobbyDirectory) {
    std::vector<protocol::LobbyListing> listings;
    listings.reserve(lobbies_.size());
    for (std::size_t index = 0; index < lobbies_.size(); ++index) {
      listings.push_back(lobby_listing_of(lobbies_.at(index)));
    }
    GameApiHttpResponse response{http::status::ok, 11};
    response.body() = protocol::encode_lobby_directory_message(listings, request_id);
    set_common_response_headers(response, request_id, allowed_origin);
    response.keep_alive(request.keep_alive());
    response.prepare_payload();
    return GameApiRouteResult::http_response(std::move(response), request_id);
  }

  // The one remaining shape is an upgrade route, and which one is a function of the target alone.
  const GameApiUpgradeRoute upgrade_route = resolved.kind == TargetKind::kSnapshotsV1Upgrade
                                                ? GameApiUpgradeRoute::kSnapshotsV1
                                                : GameApiUpgradeRoute::kSessionV3;

  if (!is_websocket_attempt(request)) {
    return error_response(request, request_id,
                          protocol::HttpError::create(protocol::HttpErrorCode::kUpgradeRequired,
                                                      "A WebSocket upgrade is required."),
                          allowed_origin);
  }
  const AdmissionResult upgrade_rate =
      peer_traffic_policy_.consume_websocket_upgrade(accounting_principal, now);
  if (!upgrade_rate.allowed) {
    return error_response(request, request_id, rate_error(upgrade_rate), allowed_origin);
  }

  if (header_count(request, "Sec-WebSocket-Version") != 1 ||
      request.base().at(http::field::sec_websocket_version) != "13") {
    return error_response(
        request, request_id,
        protocol::HttpError::create(protocol::HttpErrorCode::kWebsocketVersionUnsupported,
                                    "WebSocket version 13 is required."),
        allowed_origin);
  }
  if (header_count(request, "Connection") != 1 || header_count(request, "Upgrade") != 1 ||
      header_count(request, "Sec-WebSocket-Key") != 1 ||
      !token_list_contains(request.base().at(http::field::connection), "upgrade", false) ||
      !boost::beast::iequals(request.base().at(http::field::upgrade), "websocket") ||
      !has_valid_websocket_key(request.base().at(http::field::sec_websocket_key)) ||
      !websocket::is_upgrade(request)) {
    return error_response(request, request_id, invalid_request_error("websocket_handshake_invalid"),
                          allowed_origin);
  }

  // Route and subprotocol are validated as a pair: only the requested route's own token is looked
  // for, so an offer list carrying the other route's token is refused exactly as an empty one is.
  const std::string_view required_subprotocol = game_api_upgrade_subprotocol(upgrade_route);
  bool offered_subprotocol = false;
  const auto protocol_range = request.base().equal_range(http::field::sec_websocket_protocol);
  for (auto entry = protocol_range.first; entry != protocol_range.second; ++entry) {
    if (token_list_contains(entry->value(), required_subprotocol, true)) {
      offered_subprotocol = true;
      break;
    }
  }
  if (!offered_subprotocol) {
    protocol::HttpError::Parameters parameters;
    parameters.reason = std::string{subprotocol_required_reason(upgrade_route)};
    return error_response(request, request_id,
                          protocol::HttpError::create(protocol::HttpErrorCode::kSubprotocolRequired,
                                                      "This route's subprotocol is required.",
                                                      std::move(parameters)),
                          allowed_origin);
  }

  // Browser-origin policy, under the classification derived above. The direct-peer relaxation is
  // reached only through `is_direct_peer()`, so a peer configured as a trusted proxy never
  // inherits it -- which is the whole of v3's precedence rule, and it matters because on the
  // accepted deployment the proxy *is* loopback.
  const bool peer_is_loopback = [&] {
    boost::system::error_code error;
    const auto address = boost::asio::ip::make_address(peer_address, error);
    return !error && address.is_loopback();
  }();
  const bool direct_peer = identity.identity->is_direct_peer();
  if ((direct_peer && !peer_is_loopback) || (!direct_peer && !allowed_origin.has_value())) {
    return error_response(request, request_id,
                          protocol::HttpError::create(protocol::HttpErrorCode::kOriginRejected,
                                                      "Origin is required for this peer."));
  }
  // Admission, per room, after every upgrade rule and before any capacity is reserved for the
  // socket. The v1 stream keeps v1's answer for an unready room 1; a v3 session is refused in the
  // room's own terms, naming the room, so a client can go back to the directory with the number.
  const LobbyEntry& room = lobbies_.room(resolved.lobby_id);
  if (!room_is_serving(room)) {
    if (upgrade_route == GameApiUpgradeRoute::kSnapshotsV1) {
      protocol::HttpError::Parameters parameters;
      parameters.retry_after_ms = 1'000;
      return error_response(request, request_id,
                            protocol::HttpError::create(protocol::HttpErrorCode::kNotReady,
                                                        "Snapshot publication is not ready.",
                                                        std::move(parameters)),
                            allowed_origin);
    }
    return v3_error_response(request, request_id,
                             protocol::V3HttpError::lobby_unavailable(room.lobby_id()),
                             allowed_origin);
  }
  if (upgrade_route == GameApiUpgradeRoute::kSessionV3 && room_is_full(room)) {
    return v3_error_response(request, request_id,
                             protocol::V3HttpError::lobby_full(room.lobby_id()), allowed_origin);
  }

  WebSocketReservationResult reservation =
      peer_traffic_policy_.reserve_websocket(accounting_principal, now);
  if (!reservation.admission.allowed) {
    return error_response(request, request_id, rate_error(reservation.admission), allowed_origin);
  }
  return GameApiRouteResult::websocket_upgrade(request_id, std::move(*reservation.lease),
                                               upgrade_route, *identity.identity, room.lobby_id());
}

GameApiRouteResult
GameApiRouter::error_response(const GameApiHttpRequest& request,
                              const protocol::RequestId& request_id, protocol::HttpError error,
                              const std::optional<std::string_view> allowed_origin) const {
  GameApiHttpResponse response{static_cast<http::status>(error.status_code()), 11};
  response.body() =
      target_selects_v3_envelope(target_of(request))
          ? protocol::encode_error_response_v3(protocol::V3HttpError::shared(error), request_id)
          : protocol::encode_error_response(error, request_id);
  set_common_response_headers(response, request_id, allowed_origin);
  set_error_specific_headers(response, error);
  response.keep_alive(request.keep_alive());
  response.prepare_payload();
  return GameApiRouteResult::http_response(std::move(response), request_id);
}

GameApiRouteResult
GameApiRouter::forwarded_client_error_response(const GameApiHttpRequest& request,
                                               const protocol::RequestId& request_id,
                                               const protocol::ForwardedClientReason reason) const {
  if (!target_selects_v3_envelope(target_of(request))) {
    return error_response(request, request_id,
                          invalid_request_error(forwarded_client_reason_detail(reason)));
  }
  return v3_error_response(request, request_id,
                           protocol::V3HttpError::invalid_forwarded_client(reason), std::nullopt);
}

GameApiRouteResult
GameApiRouter::v3_error_response(const GameApiHttpRequest& request,
                                 const protocol::RequestId& request_id,
                                 const protocol::V3HttpError& error,
                                 const std::optional<std::string_view> allowed_origin) const {
  GameApiHttpResponse response{static_cast<http::status>(error.status_code()), 11};
  response.body() = protocol::encode_error_response_v3(error, request_id);
  set_common_response_headers(response, request_id, allowed_origin);
  if (error.lobby_error() == protocol::LobbyErrorKind::kUnavailable) {
    response.set(http::field::retry_after, "1");
  }
  response.keep_alive(request.keep_alive());
  if (error.requires_session_version_upgrade()) {
    // RFC 9110 sections 7.8/15.5.22 require Upgrade plus its hop-by-hop Connection option on
    // this 426. The fixed JSON detail selects the application major; no offer is reflected.
    response.set(http::field::upgrade, "websocket");
    response.set(http::field::connection, request.keep_alive() ? "Upgrade" : "close, Upgrade");
  }
  response.prepare_payload();
  return GameApiRouteResult::http_response(std::move(response), request_id);
}

} // namespace blob_royale::server
