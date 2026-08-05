#include "protocol_contract_validation.hpp"

#include "integration_test_error.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace blob_royale::integration_test {
namespace {

namespace http = boost::beast::http;

constexpr std::uint64_t kMaximumSafeInteger = 9'007'199'254'740'991;
constexpr std::size_t kMaximumHttpResponseBytes = 65'536;
constexpr std::size_t kMaximumSnapshotFrameBytes = 2'097'152;
constexpr std::string_view kProtocolVersion = "1.0";
constexpr std::string_view kConfigurationSchema =
    "blob-royale://protocol/v1/configuration-response";
constexpr std::string_view kReadinessSchema = "blob-royale://protocol/v1/readiness-response";
constexpr std::string_view kErrorSchema = "blob-royale://protocol/v1/error-response";
constexpr std::string_view kSnapshotSchema = "blob-royale://protocol/v1/snapshot-message";
constexpr std::string_view kSnapshotSubprotocol = "blob-royale.snapshot.v1";

[[noreturn]] void contract_failure(const std::string_view operation,
                                   const std::string_view context) {
  throw IntegrationTestError{IntegrationTestErrorCode::kContractViolation, std::string{operation},
                             std::string{context}};
}

[[nodiscard]] boost::json::value parse_json(const std::string_view encoded,
                                            const std::size_t maximum_byte_count,
                                            const std::string_view operation) {
  if (encoded.size() > maximum_byte_count) {
    contract_failure(operation, "encoded JSON exceeds the protocol byte bound");
  }
  try {
    return boost::json::parse(boost::json::string_view{encoded.data(), encoded.size()});
  } catch (const std::exception&) {
    contract_failure(operation, "body is not one complete JSON document");
  }
}

[[nodiscard]] const boost::json::object& require_object(const boost::json::value& value,
                                                        const std::string_view context) {
  if (!value.is_object()) {
    contract_failure("protocol.validate_json", context);
  }
  return value.as_object();
}

void require_exact_members(const boost::json::object& object,
                           const std::initializer_list<std::string_view> members,
                           const std::string_view context) {
  if (object.size() != members.size()) {
    contract_failure("protocol.validate_json", context);
  }
  for (const std::string_view member : members) {
    if (!object.contains(boost::json::string_view{member.data(), member.size()})) {
      contract_failure("protocol.validate_json", context);
    }
  }
}

[[nodiscard]] const boost::json::value& require_member(const boost::json::object& object,
                                                       const std::string_view member,
                                                       const std::string_view context) {
  const auto entry = object.find(boost::json::string_view{member.data(), member.size()});
  if (entry == object.end()) {
    contract_failure("protocol.validate_json", context);
  }
  return entry->value();
}

[[nodiscard]] std::string_view require_string(const boost::json::value& value,
                                              const std::string_view context) {
  if (!value.is_string()) {
    contract_failure("protocol.validate_json", context);
  }
  const boost::json::string& string = value.as_string();
  return {string.data(), string.size()};
}

[[nodiscard]] std::uint64_t require_unsigned_integer(const boost::json::value& value,
                                                     const std::uint64_t minimum,
                                                     const std::uint64_t maximum,
                                                     const std::string_view context) {
  std::uint64_t integer = 0;
  if (value.is_uint64()) {
    integer = value.as_uint64();
  } else if (value.is_int64() && value.as_int64() >= 0) {
    integer = static_cast<std::uint64_t>(value.as_int64());
  } else {
    contract_failure("protocol.validate_json", context);
  }
  if (integer < minimum || integer > maximum) {
    contract_failure("protocol.validate_json", context);
  }
  return integer;
}

[[nodiscard]] double require_number(const boost::json::value& value,
                                    const std::string_view context) {
  double number = 0.0;
  if (value.is_double()) {
    number = value.as_double();
  } else if (value.is_int64()) {
    number = static_cast<double>(value.as_int64());
  } else if (value.is_uint64()) {
    number = static_cast<double>(value.as_uint64());
  } else {
    contract_failure("protocol.validate_json", context);
  }
  if (!std::isfinite(number) || std::abs(number) > 1'000'000'000'000.0 ||
      (number == 0.0 && std::signbit(number))) {
    contract_failure("protocol.validate_json", context);
  }
  return number;
}

void require_exact_string(const boost::json::value& value, const std::string_view expected,
                          const std::string_view context) {
  if (require_string(value, context) != expected) {
    contract_failure("protocol.validate_json", context);
  }
}

void validate_request_id(const std::string_view request_id) {
  if (request_id.empty() || request_id.size() > 64) {
    contract_failure("protocol.validate_request_id", "request ID length is invalid");
  }
  for (const char character : request_id) {
    const bool accepted = (character >= 'A' && character <= 'Z') ||
                          (character >= 'a' && character <= 'z') ||
                          (character >= '0' && character <= '9') || character == '.' ||
                          character == '_' || character == ':' || character == '-';
    if (!accepted) {
      contract_failure("protocol.validate_request_id", "request ID character is invalid");
    }
  }
}

void validate_response_headers(const IntegrationHttpResponse& response,
                               const std::string_view expected_request_id,
                               const std::optional<std::string_view> expected_allowed_origin) {
  if (response[http::field::content_type] != "application/json; charset=utf-8" ||
      response[http::field::cache_control] != "no-store" ||
      response["X-Content-Type-Options"] != "nosniff" ||
      response["X-Request-ID"] != expected_request_id) {
    contract_failure("protocol.validate_http_headers",
                     "required protocol response header is absent or invalid");
  }
  if (expected_allowed_origin.has_value()) {
    if (response[http::field::access_control_allow_origin] != *expected_allowed_origin ||
        response[http::field::vary] != "Origin") {
      contract_failure("protocol.validate_http_headers",
                       "accepted Origin was not reflected exactly");
    }
  } else if (!response[http::field::access_control_allow_origin].empty()) {
    contract_failure("protocol.validate_http_headers",
                     "response unexpectedly permits a cross-origin caller");
  }
}

[[nodiscard]] const boost::json::object&
validate_envelope(const boost::json::value& document, const std::string_view expected_schema,
                  const std::string_view expected_request_id) {
  const boost::json::object& envelope = require_object(document, "envelope must be an object");
  require_exact_members(envelope, {"data", "error", "meta"}, "envelope member set is invalid");
  const boost::json::object& metadata =
      require_object(require_member(envelope, "meta", "meta is missing"), "meta must be an object");
  const bool snapshot_metadata = expected_schema == kSnapshotSchema;
  if (snapshot_metadata) {
    require_exact_members(
        metadata,
        {"protocol_version", "schema_id", "request_id", "message_sequence", "sent_at_utc"},
        "snapshot meta member set is invalid");
  } else {
    require_exact_members(metadata, {"protocol_version", "schema_id", "request_id"},
                          "HTTP meta member set is invalid");
  }
  require_exact_string(require_member(metadata, "protocol_version", "protocol version missing"),
                       kProtocolVersion, "protocol version is invalid");
  require_exact_string(require_member(metadata, "schema_id", "schema ID missing"), expected_schema,
                       "schema ID is invalid");
  require_exact_string(require_member(metadata, "request_id", "request ID missing"),
                       expected_request_id, "request ID does not match transport metadata");
  validate_request_id(expected_request_id);
  return envelope;
}

void require_exact_number(const boost::json::object& object, const std::string_view member,
                          const double expected, const std::string_view context) {
  if (require_number(require_member(object, member, context), context) != expected) {
    contract_failure("protocol.validate_json", context);
  }
}

void validate_configuration_data(const boost::json::value& value) {
  const boost::json::object& configuration =
      require_object(value, "configuration data must be an object");
  require_exact_members(configuration, {"world", "simulation", "presentation"},
                        "configuration member set is invalid");

  const boost::json::object& world = require_object(
      require_member(configuration, "world", "world is missing"), "world must be an object");
  require_exact_members(world,
                        {"width_world_units", "height_world_units", "player_radius_world_units"},
                        "world member set is invalid");
  require_exact_number(world, "width_world_units", 100.0, "world width is invalid");
  require_exact_number(world, "height_world_units", 80.0, "world height is invalid");
  require_exact_number(world, "player_radius_world_units", 2.0, "player radius is invalid");

  const boost::json::object& simulation =
      require_object(require_member(configuration, "simulation", "simulation is missing"),
                     "simulation must be an object");
  require_exact_members(simulation, {"ticks_per_second", "fixed_delta_seconds"},
                        "simulation member set is invalid");
  if (require_unsigned_integer(require_member(simulation, "ticks_per_second", "tick rate missing"),
                               400, 400, "tick rate is invalid") != 400) {
    contract_failure("protocol.validate_configuration", "tick rate is invalid");
  }
  require_exact_number(simulation, "fixed_delta_seconds", 0.0025, "fixed delta is invalid");

  const boost::json::object& presentation =
      require_object(require_member(configuration, "presentation", "presentation is missing"),
                     "presentation must be an object");
  require_exact_members(
      presentation, {"snapshots_per_second", "snapshot_player_limit", "snapshot_frame_max_bytes"},
      "presentation member set is invalid");
  static_cast<void>(require_unsigned_integer(
      require_member(presentation, "snapshots_per_second", "snapshot cadence missing"), 20, 20,
      "snapshot cadence is invalid"));
  static_cast<void>(require_unsigned_integer(
      require_member(presentation, "snapshot_player_limit", "player limit missing"), 4'096, 4'096,
      "snapshot player limit is invalid"));
  static_cast<void>(require_unsigned_integer(
      require_member(presentation, "snapshot_frame_max_bytes", "frame limit missing"),
      kMaximumSnapshotFrameBytes, kMaximumSnapshotFrameBytes, "snapshot frame limit is invalid"));
}

[[nodiscard]] bool is_ascii_digit(const char character) noexcept {
  return character >= '0' && character <= '9';
}

void validate_utc_timestamp(const std::string_view timestamp) {
  if (timestamp.size() < 20 || timestamp.size() > 32 || timestamp[4] != '-' ||
      timestamp[7] != '-' || timestamp[10] != 'T' || timestamp[13] != ':' || timestamp[16] != ':' ||
      timestamp.back() != 'Z') {
    contract_failure("protocol.validate_snapshot", "sent_at_utc shape is invalid");
  }
  constexpr std::array<std::size_t, 14> kDigitPositions = {0, 1,  2,  3,  5,  6,  8,
                                                           9, 11, 12, 14, 15, 17, 18};
  for (const std::size_t position : kDigitPositions) {
    if (!is_ascii_digit(timestamp[position])) {
      contract_failure("protocol.validate_snapshot", "sent_at_utc contains a non-digit");
    }
  }
  if (timestamp.size() > 20) {
    if (timestamp[19] != '.' || timestamp.size() < 22) {
      contract_failure("protocol.validate_snapshot", "sent_at_utc fraction is invalid");
    }
    for (std::size_t position = 20; position + 1 < timestamp.size(); ++position) {
      if (!is_ascii_digit(timestamp[position])) {
        contract_failure("protocol.validate_snapshot", "sent_at_utc fraction is invalid");
      }
    }
  }
}

[[nodiscard]] std::array<double, 2> validate_vector(const boost::json::value& value,
                                                    const std::string_view context) {
  const boost::json::object& vector = require_object(value, context);
  require_exact_members(vector, {"x", "y"}, context);
  return {require_number(require_member(vector, "x", context), context),
          require_number(require_member(vector, "y", context), context)};
}

void validate_players(const boost::json::value& value, const std::size_t expected_player_count) {
  if (!value.is_array()) {
    contract_failure("protocol.validate_snapshot", "players must be an array");
  }
  const boost::json::array& players = value.as_array();
  if (expected_player_count == 0 || expected_player_count > 4'096 ||
      players.size() != expected_player_count) {
    contract_failure("protocol.validate_snapshot",
                     "snapshot player count does not match the complete fixture");
  }

  std::uint64_t previous_entity_id = 0;
  for (std::size_t index = 0; index < players.size(); ++index) {
    const boost::json::object& player = require_object(players[index], "player must be an object");
    require_exact_members(player, {"entity_id", "position", "velocity", "acceleration"},
                          "player member set is invalid");
    const std::uint64_t entity_id =
        require_unsigned_integer(require_member(player, "entity_id", "entity ID missing"), 1,
                                 kMaximumSafeInteger, "entity ID is invalid");
    if (entity_id != index + 1 || entity_id <= previous_entity_id) {
      contract_failure("protocol.validate_snapshot",
                       "players are missing, duplicated, or not in canonical order");
    }
    previous_entity_id = entity_id;
    const std::array<double, 2> position = validate_vector(
        require_member(player, "position", "position missing"), "position vector is invalid");
    const std::array<double, 2> velocity = validate_vector(
        require_member(player, "velocity", "velocity missing"), "velocity vector is invalid");
    const std::array<double, 2> acceleration =
        validate_vector(require_member(player, "acceleration", "acceleration missing"),
                        "acceleration vector is invalid");
    const bool exact_two_player_fixture = expected_player_count == 2;
    const double expected_velocity_x = entity_id == 1 ? 1.0 : -1.0;
    const double minimum_position = exact_two_player_fixture ? 2.0 : 1.0;
    const double maximum_position_x = exact_two_player_fixture ? 98.0 : 99.0;
    const double maximum_position_y = exact_two_player_fixture ? 78.0 : 79.0;
    if (position[0] < minimum_position || position[0] > maximum_position_x ||
        position[1] < minimum_position || position[1] > maximum_position_y || velocity[1] != 0.0 ||
        acceleration[0] != 0.0 || acceleration[1] != 0.0 ||
        (exact_two_player_fixture && velocity[0] != expected_velocity_x) ||
        (!exact_two_player_fixture && std::abs(velocity[0]) > 1.0)) {
      contract_failure("protocol.validate_snapshot",
                       "player snapshot is not a complete coherent fixture state");
    }
  }
}

void validate_error_object(const IntegrationHttpResponse& response,
                           const http::status expected_status,
                           const std::string_view expected_error_code,
                           const std::string_view expected_request_id) {
  if (response.result() != expected_status) {
    contract_failure("protocol.validate_error", "HTTP error status is invalid");
  }
  validate_response_headers(response, expected_request_id, std::nullopt);
  const boost::json::value document =
      parse_json(response.body(), kMaximumHttpResponseBytes, "protocol.validate_error");
  const boost::json::object& envelope =
      validate_envelope(document, kErrorSchema, expected_request_id);
  if (!require_member(envelope, "data", "error data missing").is_null()) {
    contract_failure("protocol.validate_error", "error response data must be null");
  }
  const boost::json::object& error = require_object(
      require_member(envelope, "error", "error is missing"), "error must be an object");
  require_exact_members(error, {"code", "message", "retryable", "details"},
                        "error member set is invalid");
  require_exact_string(require_member(error, "code", "error code missing"), expected_error_code,
                       "error code is invalid");
  const std::string_view message = require_string(
      require_member(error, "message", "error message missing"), "error message is invalid");
  if (message.empty() || message.size() > 256) {
    contract_failure("protocol.validate_error", "error message length is invalid");
  }
  const boost::json::value& retryable = require_member(error, "retryable", "retryable missing");
  if (!retryable.is_bool() || retryable.as_bool()) {
    contract_failure("protocol.validate_error", "representative permanent error is retryable");
  }
  const boost::json::object& details = require_object(
      require_member(error, "details", "error details missing"), "error details must be an object");
  if (expected_error_code == "PROTOCOL.METHOD_NOT_ALLOWED") {
    require_exact_members(details, {"allowed_methods"}, "method error details are invalid");
  } else {
    require_exact_members(details, {}, "error details must be empty for this failure");
  }
  if (!require_member(envelope, "error", "error missing").is_object()) {
    contract_failure("protocol.validate_error", "error member is invalid");
  }
}

} // namespace

void validate_configuration_response(const IntegrationHttpResponse& response,
                                     const std::string_view expected_request_id,
                                     const std::string_view expected_allowed_origin) {
  if (response.result() != http::status::ok) {
    contract_failure("protocol.validate_configuration", "configuration status is not 200");
  }
  validate_response_headers(response, expected_request_id, expected_allowed_origin);
  const boost::json::value document =
      parse_json(response.body(), kMaximumHttpResponseBytes, "protocol.validate_configuration");
  const boost::json::object& envelope =
      validate_envelope(document, kConfigurationSchema, expected_request_id);
  validate_configuration_data(require_member(envelope, "data", "configuration data missing"));
  if (!require_member(envelope, "error", "configuration error missing").is_null()) {
    contract_failure("protocol.validate_configuration", "configuration error must be null");
  }
}

void validate_readiness_response(const IntegrationHttpResponse& response,
                                 const std::string_view expected_request_id) {
  if (response.result() != http::status::ok) {
    contract_failure("protocol.validate_readiness", "readiness status is not 200");
  }
  validate_response_headers(response, expected_request_id, std::nullopt);
  const boost::json::value document =
      parse_json(response.body(), kMaximumHttpResponseBytes, "protocol.validate_readiness");
  const boost::json::object& envelope =
      validate_envelope(document, kReadinessSchema, expected_request_id);
  const boost::json::object& data =
      require_object(require_member(envelope, "data", "readiness data missing"),
                     "readiness data must be an object");
  require_exact_members(data, {"status", "snapshot_available"},
                        "readiness data member set is invalid");
  require_exact_string(require_member(data, "status", "readiness status missing"), "ready",
                       "readiness data status is invalid");
  const boost::json::value& snapshot_available =
      require_member(data, "snapshot_available", "snapshot availability missing");
  if (!snapshot_available.is_bool() || !snapshot_available.as_bool()) {
    contract_failure("protocol.validate_readiness", "snapshot_available must be true");
  }
  if (!require_member(envelope, "error", "readiness error missing").is_null()) {
    contract_failure("protocol.validate_readiness", "readiness error must be null");
  }
}

void validate_error_response(const IntegrationHttpResponse& response,
                             const http::status expected_status,
                             const std::string_view expected_error_code,
                             const std::string_view expected_request_id) {
  validate_error_object(response, expected_status, expected_error_code, expected_request_id);
}

void validate_method_not_allowed_details(const IntegrationHttpResponse& response) {
  if (response[http::field::allow] != "GET") {
    contract_failure("protocol.validate_method_error", "Allow header must be GET");
  }
  const boost::json::value document =
      parse_json(response.body(), kMaximumHttpResponseBytes, "protocol.validate_method_error");
  const boost::json::object& envelope = require_object(document, "envelope must be an object");
  const boost::json::object& error =
      require_object(require_member(envelope, "error", "error missing"), "error must be an object");
  const boost::json::object& details = require_object(
      require_member(error, "details", "details missing"), "details must be an object");
  const boost::json::value& methods =
      require_member(details, "allowed_methods", "allowed methods missing");
  if (!methods.is_array() || methods.as_array().size() != 1 ||
      require_string(methods.as_array().front(), "allowed method must be a string") != "GET") {
    contract_failure("protocol.validate_method_error", "allowed_methods must contain only GET");
  }
}

void validate_upgrade_required_header(const IntegrationHttpResponse& response) {
  if (response[http::field::upgrade] != "websocket") {
    contract_failure("protocol.validate_upgrade_error", "Upgrade header must be websocket");
  }
}

void validate_snapshot_handshake(const IntegrationHttpResponse& response,
                                 const std::string_view expected_request_id) {
  if (response.result() != http::status::switching_protocols ||
      response[http::field::sec_websocket_protocol] != kSnapshotSubprotocol ||
      response["X-Request-ID"] != expected_request_id ||
      !response[http::field::sec_websocket_extensions].empty()) {
    contract_failure("protocol.validate_websocket_handshake",
                     "WebSocket handshake metadata is invalid");
  }
}

SnapshotContractObservation validate_snapshot_message(const std::string_view encoded_message,
                                                      const std::string_view expected_request_id,
                                                      const std::uint64_t expected_message_sequence,
                                                      const std::size_t expected_player_count) {
  const boost::json::value document =
      parse_json(encoded_message, kMaximumSnapshotFrameBytes, "protocol.validate_snapshot");
  const boost::json::object& envelope =
      validate_envelope(document, kSnapshotSchema, expected_request_id);
  const boost::json::object& data = require_object(
      require_member(envelope, "data", "snapshot data missing"), "snapshot data must be an object");
  require_exact_members(data, {"tick_sequence", "players"}, "snapshot data member set is invalid");
  const std::uint64_t tick_sequence =
      require_unsigned_integer(require_member(data, "tick_sequence", "tick sequence missing"), 1,
                               kMaximumSafeInteger, "tick sequence is invalid");
  validate_players(require_member(data, "players", "players missing"), expected_player_count);
  if (!require_member(envelope, "error", "snapshot error missing").is_null()) {
    contract_failure("protocol.validate_snapshot", "snapshot error must be null");
  }

  const boost::json::object& metadata =
      require_object(require_member(envelope, "meta", "snapshot metadata missing"),
                     "snapshot metadata must be an object");
  const std::uint64_t message_sequence = require_unsigned_integer(
      require_member(metadata, "message_sequence", "message sequence missing"), 1,
      kMaximumSafeInteger, "message sequence is invalid");
  if (message_sequence != expected_message_sequence) {
    contract_failure("protocol.validate_snapshot",
                     "message sequence does not start at one and increase exactly once");
  }
  validate_utc_timestamp(
      require_string(require_member(metadata, "sent_at_utc", "snapshot timestamp missing"),
                     "snapshot timestamp must be a string"));
  return SnapshotContractObservation{tick_sequence, message_sequence};
}

} // namespace blob_royale::integration_test
