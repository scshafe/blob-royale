#include "protocol_json_encoding.hpp"

#include "protocol_encoding_error.hpp"

#include "player_snapshot.hpp"
#include "simulation_limits.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/serializer.hpp>
#include <boost/json/value.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace blob_royale::protocol {
namespace {

namespace json = boost::json;
namespace simulation = blob_royale::simulation;

static_assert(kMaximumSafeInteger == simulation::kMaximumProtocolSafeInteger);
static_assert(kMaximumFiniteWorldScalar == simulation::kMaximumPhysicalComponentMagnitude);
static_assert(kMaximumPublicWorldDimension == simulation::kMaximumWorldDimension);
static_assert(kRequiredSimulationTicksPerSecond == simulation::kSimulationTicksPerSecond);
static_assert(kRequiredFixedDeltaSeconds == simulation::kFixedDeltaSeconds);
static_assert(kSnapshotPlayerLimit == simulation::kMaximumPlayerCount);

[[nodiscard]] json::value encode_json_number(const double value) {
  if (std::trunc(value) == value && std::abs(value) <= static_cast<double>(kMaximumSafeInteger)) {
    return json::value(static_cast<std::int64_t>(value));
  }
  return json::value(value);
}

[[nodiscard]] json::object encode_response_metadata(const std::string_view schema_id,
                                                    const RequestId& request_id) {
  json::object metadata;
  metadata.reserve(3);
  metadata.emplace("protocol_version", kProtocolVersion);
  metadata.emplace("schema_id", schema_id);
  metadata.emplace("request_id", request_id.value());
  return metadata;
}

void validate_output_byte_limit(const std::size_t output_byte_limit,
                                const std::size_t protocol_maximum_byte_count,
                                const std::string_view context) {
  if (output_byte_limit == 0 || output_byte_limit > protocol_maximum_byte_count) {
    throw ProtocolEncodingError{
        ProtocolEncodingErrorCode::kOutputByteLimitInvalid, std::string{context},
        "output byte limit must be positive and no greater than the protocol maximum"};
  }
}

[[nodiscard]] std::string serialize_bounded_json(const json::value& document,
                                                 const std::size_t output_byte_limit,
                                                 const std::size_t protocol_maximum_byte_count,
                                                 const std::string_view context) {
  validate_output_byte_limit(output_byte_limit, protocol_maximum_byte_count, context);

  json::serializer serializer;
  serializer.reset(&document);
  std::string output;
  output.reserve(std::min<std::size_t>(output_byte_limit, 16'384));
  std::array<char, 4'096> buffer{};
  while (!serializer.done()) {
    const std::size_t remaining_byte_count = output_byte_limit - output.size();
    const std::size_t next_read_byte_count =
        std::min(buffer.size(), remaining_byte_count + std::size_t{1});
    const json::string_view chunk = serializer.read(buffer.data(), next_read_byte_count);
    if (chunk.size() > remaining_byte_count) {
      throw ProtocolEncodingError{ProtocolEncodingErrorCode::kEncodedPayloadTooLarge,
                                  std::string{context},
                                  "complete encoded JSON exceeds the configured byte limit"};
    }
    output.append(chunk.data(), chunk.size());
  }
  return output;
}

[[nodiscard]] bool is_ascii_digit(const char character) noexcept {
  return character >= '0' && character <= '9';
}

[[nodiscard]] unsigned parse_two_digits(const std::string_view value,
                                        const std::size_t offset) noexcept {
  return static_cast<unsigned>((value[offset] - '0') * 10 + (value[offset + 1] - '0'));
}

[[nodiscard]] unsigned parse_four_digits(const std::string_view value,
                                         const std::size_t offset) noexcept {
  return static_cast<unsigned>((value[offset] - '0') * 1'000 + (value[offset + 1] - '0') * 100 +
                               (value[offset + 2] - '0') * 10 + (value[offset + 3] - '0'));
}

[[nodiscard]] bool is_leap_year(const unsigned year) noexcept {
  return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

[[nodiscard]] unsigned days_in_month(const unsigned year, const unsigned month) noexcept {
  static constexpr std::array<unsigned, 12> kDaysByMonth{31, 28, 31, 30, 31, 30,
                                                         31, 31, 30, 31, 30, 31};
  if (month == 2U && is_leap_year(year)) {
    return 29U;
  }
  return kDaysByMonth[month - 1U];
}

[[nodiscard]] bool is_valid_utc_timestamp(const std::string_view value) noexcept {
  if (value.size() < 20 || value.size() > 32 || value.back() != 'Z') {
    return false;
  }
  constexpr std::array<std::size_t, 14> kDigitPositions{0, 1,  2,  3,  5,  6,  8,
                                                        9, 11, 12, 14, 15, 17, 18};
  for (const std::size_t position : kDigitPositions) {
    if (!is_ascii_digit(value[position])) {
      return false;
    }
  }
  if (value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' ||
      value[16] != ':') {
    return false;
  }
  if (value.size() > 20) {
    if (value.size() < 22 || value[19] != '.') {
      return false;
    }
    for (std::size_t position = 20; position + 1 < value.size(); ++position) {
      if (!is_ascii_digit(value[position])) {
        return false;
      }
    }
  } else if (value[19] != 'Z') {
    return false;
  }

  const unsigned year = parse_four_digits(value, 0);
  const unsigned month = parse_two_digits(value, 5);
  const unsigned day = parse_two_digits(value, 8);
  const unsigned hour = parse_two_digits(value, 11);
  const unsigned minute = parse_two_digits(value, 14);
  const unsigned second = parse_two_digits(value, 17);
  if (year == 0 || month == 0 || month > 12 || day == 0 || day > days_in_month(year, month) ||
      hour > 23 || minute > 59 || second > 60) {
    return false;
  }
  return true;
}

void validate_snapshot(const simulation::WorldSnapshot& snapshot) {
  // WorldSnapshot's private construction boundary already guarantees player count, ID order,
  // exact-integer bounds, finite scalars, scalar bounds, and canonical signed zero. Protocol v1
  // adds only the rule that the loaded tick-zero state is not a publishable wire frame.
  if (snapshot.tick_sequence().value() == 0) {
    throw ProtocolEncodingError{ProtocolEncodingErrorCode::kSnapshotTickOutOfRange,
                                "snapshot.tick_sequence",
                                "tick sequence must be in the inclusive range 1 to 2^53-1"};
  }
}

[[nodiscard]] json::object encode_vector(const simulation::Vector2& vector) {
  json::object encoded;
  encoded.reserve(2);
  encoded.emplace("x", encode_json_number(vector.x()));
  encoded.emplace("y", encode_json_number(vector.y()));
  return encoded;
}

[[nodiscard]] json::object encode_player(const simulation::PlayerSnapshot& player) {
  json::object encoded;
  encoded.reserve(4);
  encoded.emplace("entity_id", player.entity_id().value());
  encoded.emplace("position", encode_vector(player.position()));
  encoded.emplace("velocity", encode_vector(player.velocity()));
  encoded.emplace("acceleration", encode_vector(player.acceleration()));
  return encoded;
}

} // namespace

std::string encode_configuration_response(const PublicConfiguration& configuration,
                                          const RequestId& request_id,
                                          const std::size_t output_byte_limit) {
  json::object world;
  world.reserve(3);
  world.emplace("width_world_units", encode_json_number(configuration.world_width()));
  world.emplace("height_world_units", encode_json_number(configuration.world_height()));
  world.emplace("player_radius_world_units", encode_json_number(configuration.player_radius()));

  json::object simulation_data;
  simulation_data.reserve(2);
  simulation_data.emplace("ticks_per_second", kRequiredSimulationTicksPerSecond);
  simulation_data.emplace("fixed_delta_seconds", kRequiredFixedDeltaSeconds);

  json::object presentation;
  presentation.reserve(3);
  presentation.emplace("snapshots_per_second", configuration.snapshots_per_second());
  presentation.emplace("snapshot_player_limit", kSnapshotPlayerLimit);
  presentation.emplace("snapshot_frame_max_bytes", kSnapshotFrameMaximumByteCount);

  json::object data;
  data.reserve(3);
  data.emplace("world", std::move(world));
  data.emplace("simulation", std::move(simulation_data));
  data.emplace("presentation", std::move(presentation));

  json::object envelope;
  envelope.reserve(3);
  envelope.emplace("data", std::move(data));
  envelope.emplace("error", nullptr);
  envelope.emplace("meta", encode_response_metadata(kConfigurationResponseSchemaId, request_id));
  return serialize_bounded_json(json::value(std::move(envelope)), output_byte_limit,
                                kHttpJsonResponseMaximumByteCount,
                                "configuration_response.encoding");
}

std::string encode_liveness_response(const RequestId& request_id,
                                     const std::size_t output_byte_limit) {
  json::object data;
  data.emplace("status", "alive");

  json::object envelope;
  envelope.reserve(3);
  envelope.emplace("data", std::move(data));
  envelope.emplace("error", nullptr);
  envelope.emplace("meta", encode_response_metadata(kLivenessResponseSchemaId, request_id));
  return serialize_bounded_json(json::value(std::move(envelope)), output_byte_limit,
                                kHttpJsonResponseMaximumByteCount, "liveness_response.encoding");
}

std::string encode_readiness_response(const RequestId& request_id,
                                      const std::size_t output_byte_limit) {
  json::object data;
  data.reserve(2);
  data.emplace("status", "ready");
  data.emplace("snapshot_available", true);

  json::object envelope;
  envelope.reserve(3);
  envelope.emplace("data", std::move(data));
  envelope.emplace("error", nullptr);
  envelope.emplace("meta", encode_response_metadata(kReadinessResponseSchemaId, request_id));
  return serialize_bounded_json(json::value(std::move(envelope)), output_byte_limit,
                                kHttpJsonResponseMaximumByteCount, "readiness_response.encoding");
}

std::string encode_error_response(const HttpError& error, const RequestId& request_id,
                                  const std::size_t output_byte_limit) {
  json::object details;
  details.reserve(5);
  if (!error.allowed_methods().empty()) {
    json::array methods;
    methods.reserve(error.allowed_methods().size());
    for (const std::string_view method : error.allowed_methods()) {
      methods.emplace_back(method);
    }
    details.emplace("allowed_methods", std::move(methods));
  }
  if (const auto websocket_version = error.expected_websocket_version();
      websocket_version.has_value()) {
    details.emplace("expected_websocket_version", *websocket_version);
  }
  if (error.limit().has_value()) {
    details.emplace("limit", *error.limit());
  }
  if (const auto reason = error.reason(); reason.has_value()) {
    details.emplace("reason", *reason);
  }
  if (error.retry_after_ms().has_value()) {
    details.emplace("retry_after_ms", *error.retry_after_ms());
  }

  json::object encoded_error;
  encoded_error.reserve(4);
  encoded_error.emplace("code", error.code());
  encoded_error.emplace("message", error.message());
  encoded_error.emplace("retryable", error.retryable());
  encoded_error.emplace("details", std::move(details));

  json::object envelope;
  envelope.reserve(3);
  envelope.emplace("data", nullptr);
  envelope.emplace("error", std::move(encoded_error));
  envelope.emplace("meta", encode_response_metadata(kErrorResponseSchemaId, request_id));
  return serialize_bounded_json(json::value(std::move(envelope)), output_byte_limit,
                                kHttpJsonResponseMaximumByteCount, "error_response.encoding");
}

std::string encode_snapshot_message(const simulation::WorldSnapshot& snapshot,
                                    const RequestId& request_id,
                                    const std::uint64_t message_sequence,
                                    const std::string_view sent_at_utc,
                                    const std::size_t output_byte_limit) {
  if (message_sequence == 0 || message_sequence > kMaximumSafeInteger) {
    throw ProtocolEncodingError{ProtocolEncodingErrorCode::kMessageSequenceOutOfRange,
                                "snapshot_message.meta.message_sequence",
                                "message sequence must be in the inclusive range 1 to 2^53-1"};
  }
  if (!is_valid_utc_timestamp(sent_at_utc)) {
    throw ProtocolEncodingError{ProtocolEncodingErrorCode::kTimestampInvalid,
                                "snapshot_message.meta.sent_at_utc",
                                "timestamp must be a bounded RFC 3339 UTC value ending in Z"};
  }
  validate_snapshot(snapshot);

  json::array players;
  players.reserve(snapshot.players().size());
  for (const simulation::PlayerSnapshot& player : snapshot.players()) {
    players.emplace_back(encode_player(player));
  }

  json::object data;
  data.reserve(2);
  data.emplace("tick_sequence", snapshot.tick_sequence().value());
  data.emplace("players", std::move(players));

  json::object metadata;
  metadata.reserve(5);
  metadata.emplace("protocol_version", kProtocolVersion);
  metadata.emplace("schema_id", kSnapshotMessageSchemaId);
  metadata.emplace("request_id", request_id.value());
  metadata.emplace("message_sequence", message_sequence);
  metadata.emplace("sent_at_utc", sent_at_utc);

  json::object envelope;
  envelope.reserve(3);
  envelope.emplace("data", std::move(data));
  envelope.emplace("error", nullptr);
  envelope.emplace("meta", std::move(metadata));
  return serialize_bounded_json(json::value(std::move(envelope)), output_byte_limit,
                                kSnapshotFrameMaximumByteCount, "snapshot_message.encoding");
}

} // namespace blob_royale::protocol
