#include "protocol_test_fixture.hpp"

#include "entity_id.hpp"
#include "game_world.hpp"
#include "http_error.hpp"
#include "physics_body.hpp"
#include "protocol_constants.hpp"
#include "protocol_encoding_error.hpp"
#include "protocol_json_encoding.hpp"
#include "public_configuration.hpp"
#include "request_id.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <boost/json/parse.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace protocol = blob_royale::protocol;
namespace fixture = blob_royale::protocol::test_fixture;
namespace simulation = blob_royale::simulation;

namespace {

void require_fields_in_order(const std::string_view encoded,
                             const std::vector<std::string_view>& fields) {
  std::size_t previous_position = 0;
  bool first_field = true;
  for (const std::string_view field : fields) {
    const std::size_t field_position = encoded.find(field, previous_position);
    REQUIRE(field_position != std::string_view::npos);
    if (!first_field) {
      REQUIRE(field_position > previous_position);
    }
    previous_position = field_position;
    first_field = false;
  }
}

[[nodiscard]] protocol::HttpError golden_error() {
  return protocol::HttpError::create(protocol::HttpErrorCode::kMethodNotAllowed,
                                     "GET is the only method allowed for this route.");
}

} // namespace

TEST_CASE("Configuration encoder matches the accepted golden example and canonical bytes",
          "[unit][protocol][encoding][golden]") {
  const std::string encoded = protocol::encode_configuration_response(
      fixture::golden_configuration(), fixture::request_id(fixture::kConfigurationRequestId));

  fixture::require_json_matches_golden_example(encoded, "configuration-response.json");
  CHECK(
      encoded ==
      R"({"data":{"world":{"width_world_units":960,"height_world_units":640,"player_radius_world_units":10},"simulation":{"ticks_per_second":400,"fixed_delta_seconds":2.5E-3},"presentation":{"snapshots_per_second":30,"snapshot_player_limit":4096,"snapshot_frame_max_bytes":2097152}},"error":null,"meta":{"protocol_version":"1.0","schema_id":"blob-royale://protocol/v1/configuration-response","request_id":"018f47a4-5d5b-7b86-bd4a-273c2dd6f4ee"}})");
}

TEST_CASE("Liveness encoder matches the accepted golden example and canonical bytes",
          "[unit][protocol][encoding][golden]") {
  const std::string encoded =
      protocol::encode_liveness_response(fixture::request_id(fixture::kLivenessRequestId));

  fixture::require_json_matches_golden_example(encoded, "liveness-response.json");
  CHECK(
      encoded ==
      R"({"data":{"status":"alive"},"error":null,"meta":{"protocol_version":"1.0","schema_id":"blob-royale://protocol/v1/liveness-response","request_id":"probe-live-0001"}})");
}

TEST_CASE("Readiness encoder matches the accepted golden example and canonical bytes",
          "[unit][protocol][encoding][golden]") {
  const std::string encoded =
      protocol::encode_readiness_response(fixture::request_id(fixture::kReadinessRequestId));

  fixture::require_json_matches_golden_example(encoded, "readiness-response.json");
  CHECK(
      encoded ==
      R"({"data":{"status":"ready","snapshot_available":true},"error":null,"meta":{"protocol_version":"1.0","schema_id":"blob-royale://protocol/v1/readiness-response","request_id":"probe-ready-0001"}})");
}

TEST_CASE("Error encoder matches the accepted golden example and canonical bytes",
          "[unit][protocol][encoding][golden]") {
  const std::string encoded = protocol::encode_error_response(
      golden_error(), fixture::request_id(fixture::kErrorRequestId));

  fixture::require_json_matches_golden_example(encoded, "error-response.json");
  CHECK(
      encoded ==
      R"({"data":null,"error":{"code":"PROTOCOL.METHOD_NOT_ALLOWED","message":"GET is the only method allowed for this route.","retryable":false,"details":{"allowed_methods":["GET"]}},"meta":{"protocol_version":"1.0","schema_id":"blob-royale://protocol/v1/error-response","request_id":"018f47a4-66c3-79b4-91ac-ecb672f93fc0"}})");
}

TEST_CASE("Snapshot encoder matches the accepted golden example",
          "[unit][protocol][encoding][golden]") {
  const std::string encoded = protocol::encode_snapshot_message(
      fixture::golden_snapshot(), fixture::request_id(fixture::kSnapshotRequestId), 48,
      fixture::kSnapshotTimestamp);

  fixture::require_json_matches_golden_example(encoded, "snapshot-message.json");
  CHECK(
      encoded ==
      R"({"data":{"tick_sequence":1601,"players":[{"entity_id":1,"position":{"x":240,"y":300},"velocity":{"x":0,"y":0},"acceleration":{"x":0,"y":0}},{"entity_id":2,"position":{"x":240,"y":320},"velocity":{"x":0,"y":0},"acceleration":{"x":0,"y":0}}]},"error":null,"meta":{"protocol_version":"1.0","schema_id":"blob-royale://protocol/v1/snapshot-message","request_id":"018f47a4-70b7-77c8-aa51-f91265a9bb2f","message_sequence":48,"sent_at_utc":"2026-08-05T19:42:17.125Z"}})");
}

TEST_CASE("Snapshot encoder emits every normative object member in canonical order",
          "[unit][protocol][encoding][ordering]") {
  const std::string encoded = protocol::encode_snapshot_message(
      fixture::golden_snapshot(), fixture::request_id(fixture::kSnapshotRequestId), 48,
      fixture::kSnapshotTimestamp);

  require_fields_in_order(encoded, {R"("data")", R"("tick_sequence")", R"("players")",
                                    R"("entity_id")", R"("position")", R"("x")", R"("y")",
                                    R"("velocity")", R"("acceleration")", R"("error")", R"("meta")",
                                    R"("protocol_version")", R"("schema_id")", R"("request_id")",
                                    R"("message_sequence")", R"("sent_at_utc")"});
}

TEST_CASE("Snapshot encoder returns identical bytes across repeated encodings",
          "[unit][protocol][encoding][determinism]") {
  const simulation::WorldSnapshot snapshot = fixture::golden_snapshot();
  const protocol::RequestId request_id = fixture::request_id(fixture::kSnapshotRequestId);
  const std::string expected =
      protocol::encode_snapshot_message(snapshot, request_id, 48, fixture::kSnapshotTimestamp);

  for (std::size_t repetition = 0; repetition < 100; ++repetition) {
    CHECK(protocol::encode_snapshot_message(snapshot, request_id, 48,
                                            fixture::kSnapshotTimestamp) == expected);
  }
}

TEST_CASE("Snapshot encoder accepts an empty complete state",
          "[unit][protocol][encoding][boundary]") {
  const std::string encoded = protocol::encode_snapshot_message(
      fixture::empty_snapshot(), fixture::request_id("empty-state"), 1, "2026-08-05T00:00:00Z");
  const boost::json::value parsed = boost::json::parse(encoded);
  REQUIRE(parsed.is_object());
  const boost::json::value& data = parsed.as_object().at("data");
  REQUIRE(data.is_object());
  const boost::json::value& encoded_players = data.as_object().at("players");
  REQUIRE(encoded_players.is_array());

  CHECK(encoded_players.as_array().empty());
}

TEST_CASE("Snapshot encoder accepts the exact maximum player count within the frame ceiling",
          "[unit][protocol][encoding][boundary]") {
  const std::string encoded = protocol::encode_snapshot_message(
      fixture::maximum_player_snapshot(), fixture::request_id("maximum-state"),
      protocol::kMaximumSafeInteger, "2026-08-05T00:00:00.12345678901Z");
  const boost::json::value parsed = boost::json::parse(encoded);
  REQUIRE(parsed.is_object());
  const boost::json::value& data = parsed.as_object().at("data");
  REQUIRE(data.is_object());
  const boost::json::value& encoded_players = data.as_object().at("players");
  REQUIRE(encoded_players.is_array());

  CHECK(encoded_players.as_array().size() == protocol::kSnapshotPlayerLimit);
  CHECK(encoded.size() <= protocol::kSnapshotFrameMaximumByteCount);
}

TEST_CASE("Snapshot encoder accepts the maximum safe entity and message identifiers",
          "[unit][protocol][encoding][boundary]") {
  const simulation::WorldSnapshot snapshot = fixture::snapshot_after_steps(
      fixture::default_simulation_config(),
      {fixture::stationary_player(protocol::kMaximumSafeInteger, 100.0, 100.0)}, 1);
  const std::string encoded =
      protocol::encode_snapshot_message(snapshot, fixture::request_id("maximum-scalars"),
                                        protocol::kMaximumSafeInteger, "2026-08-05T00:00:00Z");
  const boost::json::value parsed = boost::json::parse(encoded);
  REQUIRE(parsed.is_object());
  const boost::json::value& data = parsed.as_object().at("data");
  REQUIRE(data.is_object());
  const boost::json::value& encoded_players = data.as_object().at("players");
  REQUIRE(encoded_players.is_array());
  REQUIRE(encoded_players.as_array().size() == 1);
  const boost::json::value& encoded_player = encoded_players.as_array().front();
  REQUIRE(encoded_player.is_object());

  CHECK(encoded_player.as_object().at("entity_id").as_int64() ==
        static_cast<std::int64_t>(protocol::kMaximumSafeInteger));
}

TEST_CASE("Snapshot encoder rejects the initial tick because it is not a committed frame",
          "[unit][protocol][encoding][rejection]") {
  const simulation::WorldSnapshot initial_snapshot =
      simulation::GameSimulation::create(fixture::default_simulation_config(),
                                         simulation::GameWorld::create({}))
          .snapshot();

  fixture::require_protocol_error_code(
      [&initial_snapshot] {
        static_cast<void>(protocol::encode_snapshot_message(
            initial_snapshot, fixture::request_id("initial-state"), 1, "2026-08-05T00:00:00Z"));
      },
      protocol::ProtocolEncodingErrorCode::kSnapshotTickOutOfRange);
}

TEST_CASE("Snapshot encoder rejects message sequences outside the safe integer range",
          "[unit][protocol][encoding][rejection]") {
  constexpr std::array<std::uint64_t, 2> kInvalidMessageSequences{0, protocol::kMaximumSafeInteger +
                                                                         1};
  for (const std::uint64_t invalid_sequence : kInvalidMessageSequences) {
    fixture::require_protocol_error_code(
        [invalid_sequence] {
          static_cast<void>(protocol::encode_snapshot_message(
              fixture::golden_snapshot(), fixture::request_id("invalid-sequence"), invalid_sequence,
              "2026-08-05T00:00:00Z"));
        },
        protocol::ProtocolEncodingErrorCode::kMessageSequenceOutOfRange);
  }
}

TEST_CASE("Snapshot encoder rejects malformed or impossible UTC timestamps",
          "[unit][protocol][encoding][rejection]") {
  for (const std::string_view invalid_timestamp :
       {"2026-08-05T00:00:00", "2026-08-05 00:00:00Z", "2026-02-29T00:00:00Z",
        "2024-02-30T00:00:00Z", "2026-13-01T00:00:00Z", "2026-08-05T24:00:00Z",
        "2026-08-05T00:60:00Z", "2026-08-05T00:00:61Z", "2026-08-05T00:00:00.Z",
        "2026-08-05T00:00:00.123456789012Z"}) {
    fixture::require_protocol_error_code(
        [invalid_timestamp] {
          static_cast<void>(protocol::encode_snapshot_message(fixture::golden_snapshot(),
                                                              fixture::request_id("invalid-time"),
                                                              1, invalid_timestamp));
        },
        protocol::ProtocolEncodingErrorCode::kTimestampInvalid);
  }
}

TEST_CASE("Bounded encoder fails visibly instead of truncating an oversized result",
          "[unit][protocol][encoding][rejection]") {
  const simulation::WorldSnapshot snapshot = fixture::golden_snapshot();
  const protocol::RequestId request_id = fixture::request_id("bounded-output");
  const std::string complete =
      protocol::encode_snapshot_message(snapshot, request_id, 1, "2026-08-05T00:00:00Z");

  fixture::require_protocol_error_code(
      [&snapshot, &request_id, &complete] {
        static_cast<void>(protocol::encode_snapshot_message(
            snapshot, request_id, 1, "2026-08-05T00:00:00Z", complete.size() - 1));
      },
      protocol::ProtocolEncodingErrorCode::kEncodedPayloadTooLarge);
}

TEST_CASE("Bounded encoder accepts a complete result at the exact byte limit",
          "[unit][protocol][encoding][boundary]") {
  const simulation::WorldSnapshot snapshot = fixture::golden_snapshot();
  const protocol::RequestId request_id = fixture::request_id("exact-output-limit");
  const std::string complete =
      protocol::encode_snapshot_message(snapshot, request_id, 1, "2026-08-05T00:00:00Z");

  CHECK(protocol::encode_snapshot_message(snapshot, request_id, 1, "2026-08-05T00:00:00Z",
                                          complete.size()) == complete);
}

TEST_CASE("HTTP encoder fails visibly instead of truncating an oversized result",
          "[unit][protocol][encoding][rejection]") {
  const protocol::RequestId request_id = fixture::request_id("bounded-http-output");
  const std::string complete = protocol::encode_error_response(golden_error(), request_id);

  fixture::require_protocol_error_code(
      [&request_id, &complete] {
        static_cast<void>(
            protocol::encode_error_response(golden_error(), request_id, complete.size() - 1));
      },
      protocol::ProtocolEncodingErrorCode::kEncodedPayloadTooLarge);
}

TEST_CASE("Encoder rejects caller limits outside the accepted protocol ceiling",
          "[unit][protocol][encoding][rejection]") {
  for (const std::size_t invalid_limit :
       {std::size_t{0}, protocol::kHttpJsonResponseMaximumByteCount + 1}) {
    fixture::require_protocol_error_code(
        [invalid_limit] {
          static_cast<void>(protocol::encode_liveness_response(
              fixture::request_id("invalid-output-limit"), invalid_limit));
        },
        protocol::ProtocolEncodingErrorCode::kOutputByteLimitInvalid);
  }
}

TEST_CASE("Canonical simulation values prevent negative signed zero from reaching JSON",
          "[unit][protocol][encoding][invariant]") {
  constexpr fixture::PlayerValues kSignedZeroSource{1, 100.0, 100.0, -0.0, -0.0, -0.0, -0.0};
  const simulation::WorldSnapshot snapshot = fixture::snapshot_after_steps(
      fixture::default_simulation_config(), {fixture::player(kSignedZeroSource)}, 1);
  const std::string encoded = protocol::encode_snapshot_message(
      snapshot, fixture::request_id("signed-zero"), 1, "2026-08-05T00:00:00Z");

  CHECK(encoded.find(R"(:-0)") == std::string::npos);
}

TEST_CASE("Canonical world ordering produces ascending entity IDs on the wire",
          "[unit][protocol][encoding][invariant]") {
  const simulation::WorldSnapshot snapshot = fixture::snapshot_after_steps(
      fixture::default_simulation_config(),
      {fixture::stationary_player(20, 100.0, 100.0), fixture::stationary_player(3, 200.0, 100.0),
       fixture::stationary_player(11, 300.0, 100.0)},
      1);
  const std::string encoded = protocol::encode_snapshot_message(
      snapshot, fixture::request_id("ordered-ids"), 1, "2026-08-05T00:00:00Z");

  require_fields_in_order(encoded, {R"("entity_id":3)", R"("entity_id":11)", R"("entity_id":20)"});
}

TEST_CASE("Duplicate entity IDs fail before a partial snapshot can be encoded",
          "[unit][protocol][encoding][invariant]") {
  CHECK_THROWS_AS(simulation::GameWorld::create({fixture::zero_player(1), fixture::zero_player(1)}),
                  simulation::SimulationValidationError);
}

TEST_CASE("Player populations above the protocol limit fail before encoding",
          "[unit][protocol][encoding][invariant]") {
  std::vector<simulation::GameWorld::EntitySeed> players;
  players.reserve(simulation::kMaximumPlayerCount + 1);
  for (std::size_t index = 0; index <= simulation::kMaximumPlayerCount; ++index) {
    players.push_back(fixture::zero_player(static_cast<std::uint64_t>(index + 1)));
  }

  CHECK_THROWS_AS(simulation::GameWorld::create(std::move(players)),
                  simulation::SimulationValidationError);
}

TEST_CASE("Non-finite physical components fail before encoding",
          "[unit][protocol][encoding][invariant]") {
  CHECK_THROWS_AS(simulation::Vector2::create(std::numeric_limits<double>::infinity(), 0.0),
                  simulation::SimulationValidationError);
}

TEST_CASE("Entity IDs above the exact integer ceiling fail before encoding",
          "[unit][protocol][encoding][invariant]") {
  CHECK_THROWS_AS(simulation::EntityId::create(protocol::kMaximumSafeInteger + 1),
                  simulation::SimulationValidationError);
}
