#include "protocol_json_encoding.hpp"

#include "bounded_json_serialization.hpp"
#include "protocol_encoding_error.hpp"
#include "utc_timestamp.hpp"

#include "player_snapshot.hpp"
#include "simulation_limits.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

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

[[nodiscard]] json::object encode_response_metadata(const std::string_view schema_id,
                                                    const RequestId& request_id) {
  json::object metadata;
  metadata.reserve(3);
  metadata.emplace("protocol_version", kProtocolVersion);
  metadata.emplace("schema_id", schema_id);
  metadata.emplace("request_id", request_id.value());
  return metadata;
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
  if (!is_accepted_utc_timestamp(sent_at_utc)) {
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
