#include "protocol_v2_test_fixture.hpp"

#include "component_encoding_registry.hpp"
#include "controller_directory_view.hpp"
#include "protocol_encoding_error.hpp"
#include "protocol_v2_constants.hpp"
#include "protocol_v2_frame_conformance.hpp"
#include "protocol_v2_json_encoding.hpp"
#include "session_welcome.hpp"
#include "v2_http_error.hpp"

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "component_kind_name.hpp"
#include "component_registry.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "http_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace protocol = blob_royale::protocol;
namespace fixture = blob_royale::protocol::v2_test_fixture;
namespace simulation = blob_royale::simulation;

namespace {

void require_members_in_order(const std::string_view encoded,
                              const std::vector<std::string_view>& members) {
  std::size_t previous_position = 0;
  bool first_member = true;
  for (const std::string_view member : members) {
    const std::size_t member_position = encoded.find(member, previous_position);
    REQUIRE(member_position != std::string_view::npos);
    if (!first_member) {
      REQUIRE(member_position > previous_position);
    }
    previous_position = member_position;
    first_member = false;
  }
}

} // namespace

TEST_CASE("Welcome encoder matches the accepted golden example and canonical bytes",
          "[unit][protocol][v2][encoding][golden]") {
  const std::string encoded = protocol::encode_welcome_message(
      fixture::golden_welcome(), fixture::session_request_id(), fixture::kWelcomeTimestamp);

  fixture::require_json_matches_v2_golden_example(encoded, "welcome-message.json");
  CHECK(
      encoded ==
      R"({"data":{"entity_id":7,"controller_id":3,"display_name":"Cole Shaffer","mode":"royale","map":"arena-960x640","accepted_command_kinds":["set_thrust"]},"error":null,"meta":{"protocol_version":"2.0","schema_id":"blob-royale://protocol/v2/welcome-message","request_id":"018f47a4-9c21-7f10-8a55-4b7d1e0c33a2","message_sequence":1,"sent_at_utc":"2026-09-06T18:04:11.500Z"}})");
}

TEST_CASE("Snapshot v2 encoder matches the accepted golden example",
          "[unit][protocol][v2][encoding][golden]") {
  const fixture::StubControllerDirectory directory = fixture::golden_directory();
  const std::string encoded = protocol::encode_snapshot_message_v2(
      fixture::golden_snapshot(), directory, fixture::session_request_id(),
      fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp);

  fixture::require_json_matches_v2_golden_example(encoded, "snapshot-message.json");
}

TEST_CASE("Snapshot v2 encoder emits canonical bytes for the accepted golden world",
          "[unit][protocol][v2][encoding][golden]") {
  const fixture::StubControllerDirectory directory = fixture::golden_directory();
  const std::string encoded = protocol::encode_snapshot_message_v2(
      fixture::golden_snapshot(), directory, fixture::session_request_id(),
      fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp);

  CHECK(
      encoded ==
      R"({"data":{"tick_sequence":12904,"entities":[{"entity_id":1,"components":{"physics_body":{"position":{"x":480,"y":160},"velocity":{"x":0,"y":0},"acceleration":{"x":0,"y":0},"radius":40,"mass":0,"collision_layer":2,"collision_mask":1,"is_static":true}}},{"entity_id":7,"components":{"controllable":{"controller_id":3,"controller_kind":"session","display_name":"Cole Shaffer"},"physics_body":{"position":{"x":4.125E2,"y":2.8825E2},"velocity":{"x":1.875E1,"y":-4.25E1},"acceleration":{"x":400,"y":0},"radius":10,"mass":1,"collision_layer":1,"collision_mask":3,"is_static":false},"zone_exposure":{"outside_ticks":0}}},{"entity_id":8,"components":{"controllable":{"controller_id":4,"controller_kind":"wanderer","display_name":"wanderer-1"},"physics_body":{"position":{"x":7.605E2,"y":5.1225E2},"velocity":{"x":-6.25E0,"y":3.15E1},"acceleration":{"x":0,"y":-400},"radius":10,"mass":1,"collision_layer":1,"collision_mask":3,"is_static":false},"zone_exposure":{"outside_ticks":214}}},{"entity_id":9,"components":{"zone":{"center":{"x":480,"y":320},"radius":2.105E2}}}],"match":{"mode":"royale","phase":"running","phase_started_tick":10904,"outcome":{"kind":"none","winner_entity_id":null,"winner_team_id":null},"placements":[{"entity_id":5,"controller_id":6,"placement":3,"eliminated_tick":12400}],"mode_state":{"schema_id":"blob-royale://protocol/v2/mode-state/royale","value":{"previous_phase":"running"}}}},"error":null,"meta":{"protocol_version":"2.0","schema_id":"blob-royale://protocol/v2/snapshot-message","request_id":"018f47a4-9c21-7f10-8a55-4b7d1e0c33a2","message_sequence":129,"sent_at_utc":"2026-09-06T18:04:17.750Z"}})");
}

TEST_CASE("Error response v2 encoder matches the accepted golden example and canonical bytes",
          "[unit][protocol][v2][encoding][golden]") {
  const std::string encoded =
      protocol::encode_error_response_v2(protocol::V2HttpError::invalid_forwarded_client(
                                             protocol::ForwardedClientReason::kMultipleValues),
                                         fixture::session_request_id());

  fixture::require_json_matches_v2_golden_example(encoded, "error-response.json");
  CHECK(
      encoded ==
      R"({"data":null,"error":{"code":"PROTOCOL.INVALID_FORWARDED_CLIENT","message":"A proxy-forwarded connection must present exactly one canonical forwarded client address.","retryable":false,"details":{"forwarded_client_reason":"multiple_values"}},"meta":{"protocol_version":"2.0","schema_id":"blob-royale://protocol/v2/error-response","request_id":"018f47a4-9c21-7f10-8a55-4b7d1e0c33a2"}})");
}

TEST_CASE("Error response v2 encoder carries the fourteen rows v2 shares with v1",
          "[unit][protocol][v2][encoding]") {
  const std::string encoded =
      protocol::encode_error_response_v2(protocol::V2HttpError::shared(protocol::HttpError::create(
                                             protocol::HttpErrorCode::kMethodNotAllowed,
                                             "GET is the only method allowed for this route.")),
                                         fixture::session_request_id());

  CHECK(encoded.find(R"("code":"PROTOCOL.METHOD_NOT_ALLOWED")") != std::string::npos);
  CHECK(encoded.find(R"("allowed_methods":["GET"])") != std::string::npos);
  CHECK(encoded.find(R"("protocol_version":"2.0")") != std::string::npos);
  CHECK(encoded.find(R"("schema_id":"blob-royale://protocol/v2/error-response")") !=
        std::string::npos);
}

TEST_CASE("Snapshot v2 encoder emits every normative object member in canonical order",
          "[unit][protocol][v2][encoding]") {
  const fixture::StubControllerDirectory directory = fixture::golden_directory();
  const std::string encoded = protocol::encode_snapshot_message_v2(
      fixture::golden_snapshot(), directory, fixture::session_request_id(),
      fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp);

  require_members_in_order(encoded, {R"("data")",
                                     R"("tick_sequence")",
                                     R"("entities")",
                                     R"("entity_id")",
                                     R"("components")",
                                     R"("controllable")",
                                     R"("controller_id")",
                                     R"("controller_kind")",
                                     R"("display_name")",
                                     R"("physics_body")",
                                     R"("position")",
                                     R"("velocity")",
                                     R"("acceleration")",
                                     R"("radius")",
                                     R"("mass")",
                                     R"("collision_layer")",
                                     R"("collision_mask")",
                                     R"("is_static")",
                                     R"("zone_exposure")",
                                     R"("match")",
                                     R"("mode")",
                                     R"("phase")",
                                     R"("phase_started_tick")",
                                     R"("outcome")",
                                     R"("kind")",
                                     R"("winner_entity_id")",
                                     R"("winner_team_id")",
                                     R"("placements")",
                                     R"("mode_state")",
                                     R"("schema_id")",
                                     R"("value")",
                                     R"("error")",
                                     R"("meta")",
                                     R"("protocol_version")",
                                     R"("request_id")",
                                     R"("message_sequence")",
                                     R"("sent_at_utc")"});
}

TEST_CASE("Snapshot v2 encoder returns identical bytes across repeated encodings",
          "[unit][protocol][v2][encoding]") {
  const fixture::StubControllerDirectory directory = fixture::golden_directory();
  const std::string first = protocol::encode_snapshot_message_v2(
      fixture::golden_snapshot(), directory, fixture::session_request_id(),
      fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp);
  const std::string second = protocol::encode_snapshot_message_v2(
      fixture::golden_snapshot(), directory, fixture::session_request_id(),
      fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp);

  CHECK(first == second);
}

TEST_CASE("Every encoded v2 frame satisfies the invariants JSON Schema cannot express",
          "[unit][protocol][v2][conformance]") {
  const fixture::StubControllerDirectory directory = fixture::golden_directory();

  CHECK(protocol::check_v2_server_frame(protocol::encode_welcome_message(
            fixture::golden_welcome(), fixture::session_request_id(),
            fixture::kWelcomeTimestamp)) == protocol::V2FrameConformance::kConforms);
  CHECK(protocol::check_v2_server_frame(protocol::encode_snapshot_message_v2(
            fixture::golden_snapshot(), directory, fixture::session_request_id(),
            fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp)) ==
        protocol::V2FrameConformance::kConforms);
  CHECK(
      protocol::check_v2_server_frame(protocol::encode_error_response_v2(
          protocol::V2HttpError::invalid_forwarded_client(protocol::ForwardedClientReason::kAbsent),
          fixture::session_request_id())) == protocol::V2FrameConformance::kConforms);
}

TEST_CASE("Conformance rejects a frame carrying both data and error",
          "[unit][protocol][v2][conformance][rejection]") {
  constexpr std::string_view kDataAndError =
      R"({"data":{"tick_sequence":1,"entities":[],"match":{}},)"
      R"("error":{"code":"SERVICE.INTERNAL_FAILURE","message":"m","retryable":false,"details":{}},)"
      R"("meta":{"protocol_version":"2.0","schema_id":"blob-royale://protocol/v2/snapshot-message",)"
      R"("request_id":"r","message_sequence":2,"sent_at_utc":"2026-09-06T18:04:17.750Z"}})";

  CHECK(protocol::check_v2_server_frame(kDataAndError) ==
        protocol::V2FrameConformance::kDataAndErrorExclusivityViolated);
}

TEST_CASE("Conformance rejects a frame carrying neither data nor error",
          "[unit][protocol][v2][conformance][rejection]") {
  constexpr std::string_view kNeither =
      R"({"data":null,"error":null,)"
      R"("meta":{"protocol_version":"2.0","schema_id":"blob-royale://protocol/v2/snapshot-message",)"
      R"("request_id":"r","message_sequence":2,"sent_at_utc":"2026-09-06T18:04:17.750Z"}})";

  CHECK(protocol::check_v2_server_frame(kNeither) ==
        protocol::V2FrameConformance::kDataAndErrorExclusivityViolated);
}

TEST_CASE("Conformance rejects a snapshot carrying an unregistered component kind",
          "[unit][protocol][v2][conformance][rejection]") {
  constexpr std::string_view kUnknownComponentKind =
      R"({"data":{"tick_sequence":1,"entities":[{"entity_id":1,"components":)"
      R"({"blob_shape":{"sides":5}}}],"match":{"mode":"royale","phase":"lobby",)"
      R"("phase_started_tick":1,"outcome":{"kind":"none","winner_entity_id":null,)"
      R"("winner_team_id":null},"placements":[],"mode_state":)"
      R"({"schema_id":"blob-royale://protocol/v2/mode-state/none","value":{}}}},"error":null,)"
      R"("meta":{"protocol_version":"2.0","schema_id":"blob-royale://protocol/v2/snapshot-message",)"
      R"("request_id":"r","message_sequence":2,"sent_at_utc":"2026-09-06T18:04:17.750Z"}})";

  CHECK(protocol::check_v2_server_frame(kUnknownComponentKind) ==
        protocol::V2FrameConformance::kComponentKindUnregistered);
}

TEST_CASE("Conformance rejects a snapshot whose entity ids are not ascending and distinct",
          "[unit][protocol][v2][conformance][rejection]") {
  constexpr std::string_view kDescending =
      R"({"data":{"tick_sequence":1,"entities":[)"
      R"({"entity_id":9,"components":{"score":{"points":0}}},)"
      R"({"entity_id":2,"components":{"score":{"points":0}}}],)"
      R"("match":{"mode":"royale","phase":"lobby","phase_started_tick":1,)"
      R"("outcome":{"kind":"none","winner_entity_id":null,"winner_team_id":null},"placements":[],)"
      R"("mode_state":{"schema_id":"blob-royale://protocol/v2/mode-state/none","value":{}}}},)"
      R"("error":null,)"
      R"("meta":{"protocol_version":"2.0","schema_id":"blob-royale://protocol/v2/snapshot-message",)"
      R"("request_id":"r","message_sequence":2,"sent_at_utc":"2026-09-06T18:04:17.750Z"}})";

  CHECK(protocol::check_v2_server_frame(kDescending) ==
        protocol::V2FrameConformance::kEntitiesNotAscending);
}

TEST_CASE("Conformance rejects a published entity carrying no component",
          "[unit][protocol][v2][conformance][rejection]") {
  constexpr std::string_view kEmptyComponents =
      R"({"data":{"tick_sequence":1,"entities":[{"entity_id":1,"components":{}}],)"
      R"("match":{"mode":"royale","phase":"lobby","phase_started_tick":1,)"
      R"("outcome":{"kind":"none","winner_entity_id":null,"winner_team_id":null},"placements":[],)"
      R"("mode_state":{"schema_id":"blob-royale://protocol/v2/mode-state/none","value":{}}}},)"
      R"("error":null,)"
      R"("meta":{"protocol_version":"2.0","schema_id":"blob-royale://protocol/v2/snapshot-message",)"
      R"("request_id":"r","message_sequence":2,"sent_at_utc":"2026-09-06T18:04:17.750Z"}})";

  CHECK(protocol::check_v2_server_frame(kEmptyComponents) ==
        protocol::V2FrameConformance::kEntityWithoutComponents);
}

TEST_CASE("Conformance rejects a frame naming a protocol version this schema set does not pin",
          "[unit][protocol][v2][conformance][rejection]") {
  constexpr std::string_view kMinorAhead =
      R"({"data":{"entity_id":7,"controller_id":3,"display_name":"Cole Shaffer","mode":"royale",)"
      R"("map":"arena-960x640","accepted_command_kinds":["set_thrust"]},"error":null,)"
      R"("meta":{"protocol_version":"2.1","schema_id":"blob-royale://protocol/v2/welcome-message",)"
      R"("request_id":"r","message_sequence":1,"sent_at_utc":"2026-09-06T18:04:11.500Z"}})";

  CHECK(protocol::check_v2_server_frame(kMinorAhead) ==
        protocol::V2FrameConformance::kProtocolVersionUnsupported);
}

TEST_CASE("Conformance rejects a snapshot delivered as message one",
          "[unit][protocol][v2][conformance][rejection]") {
  constexpr std::string_view kSnapshotAsFirstFrame =
      R"({"data":{"tick_sequence":1,"entities":[],)"
      R"("match":{"mode":"royale","phase":"lobby","phase_started_tick":1,)"
      R"("outcome":{"kind":"none","winner_entity_id":null,"winner_team_id":null},"placements":[],)"
      R"("mode_state":{"schema_id":"blob-royale://protocol/v2/mode-state/none","value":{}}}},)"
      R"("error":null,)"
      R"("meta":{"protocol_version":"2.0","schema_id":"blob-royale://protocol/v2/snapshot-message",)"
      R"("request_id":"r","message_sequence":1,"sent_at_utc":"2026-09-06T18:04:17.750Z"}})";

  CHECK(protocol::check_v2_server_frame(kSnapshotAsFirstFrame) ==
        protocol::V2FrameConformance::kMessageSequenceInvalid);
}

TEST_CASE("Conformance rejects a snapshot naming an unregistered mode-state schema id",
          "[unit][protocol][v2][conformance][rejection]") {
  constexpr std::string_view kUnknownModeState =
      R"({"data":{"tick_sequence":1,"entities":[],)"
      R"("match":{"mode":"royale","phase":"lobby","phase_started_tick":1,)"
      R"("outcome":{"kind":"none","winner_entity_id":null,"winner_team_id":null},"placements":[],)"
      R"("mode_state":{"schema_id":"blob-royale://protocol/v2/mode-state/capture","value":{}}}},)"
      R"("error":null,)"
      R"("meta":{"protocol_version":"2.0","schema_id":"blob-royale://protocol/v2/snapshot-message",)"
      R"("request_id":"r","message_sequence":2,"sent_at_utc":"2026-09-06T18:04:17.750Z"}})";

  CHECK(protocol::check_v2_server_frame(kUnknownModeState) ==
        protocol::V2FrameConformance::kModeStateSchemaIdUnregistered);
}

TEST_CASE("The closed v2 component vocabulary names exactly the registered component kinds",
          "[unit][protocol][v2][vocabulary]") {
  std::vector<std::string_view> registered_names;
  simulation::ComponentRegistry::for_each_kind([&registered_names]<typename Component>() {
    registered_names.push_back(simulation::component_kind_name<Component>);
  });

  REQUIRE(registered_names.size() == protocol::kV2ComponentKindNames.size());
  for (const std::string_view name : registered_names) {
    CHECK(protocol::is_v2_component_kind(name));
  }
  CHECK_FALSE(protocol::is_v2_component_kind("blob_shape"));
  CHECK_FALSE(protocol::is_v2_component_kind(""));
}

TEST_CASE("Snapshot v2 encoder rejects a message sequence below the first snapshot's",
          "[unit][protocol][v2][encoding][rejection]") {
  const fixture::StubControllerDirectory directory = fixture::golden_directory();
  fixture::require_protocol_error_code(
      [&directory] {
        return protocol::encode_snapshot_message_v2(fixture::golden_snapshot(), directory,
                                                    fixture::session_request_id(), 1,
                                                    fixture::kSnapshotTimestamp);
      },
      protocol::ProtocolEncodingErrorCode::kMessageSequenceOutOfRange);
}

TEST_CASE("Snapshot v2 encoder rejects a malformed UTC timestamp",
          "[unit][protocol][v2][encoding][rejection]") {
  const fixture::StubControllerDirectory directory = fixture::golden_directory();
  fixture::require_protocol_error_code(
      [&directory] {
        return protocol::encode_snapshot_message_v2(
            fixture::golden_snapshot(), directory, fixture::session_request_id(),
            fixture::kSnapshotMessageSequence, "2026-09-06 18:04:17Z");
      },
      protocol::ProtocolEncodingErrorCode::kTimestampInvalid);
}

TEST_CASE("Snapshot v2 encoder rejects a complete frame above the configured byte limit",
          "[unit][protocol][v2][encoding][rejection]") {
  const fixture::StubControllerDirectory directory = fixture::golden_directory();
  fixture::require_protocol_error_code(
      [&directory] {
        return protocol::encode_snapshot_message_v2(
            fixture::golden_snapshot(), directory, fixture::session_request_id(),
            fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp, 512);
      },
      protocol::ProtocolEncodingErrorCode::kEncodedPayloadTooLarge);
}

TEST_CASE("Snapshot v2 encoder rejects a placement whose controller the directory cannot name",
          "[unit][protocol][v2][encoding][rejection]") {
  fixture::StubControllerDirectory directory = fixture::golden_directory();
  directory.forget_placed_controller(fixture::kPlacedEntityId);

  fixture::require_protocol_error_code(
      [&directory] {
        return protocol::encode_snapshot_message_v2(
            fixture::golden_snapshot(), directory, fixture::session_request_id(),
            fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp);
      },
      protocol::ProtocolEncodingErrorCode::kPlacementControllerUnknown);
}

TEST_CASE("Snapshot v2 encoder rejects entity ids that are not ascending and distinct",
          "[unit][protocol][v2][encoding][rejection]") {
  const std::vector<simulation::EntityId> descending{simulation::EntityId::create(9),
                                                     simulation::EntityId::create(2)};
  const std::vector<simulation::EntityId> duplicated{simulation::EntityId::create(4),
                                                     simulation::EntityId::create(4)};

  fixture::require_protocol_error_code(
      [&descending] {
        protocol::validate_ascending_unique_entities(descending, "snapshot.entities.entity_id");
      },
      protocol::ProtocolEncodingErrorCode::kSnapshotEntityOrderInvalid);
  fixture::require_protocol_error_code(
      [&duplicated] {
        protocol::validate_ascending_unique_entities(duplicated, "snapshot.entities.entity_id");
      },
      protocol::ProtocolEncodingErrorCode::kSnapshotEntityOrderInvalid);
}

TEST_CASE("Snapshot v2 encoder publishes the documented fallback for a closed controller",
          "[unit][protocol][v2][encoding]") {
  fixture::StubControllerDirectory directory = fixture::golden_directory();
  directory.forget_controller(fixture::kPlayerControllerId);

  const std::string encoded = protocol::encode_snapshot_message_v2(
      fixture::golden_snapshot(), directory, fixture::session_request_id(),
      fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp);

  CHECK(encoded.find(R"("controller_kind":"unknown","display_name":"player-7")") !=
        std::string::npos);
  CHECK(encoded.find(R"("display_name":"Cole Shaffer")") == std::string::npos);
  CHECK(protocol::check_v2_server_frame(encoded) == protocol::V2FrameConformance::kConforms);
}

TEST_CASE("Own-body resolution finds the entity a controller drives and no other",
          "[unit][protocol][v2][encoding]") {
  const std::optional<simulation::EntityId> player = protocol::find_controlled_body(
      fixture::golden_snapshot(), simulation::ControllerId::create(fixture::kPlayerControllerId));
  const std::optional<simulation::EntityId> bot = protocol::find_controlled_body(
      fixture::golden_snapshot(), simulation::ControllerId::create(fixture::kBotControllerId));
  const std::optional<simulation::EntityId> eliminated = protocol::find_controlled_body(
      fixture::golden_snapshot(), simulation::ControllerId::create(fixture::kPlacedControllerId));

  REQUIRE(player.has_value());
  CHECK(player->value() == fixture::kPlayerEntityId);
  REQUIRE(bot.has_value());
  CHECK(bot->value() == fixture::kBotEntityId);
  CHECK_FALSE(eliminated.has_value());
}

TEST_CASE("Welcome value rejects a display name outside the accepted grammar",
          "[unit][protocol][v2][encoding][rejection]") {
  const auto welcome_with_display_name = [](const std::string& display_name) {
    return [display_name] {
      return protocol::SessionWelcome::create(
          simulation::EntityId::create(7), simulation::ControllerId::create(3), display_name,
          "royale", "arena-960x640",
          simulation::CommandKindMask::create({simulation::CommandKind::kThrust}));
    };
  };

  fixture::require_protocol_error_code(welcome_with_display_name("Cole\nShaffer"),
                                       protocol::ProtocolEncodingErrorCode::kSessionWelcomeInvalid);
  fixture::require_protocol_error_code(welcome_with_display_name(" Cole"),
                                       protocol::ProtocolEncodingErrorCode::kSessionWelcomeInvalid);
  fixture::require_protocol_error_code(welcome_with_display_name(""),
                                       protocol::ProtocolEncodingErrorCode::kSessionWelcomeInvalid);
  fixture::require_protocol_error_code(welcome_with_display_name(std::string(65, 'a')),
                                       protocol::ProtocolEncodingErrorCode::kSessionWelcomeInvalid);
}

TEST_CASE("Welcome advertises only client-sendable kinds the mode accepts",
          "[unit][protocol][v2][encoding]") {
  const protocol::SessionWelcome every_kind = protocol::SessionWelcome::create(
      simulation::EntityId::create(7), simulation::ControllerId::create(3), "Cole Shaffer",
      "royale", "arena-960x640", simulation::CommandKindMask::all());
  const protocol::SessionWelcome no_kind = protocol::SessionWelcome::create(
      simulation::EntityId::create(7), simulation::ControllerId::create(3), "Cole Shaffer",
      "sandbox", "arena-960x640", simulation::CommandKindMask::none());

  const std::string advertised_all = protocol::encode_welcome_message(
      every_kind, fixture::session_request_id(), fixture::kWelcomeTimestamp);
  const std::string advertised_none = protocol::encode_welcome_message(
      no_kind, fixture::session_request_id(), fixture::kWelcomeTimestamp);

  CHECK(advertised_all.find(R"("accepted_command_kinds":["set_thrust"])") != std::string::npos);
  CHECK(advertised_all.find("spawn") == std::string::npos);
  CHECK(advertised_all.find("despawn") == std::string::npos);
  CHECK(advertised_none.find(R"("accepted_command_kinds":[])") != std::string::npos);
}
