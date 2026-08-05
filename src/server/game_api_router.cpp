#include "game_api_router.hpp"

#include "http_error.hpp"
#include "protocol_constants.hpp"
#include "protocol_encoding_error.hpp"
#include "protocol_json_encoding.hpp"
#include "server_limits.hpp"

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
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace blob_royale::server {
namespace {

namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;

inline constexpr std::string_view kConfigTarget = "/api/v1/config";
inline constexpr std::string_view kLivenessTarget = "/api/v1/health/live";
inline constexpr std::string_view kReadinessTarget = "/api/v1/health/ready";
inline constexpr std::string_view kSnapshotsTarget = "/api/v1/snapshots";
inline constexpr std::string_view kSnapshotSubprotocol = "blob-royale.snapshot.v1";
inline constexpr std::string_view kRequestIdHeader = "X-Request-ID";

[[nodiscard]] bool is_known_target(const std::string_view target) noexcept {
  return target == kConfigTarget || target == kLivenessTarget || target == kReadinessTarget ||
         target == kSnapshotsTarget;
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
  return {GameApiRouteDisposition::kHttpResponse, std::move(response), std::move(request_id),
          std::nullopt};
}

GameApiRouteResult GameApiRouteResult::websocket_upgrade(protocol::RequestId request_id,
                                                         WebSocketAdmissionLease websocket_lease) {
  return {GameApiRouteDisposition::kWebSocketUpgrade, std::nullopt, std::move(request_id),
          std::move(websocket_lease)};
}

GameApiRouteResult GameApiRouteResult::close_without_response() {
  return {GameApiRouteDisposition::kCloseWithoutResponse, std::nullopt, std::nullopt, std::nullopt};
}

GameApiRouteResult::GameApiRouteResult(
    const GameApiRouteDisposition disposition, std::optional<GameApiHttpResponse> response,
    std::optional<protocol::RequestId> request_id,
    std::optional<WebSocketAdmissionLease> websocket_lease) noexcept
    : disposition_(disposition), response_(std::move(response)), request_id_(std::move(request_id)),
      websocket_lease_(std::move(websocket_lease)) {}

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

GameApiRouter::GameApiRouter(const ServerConfig& server_config,
                             const runtime::SnapshotPublication& snapshot_publication,
                             PeerTrafficPolicy& peer_traffic_policy,
                             RequestIdGenerator& request_id_generator) noexcept
    : server_config_(server_config), snapshot_publication_(snapshot_publication),
      peer_traffic_policy_(peer_traffic_policy), request_id_generator_(request_id_generator) {}

GameApiRouteResult GameApiRouter::route(const GameApiHttpRequest& request,
                                        const std::string_view peer_address,
                                        const PeerTrafficPolicy::Clock::time_point now) {
  if (request.target().size() > ServerLimits::kRequestTargetMaximumByteCount) {
    static_cast<void>(peer_traffic_policy_.consume_http_request(peer_address, now));
    return GameApiRouteResult::close_without_response();
  }

  bool request_id_valid = false;
  const protocol::RequestId request_id =
      request_id_or_generated(request, request_id_generator_, request_id_valid);
  const AdmissionResult request_admission =
      peer_traffic_policy_.consume_http_request(peer_address, now);
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

  const std::string_view target{request.target().data(), request.target().size()};
  if (!is_known_target(target)) {
    return error_response(request, request_id,
                          protocol::HttpError::create(protocol::HttpErrorCode::kRouteNotFound,
                                                      "Route is not part of protocol v1."),
                          allowed_origin);
  }
  if (request.method() != http::verb::get) {
    return error_response(request, request_id,
                          protocol::HttpError::create(protocol::HttpErrorCode::kMethodNotAllowed,
                                                      "Only GET is allowed for this route."),
                          allowed_origin);
  }

  if (target == kConfigTarget) {
    GameApiHttpResponse response{http::status::ok, 11};
    response.body() =
        protocol::encode_configuration_response(server_config_.public_configuration(), request_id);
    set_common_response_headers(response, request_id, allowed_origin);
    response.keep_alive(request.keep_alive());
    response.prepare_payload();
    return GameApiRouteResult::http_response(std::move(response), request_id);
  }
  if (target == kLivenessTarget) {
    GameApiHttpResponse response{http::status::ok, 11};
    response.body() = protocol::encode_liveness_response(request_id);
    set_common_response_headers(response, request_id, allowed_origin);
    response.keep_alive(request.keep_alive());
    response.prepare_payload();
    return GameApiRouteResult::http_response(std::move(response), request_id);
  }
  if (target == kReadinessTarget) {
    if (!snapshot_publication_.is_ready() ||
        snapshot_publication_.latest()->tick_sequence().value() == 0) {
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

  if (!is_websocket_attempt(request)) {
    return error_response(request, request_id,
                          protocol::HttpError::create(protocol::HttpErrorCode::kUpgradeRequired,
                                                      "A WebSocket upgrade is required."),
                          allowed_origin);
  }
  const AdmissionResult upgrade_rate =
      peer_traffic_policy_.consume_websocket_upgrade(peer_address, now);
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

  bool offered_subprotocol = false;
  const auto protocol_range = request.base().equal_range(http::field::sec_websocket_protocol);
  for (auto entry = protocol_range.first; entry != protocol_range.second; ++entry) {
    if (token_list_contains(entry->value(), kSnapshotSubprotocol, true)) {
      offered_subprotocol = true;
      break;
    }
  }
  if (!offered_subprotocol) {
    protocol::HttpError::Parameters parameters;
    parameters.reason = "snapshot_subprotocol_required";
    return error_response(request, request_id,
                          protocol::HttpError::create(protocol::HttpErrorCode::kSubprotocolRequired,
                                                      "Snapshot subprotocol is required.",
                                                      std::move(parameters)),
                          allowed_origin);
  }

  const bool peer_is_loopback = [&] {
    boost::system::error_code error;
    const auto address = boost::asio::ip::make_address(peer_address, error);
    return !error && address.is_loopback();
  }();
  const bool peer_is_trusted_proxy = server_config_.trusts_proxy_address(peer_address);
  if ((!peer_is_loopback && !peer_is_trusted_proxy) ||
      (peer_is_trusted_proxy && !allowed_origin.has_value())) {
    return error_response(request, request_id,
                          protocol::HttpError::create(protocol::HttpErrorCode::kOriginRejected,
                                                      "Origin is required for this peer."));
  }
  if (!snapshot_publication_.is_ready() ||
      snapshot_publication_.latest()->tick_sequence().value() == 0) {
    protocol::HttpError::Parameters parameters;
    parameters.retry_after_ms = 1'000;
    return error_response(request, request_id,
                          protocol::HttpError::create(protocol::HttpErrorCode::kNotReady,
                                                      "Snapshot publication is not ready.",
                                                      std::move(parameters)),
                          allowed_origin);
  }

  WebSocketReservationResult reservation =
      peer_traffic_policy_.reserve_websocket(peer_address, now);
  if (!reservation.admission.allowed) {
    return error_response(request, request_id, rate_error(reservation.admission), allowed_origin);
  }
  return GameApiRouteResult::websocket_upgrade(request_id, std::move(*reservation.lease));
}

GameApiRouteResult
GameApiRouter::error_response(const GameApiHttpRequest& request,
                              const protocol::RequestId& request_id, protocol::HttpError error,
                              const std::optional<std::string_view> allowed_origin) const {
  GameApiHttpResponse response{static_cast<http::status>(error.status_code()), 11};
  response.body() = protocol::encode_error_response(error, request_id);
  set_common_response_headers(response, request_id, allowed_origin);
  set_error_specific_headers(response, error);
  response.keep_alive(request.keep_alive());
  response.prepare_payload();
  return GameApiRouteResult::http_response(std::move(response), request_id);
}

} // namespace blob_royale::server
