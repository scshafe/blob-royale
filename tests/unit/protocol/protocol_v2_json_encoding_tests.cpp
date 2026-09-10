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
#include "mode_state_wire_encoding.hpp"
#include "simulation_limits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
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
      R"({"data":{"entity_id":7,"controller_id":3,"display_name":"Cole Shaffer","mode":"royale","map":"arena-960x640","accepted_command_kinds":["clear_seat","seat_npc","set_seat_count","set_thrust","start_match"],"npc_controller_kinds":["wanderer","chaser"],"lobby_id":1,"seat_count_maximum":32},"error":null,"meta":{"protocol_version":"2.5","schema_id":"blob-royale://protocol/v2/welcome-message","request_id":"018f47a4-9c21-7f10-8a55-4b7d1e0c33a2","message_sequence":1,"sent_at_utc":"2026-09-06T18:04:11.500Z"}})");
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
      R"({"data":{"tick_sequence":12904,"entities":[{"entity_id":1,"components":{"physics_body":{"position":{"x":480,"y":160},"velocity":{"x":0,"y":0},"acceleration":{"x":0,"y":0},"radius":40,"mass":0,"collision_layer":2,"collision_mask":1,"is_static":true}}},{"entity_id":7,"components":{"controllable":{"controller_id":3,"controller_kind":"session","display_name":"Cole Shaffer"},"physics_body":{"position":{"x":4.125E2,"y":2.8825E2},"velocity":{"x":1.875E1,"y":-4.25E1},"acceleration":{"x":400,"y":0},"radius":10,"mass":1,"collision_layer":1,"collision_mask":3,"is_static":false},"zone_exposure":{"outside_ticks":0}}},{"entity_id":8,"components":{"controllable":{"controller_id":4,"controller_kind":"wanderer","display_name":"wanderer-1"},"physics_body":{"position":{"x":7.605E2,"y":5.1225E2},"velocity":{"x":-6.25E0,"y":3.15E1},"acceleration":{"x":0,"y":-400},"radius":10,"mass":1,"collision_layer":1,"collision_mask":3,"is_static":false},"zone_exposure":{"outside_ticks":214}}},{"entity_id":9,"components":{"zone":{"center":{"x":480,"y":320},"radius":2.105E2}}}],"match":{"mode":"royale","phase":"running","phase_started_tick":10904,"seats":[{"kind":"controller","controller_id":3,"npc_kind":null},{"kind":"npc","controller_id":12,"npc_kind":"wanderer"},{"kind":"npc","controller_id":null,"npc_kind":"chaser"},{"kind":"empty","controller_id":null,"npc_kind":null}],"start_requested":true,"outcome":{"kind":"none","winner_entity_id":null,"winner_team_id":null},"placements":[{"entity_id":5,"controller_id":6,"placement":3,"eliminated_tick":12400}],"mode_state":{"schema_id":"blob-royale://protocol/v2/mode-state/royale","value":{"previous_phase":"running","elimination_grace_ticks":1200}}}},"error":null,"meta":{"protocol_version":"2.5","schema_id":"blob-royale://protocol/v2/snapshot-message","request_id":"018f47a4-9c21-7f10-8a55-4b7d1e0c33a2","message_sequence":129,"sent_at_utc":"2026-09-06T18:04:17.750Z"}})");
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
      R"({"data":null,"error":{"code":"PROTOCOL.INVALID_FORWARDED_CLIENT","message":"A proxy-forwarded connection must present exactly one canonical forwarded client address.","retryable":false,"details":{"forwarded_client_reason":"multiple_values"}},"meta":{"protocol_version":"2.5","schema_id":"blob-royale://protocol/v2/error-response","request_id":"018f47a4-9c21-7f10-8a55-4b7d1e0c33a2"}})");
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
  CHECK(encoded.find(R"("protocol_version":"2.5")") != std::string::npos);
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
      R"("meta":{"protocol_version":"2.5","schema_id":"blob-royale://protocol/v2/snapshot-message",)"
      R"("request_id":"r","message_sequence":2,"sent_at_utc":"2026-09-06T18:04:17.750Z"}})";

  CHECK(protocol::check_v2_server_frame(kDataAndError) ==
        protocol::V2FrameConformance::kDataAndErrorExclusivityViolated);
}

TEST_CASE("Conformance rejects a frame carrying neither data nor error",
          "[unit][protocol][v2][conformance][rejection]") {
  constexpr std::string_view kNeither =
      R"({"data":null,"error":null,)"
      R"("meta":{"protocol_version":"2.5","schema_id":"blob-royale://protocol/v2/snapshot-message",)"
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
      R"("meta":{"protocol_version":"2.5","schema_id":"blob-royale://protocol/v2/snapshot-message",)"
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
      R"("meta":{"protocol_version":"2.5","schema_id":"blob-royale://protocol/v2/snapshot-message",)"
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
      R"("meta":{"protocol_version":"2.5","schema_id":"blob-royale://protocol/v2/snapshot-message",)"
      R"("request_id":"r","message_sequence":2,"sent_at_utc":"2026-09-06T18:04:17.750Z"}})";

  CHECK(protocol::check_v2_server_frame(kEmptyComponents) ==
        protocol::V2FrameConformance::kEntityWithoutComponents);
}

TEST_CASE("Conformance rejects a frame naming a protocol version this schema set does not pin",
          "[unit][protocol][v2][conformance][rejection]") {
  // One minor ahead of whatever this build pins. It moved 2.1 -> 2.2 when `lethal_on_contact` was
  // published, 2.2 -> 2.3 when the royale mode-state block gained `elimination_grace_ticks`,
  // 2.3 -> 2.4 when the lobby command kinds and the match seat roster landed, 2.4 -> 2.5 when
  // the lobby directory and the welcome's room landed, and 2.5 -> 2.6 when `respawn_timer` opened
  // 2.5, because a case named "a version this schema set does not pin" that names the pinned one
  // tests nothing.
  constexpr std::string_view kMinorAhead =
      R"({"data":{"entity_id":7,"controller_id":3,"display_name":"Cole Shaffer","mode":"royale",)"
      R"("map":"arena-960x640","accepted_command_kinds":["set_thrust"],)"
      R"("npc_controller_kinds":[],"lobby_id":1,"seat_count_maximum":32},"error":null,)"
      R"("meta":{"protocol_version":"2.6","schema_id":"blob-royale://protocol/v2/welcome-message",)"
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
      R"("meta":{"protocol_version":"2.5","schema_id":"blob-royale://protocol/v2/snapshot-message",)"
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
      R"("meta":{"protocol_version":"2.5","schema_id":"blob-royale://protocol/v2/snapshot-message",)"
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
  CHECK(std::ranges::is_sorted(protocol::kV2ComponentKindNames));
}

TEST_CASE("Race progress is published without a body and matches the accepted component example",
          "[unit][protocol][v2][encoding][race_progress][golden]") {
  const simulation::WorldSnapshot snapshot =
      fixture::race_progress_snapshot(fixture::kGoldenNextCheckpoint);
  const auto& progress = snapshot.components<simulation::RaceProgress>();
  REQUIRE(progress.size() == 1);
  CHECK(progress[0].value.next_checkpoint == fixture::kGoldenNextCheckpoint);
  const std::string encoded = protocol::encode_snapshot_message_v2(
      snapshot, fixture::golden_directory(), fixture::session_request_id(),
      fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp);
  CHECK(encoded.find(R"("components":{"race_progress":{"next_checkpoint":2}})") !=
        std::string::npos);
  const boost::json::value document = boost::json::parse(encoded);
  const boost::json::value& component = document.as_object()
                                            .at("data")
                                            .as_object()
                                            .at("entities")
                                            .as_array()
                                            .at(0)
                                            .as_object()
                                            .at("components")
                                            .as_object()
                                            .at("race_progress");
  CHECK(component ==
        boost::json::parse(fixture::read_v2_golden_example("race-progress-component.json")));
  CHECK(protocol::check_v2_server_frame(encoded) == protocol::V2FrameConformance::kConforms);
}

TEST_CASE("Race progress preserves zero and is never synthesized for an entity without it",
          "[unit][protocol][v2][encoding][race_progress]") {
  const std::string starting = protocol::encode_snapshot_message_v2(
      fixture::race_progress_snapshot(0), fixture::golden_directory(),
      fixture::session_request_id(), fixture::kSnapshotMessageSequence,
      fixture::kSnapshotTimestamp);
  CHECK(starting.find(R"("race_progress":{"next_checkpoint":0})") != std::string::npos);
  const std::string absent = protocol::encode_snapshot_message_v2(
      fixture::untransitioned_lobby_snapshot(), fixture::golden_directory(),
      fixture::session_request_id(), fixture::kSnapshotMessageSequence,
      fixture::kSnapshotTimestamp);
  CHECK(absent.find("race_progress") == std::string::npos);
}

TEST_CASE("Hill publication preserves the exact configured radius and score ceilings",
          "[unit][protocol][v2][encoding][king_of_the_hill][boundary]") {
  STATIC_REQUIRE(simulation::kMaximumPhysicalComponentMagnitude ==
                 protocol::kMaximumFiniteWorldScalar);
  STATIC_REQUIRE(simulation::kMaximumProtocolSafeInteger == protocol::kMaximumSafeInteger);
  const std::string encoded = protocol::encode_snapshot_message_v2(
      fixture::hill_mode_snapshot(simulation::kMaximumPhysicalComponentMagnitude,
                                  simulation::kMaximumProtocolSafeInteger),
      fixture::golden_directory(), fixture::session_request_id(), fixture::kSnapshotMessageSequence,
      fixture::kSnapshotTimestamp);
  const boost::json::value document = boost::json::parse(encoded);
  const boost::json::object& data = document.as_object().at("data").as_object();
  const boost::json::object& hill = data.at("entities")
                                        .as_array()
                                        .at(0)
                                        .as_object()
                                        .at("components")
                                        .as_object()
                                        .at("hill")
                                        .as_object();
  CHECK(hill.at("radius").as_int64() ==
        static_cast<std::int64_t>(simulation::kMaximumPhysicalComponentMagnitude));
  const boost::json::object& state = data.at("match").as_object().at("mode_state").as_object();
  CHECK(state.at("schema_id").as_string() == protocol::kKingOfTheHillModeStateSchemaId);
  CHECK(state.at("value").as_object().at("points_to_win").as_int64() ==
        static_cast<std::int64_t>(simulation::kMaximumProtocolSafeInteger));
  CHECK(protocol::check_v2_server_frame(encoded) == protocol::V2FrameConformance::kConforms);
}

TEST_CASE("Race mode state publishes its course and shared standings in canonical order",
          "[unit][protocol][v2][encoding][race][golden]") {
  const simulation::WorldSnapshot snapshot =
      fixture::race_mode_snapshot(fixture::golden_race_mode_state());
  const std::string encoded = protocol::encode_snapshot_message_v2(
      snapshot, fixture::golden_directory(), fixture::session_request_id(),
      fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp);
  const boost::json::value document = boost::json::parse(encoded);
  const boost::json::object& match =
      document.as_object().at("data").as_object().at("match").as_object();
  const boost::json::object& block = match.at("mode_state").as_object();
  CHECK(block.at("schema_id").as_string() == protocol::kRaceModeStateSchemaId);
  CHECK(block.at("value") ==
        boost::json::parse(fixture::read_v2_golden_example("race-mode-state.json")));
  CHECK(match.at("placements").as_array().empty());
  CHECK(simulation::mode_match_state_schema_id_of(snapshot.match().mode_state()) == "race");
  CHECK(
      encoded.find(
          R"("track_half_width":60,"checkpoint_radius":20,"track":[{"x":100,"y":100},{"x":700,"y":100},{"x":700,"y":500}],"checkpoints":[{"x":300,"y":100},{"x":700,"y":200},{"x":700,"y":500}],"time_limit_ticks":96000,"finish_window_ticks":2000,"standings":[{"entity_id":7,"controller_id":3,"placement":1,"finished_tick":1},{"entity_id":8,"controller_id":4,"placement":1,"finished_tick":1}])") !=
      std::string::npos);
  CHECK(encoded == protocol::encode_snapshot_message_v2(
                       snapshot, fixture::golden_directory(), fixture::session_request_id(),
                       fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp));
  CHECK(protocol::check_v2_server_frame(encoded) == protocol::V2FrameConformance::kConforms);
}

TEST_CASE("Race publishes an empty standings array and canonicalizes nested vector numbers",
          "[unit][protocol][v2][encoding][race]") {
  simulation::RaceModeState state = fixture::golden_race_mode_state();
  state.standings.clear();
  state.track[0] = simulation::Vector2::create(-0.0, 100.0);
  const std::string encoded = protocol::encode_snapshot_message_v2(
      fixture::race_mode_snapshot(std::move(state)), fixture::golden_directory(),
      fixture::session_request_id(), fixture::kSnapshotMessageSequence,
      fixture::kSnapshotTimestamp);
  CHECK(encoded.find(R"("track":[{"x":0,"y":100})") != std::string::npos);
  CHECK(encoded.find(R"("standings":[])") != std::string::npos);
  CHECK(encoded.find(R"("placements":[])") != std::string::npos);
}

TEST_CASE("Race publication preserves both exact configured dimension ceilings",
          "[unit][protocol][v2][encoding][race][boundary]") {
  STATIC_REQUIRE(simulation::kMaximumPhysicalComponentMagnitude ==
                 protocol::kMaximumFiniteWorldScalar);
  simulation::RaceModeState state = fixture::golden_race_mode_state();
  state.track_half_width = simulation::kMaximumPhysicalComponentMagnitude;
  state.checkpoint_radius = simulation::kMaximumPhysicalComponentMagnitude;
  const std::string encoded = protocol::encode_snapshot_message_v2(
      fixture::race_mode_snapshot(std::move(state)), fixture::golden_directory(),
      fixture::session_request_id(), fixture::kSnapshotMessageSequence,
      fixture::kSnapshotTimestamp);
  const boost::json::value document = boost::json::parse(encoded);
  const boost::json::object& block = document.as_object()
                                         .at("data")
                                         .as_object()
                                         .at("match")
                                         .as_object()
                                         .at("mode_state")
                                         .as_object()
                                         .at("value")
                                         .as_object();
  CHECK(block.at("track_half_width").as_int64() ==
        static_cast<std::int64_t>(simulation::kMaximumPhysicalComponentMagnitude));
  CHECK(block.at("checkpoint_radius").as_int64() ==
        static_cast<std::int64_t>(simulation::kMaximumPhysicalComponentMagnitude));
  CHECK(protocol::check_v2_server_frame(encoded) == protocol::V2FrameConformance::kConforms);
}

TEST_CASE("Race mode state rejects out-of-schema scalars and nested standing fields",
          "[unit][protocol][v2][encoding][race][rejection]") {
  simulation::RaceModeState state = fixture::golden_race_mode_state();
  protocol::ProtocolEncodingErrorCode expected =
      protocol::ProtocolEncodingErrorCode::kComponentValueOutOfRange;
  SECTION("zero corridor") { state.track_half_width = 0.0; }
  SECTION("nonfinite corridor") {
    state.track_half_width = std::numeric_limits<double>::infinity();
  }
  SECTION("oversized corridor") {
    state.track_half_width = protocol::kMaximumFiniteWorldScalar + 1.0;
  }
  SECTION("zero gate radius") { state.checkpoint_radius = 0.0; }
  SECTION("gate wider than corridor") { state.checkpoint_radius = state.track_half_width + 1.0; }
  SECTION("one track point") { state.track.erase(state.track.begin() + 1, state.track.end()); }
  SECTION("too many track points") {
    state.track.resize(protocol::kRaceCoursePointLimit + 1, state.track.front());
  }
  SECTION("no checkpoints") { state.checkpoints.clear(); }
  SECTION("too many checkpoints") {
    state.checkpoints.resize(protocol::kRaceCoursePointLimit + 1, state.checkpoints.front());
  }
  SECTION("unsafe time limit") { state.time_limit_ticks = protocol::kMaximumSafeInteger + 1; }
  SECTION("unsafe finish window") { state.finish_window_ticks = protocol::kMaximumSafeInteger + 1; }
  SECTION("zero placement") { state.standings[0].placement = 0; }
  SECTION("placement above the ranking bound") {
    state.standings[0].placement = protocol::kMatchPlacementLimit + 1;
  }
  SECTION("zero finish tick") {
    state.standings[0].finished_tick = simulation::TickSequence::zero();
    expected = protocol::ProtocolEncodingErrorCode::kSnapshotTickOutOfRange;
  }
  SECTION("too many standings") {
    state.standings.resize(protocol::kMatchPlacementLimit + 1, state.standings.front());
    expected = protocol::ProtocolEncodingErrorCode::kPlacementLimitExceeded;
  }
  fixture::require_protocol_error_code(
      [&state] {
        return protocol::encode_snapshot_message_v2(
            fixture::race_mode_snapshot(std::move(state)), fixture::golden_directory(),
            fixture::session_request_id(), fixture::kSnapshotMessageSequence,
            fixture::kSnapshotTimestamp);
      },
      expected);
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

TEST_CASE("Snapshot v2 encoder publishes a phase_started_tick of zero for an untransitioned lobby",
          "[unit][protocol][v2][encoding]") {
  // `match-data.schema.json` types this one member as `phase_start_tick`, which admits zero, while
  // every other tick-valued member is a `tick_sequence` with a minimum of one. Zero is the truthful
  // value for "no transition has been committed yet": a match begins in `lobby` at load, before
  // tick 1 exists, and every snapshot of that lobby is a legitimate frame.
  const fixture::StubControllerDirectory directory = fixture::golden_directory();
  REQUIRE(fixture::untransitioned_lobby_snapshot().match().phase_started_tick() ==
          simulation::TickSequence::zero());

  const std::string encoded = protocol::encode_snapshot_message_v2(
      fixture::untransitioned_lobby_snapshot(), directory, fixture::session_request_id(),
      fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp);

  CHECK(encoded.find(R"("phase":"lobby","phase_started_tick":0)") != std::string::npos);
  CHECK(protocol::check_v2_server_frame(encoded) == protocol::V2FrameConformance::kConforms);
}

TEST_CASE("Snapshot v2 encoder publishes a placement controller the directory has forgotten",
          "[unit][protocol][v2][encoding]") {
  // The placed entity was destroyed on the tick that recorded it and its session has closed, so
  // `golden_directory()` holds no entry for `kPlacedControllerId`. The encoder still publishes the
  // link, because `simulation::RoyalePlacement` recorded it at elimination: this is the whole
  // reason the placement carries a `ControllerId` instead of the encoder asking a directory.
  const fixture::StubControllerDirectory directory = fixture::golden_directory();
  REQUIRE_FALSE(
      directory.find_controller(simulation::ControllerId::create(fixture::kPlacedControllerId))
          .has_value());

  const std::string encoded = protocol::encode_snapshot_message_v2(
      fixture::golden_snapshot(), directory, fixture::session_request_id(),
      fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp);

  CHECK(encoded.find(R"("entity_id":5,"controller_id":6,"placement":3)") != std::string::npos);
  CHECK(protocol::check_v2_server_frame(encoded) == protocol::V2FrameConformance::kConforms);
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
          simulation::CommandKindMask::create({simulation::CommandKind::kThrust}),
          fixture::golden_npc_controller_kinds(), fixture::kGoldenLobbyId,
          fixture::kGoldenSeatCountMaximum);
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
      "royale", "arena-960x640", simulation::CommandKindMask::all(),
      fixture::golden_npc_controller_kinds(), fixture::kGoldenLobbyId,
      fixture::kGoldenSeatCountMaximum);
  const protocol::SessionWelcome no_kind = protocol::SessionWelcome::create(
      simulation::EntityId::create(7), simulation::ControllerId::create(3), "Cole Shaffer",
      "sandbox", "arena-960x640", simulation::CommandKindMask::none(),
      fixture::golden_npc_controller_kinds(), fixture::kGoldenLobbyId,
      fixture::kGoldenSeatCountMaximum);

  const std::string advertised_all = protocol::encode_welcome_message(
      every_kind, fixture::session_request_id(), fixture::kWelcomeTimestamp);
  const std::string advertised_none = protocol::encode_welcome_message(
      no_kind, fixture::session_request_id(), fixture::kWelcomeTimestamp);

  // In the published vocabulary's own order, not the engine's declaration order, so a client reads
  // the array in the order its schema enumerates.
  CHECK(
      advertised_all.find(
          R"("accepted_command_kinds":["clear_seat","seat_npc","set_seat_count","set_thrust","start_match"])") !=
      std::string::npos);
  CHECK(advertised_all.find("spawn") == std::string::npos);
  CHECK(advertised_all.find("despawn") == std::string::npos);
  CHECK(advertised_none.find(R"("accepted_command_kinds":[])") != std::string::npos);

  // A mask naming only `set_thrust` -- sandbox's -- advertises exactly one kind, so the four lobby
  // controls a client would draw are absent from its welcome rather than drawn and inert.
  const protocol::SessionWelcome thrust_only = protocol::SessionWelcome::create(
      simulation::EntityId::create(7), simulation::ControllerId::create(3), "Cole Shaffer",
      "sandbox", "arena-960x640",
      simulation::CommandKindMask::create({simulation::CommandKind::kSpawn,
                                           simulation::CommandKind::kDespawn,
                                           simulation::CommandKind::kThrust}),
      fixture::golden_npc_controller_kinds(), fixture::kGoldenLobbyId,
      fixture::kGoldenSeatCountMaximum);
  CHECK(protocol::encode_welcome_message(thrust_only, fixture::session_request_id(),
                                         fixture::kWelcomeTimestamp)
            .find(R"("accepted_command_kinds":["set_thrust"])") != std::string::npos);
}

TEST_CASE("Welcome publishes the NPC kinds the registry declared, in registry order",
          "[unit][protocol][v2][encoding][lobby]") {
  // **The acceptance test for "registering a bot costs no client change", on the publishing side.**
  // The list is data the composition root read from `ControllerRegistry`, so a build with one more
  // registered bot publishes one more name through the same encoder, the same schema, and the same
  // client -- with no edit to any of the three.
  const auto welcome_publishing = [](std::vector<std::string> npc_controller_kinds) {
    return protocol::SessionWelcome::create(
        simulation::EntityId::create(7), simulation::ControllerId::create(3), "Cole Shaffer",
        "royale", "arena-960x640", simulation::CommandKindMask::all(),
        std::move(npc_controller_kinds), fixture::kGoldenLobbyId, fixture::kGoldenSeatCountMaximum);
  };

  const std::string two_bots =
      protocol::encode_welcome_message(welcome_publishing({"wanderer", "chaser"}),
                                       fixture::session_request_id(), fixture::kWelcomeTimestamp);
  CHECK(two_bots.find(R"("npc_controller_kinds":["wanderer","chaser"])") != std::string::npos);

  const std::string three_bots =
      protocol::encode_welcome_message(welcome_publishing({"wanderer", "chaser", "ambusher"}),
                                       fixture::session_request_id(), fixture::kWelcomeTimestamp);
  CHECK(three_bots.find(R"("npc_controller_kinds":["wanderer","chaser","ambusher"])") !=
        std::string::npos);

  // Registry order is preserved rather than sorted: the table's order is somebody's ordering of the
  // bots and a client that re-sorted it would be inventing a different one.
  const std::string reversed =
      protocol::encode_welcome_message(welcome_publishing({"chaser", "wanderer"}),
                                       fixture::session_request_id(), fixture::kWelcomeTimestamp);
  CHECK(reversed.find(R"("npc_controller_kinds":["chaser","wanderer"])") != std::string::npos);

  // A build with no registered bot publishes an empty array, which is the honest statement that no
  // seat can be filled with one -- not an omitted member a client has to guess at.
  const std::string no_bots = protocol::encode_welcome_message(
      welcome_publishing({}), fixture::session_request_id(), fixture::kWelcomeTimestamp);
  CHECK(no_bots.find(R"("npc_controller_kinds":[])") != std::string::npos);
}

TEST_CASE("Welcome refuses a registered controller kind it could not publish",
          "[unit][protocol][v2][encoding][rejection]") {
  // A registry row whose name is not a `kind_name` is a build-time mistake, and it fails at the
  // value rather than in the encoder: the encoder never has to decide what to do with a name it
  // cannot write.
  const auto welcome_publishing = [](std::vector<std::string> npc_controller_kinds) {
    return [npc_controller_kinds = std::move(npc_controller_kinds)] {
      return protocol::SessionWelcome::create(
          simulation::EntityId::create(7), simulation::ControllerId::create(3), "Cole Shaffer",
          "royale", "arena-960x640", simulation::CommandKindMask::all(), npc_controller_kinds,
          fixture::kGoldenLobbyId, fixture::kGoldenSeatCountMaximum);
    };
  };

  fixture::require_protocol_error_code(welcome_publishing({"Wanderer"}),
                                       protocol::ProtocolEncodingErrorCode::kSessionWelcomeInvalid);
  fixture::require_protocol_error_code(welcome_publishing({"wanderer", ""}),
                                       protocol::ProtocolEncodingErrorCode::kSessionWelcomeInvalid);
  fixture::require_protocol_error_code(
      welcome_publishing(std::vector<std::string>(protocol::kNpcControllerKindLimit + 1, "a")),
      protocol::ProtocolEncodingErrorCode::kSessionWelcomeInvalid);
}

TEST_CASE("Welcome refuses a room or a seat ceiling outside the protocol's bounds",
          "[unit][protocol][v2][welcome][validation]") {
  const auto welcome_with = [](const std::uint64_t lobby_id,
                               const std::uint64_t seat_count_maximum) {
    return [lobby_id, seat_count_maximum] {
      static_cast<void>(protocol::SessionWelcome::create(
          simulation::EntityId::create(7), simulation::ControllerId::create(3), "Cole Shaffer",
          "royale", "arena-960x640", simulation::CommandKindMask::all(), {}, lobby_id,
          seat_count_maximum));
    };
  };
  CHECK_NOTHROW(welcome_with(1, 1)());
  CHECK_NOTHROW(welcome_with(protocol::kLobbyDirectoryLimit, protocol::kLobbySeatCountMaximum)());
  fixture::require_protocol_error_code(welcome_with(0, 32),
                                       protocol::ProtocolEncodingErrorCode::kSessionWelcomeInvalid);
  fixture::require_protocol_error_code(welcome_with(protocol::kLobbyDirectoryLimit + 1, 32),
                                       protocol::ProtocolEncodingErrorCode::kSessionWelcomeInvalid);
  fixture::require_protocol_error_code(welcome_with(1, 0),
                                       protocol::ProtocolEncodingErrorCode::kSessionWelcomeInvalid);
  fixture::require_protocol_error_code(welcome_with(1, protocol::kLobbySeatCountMaximum + 1),
                                       protocol::ProtocolEncodingErrorCode::kSessionWelcomeInvalid);
}

TEST_CASE("Lobby directory encoder matches the accepted golden example and canonical bytes",
          "[unit][protocol][v2][encoding][golden][lobbies]") {
  const std::vector<protocol::LobbyListing> lobbies = fixture::golden_lobby_listings();
  const std::string encoded =
      protocol::encode_lobby_directory_message(lobbies, fixture::session_request_id());

  fixture::require_json_matches_v2_golden_example(encoded, "lobby-directory-message.json");
  CHECK(
      encoded ==
      R"({"data":{"lobbies":[{"lobby_id":1,"mode":"royale","map":"arena-960x640","phase":"running","phase_started_tick":10904,"tick_sequence":12904,"seat_count":4,"seat_count_maximum":32,"filled_seat_count":2,"npc_seat_count":2,"session_count":1,"healthy":true},{"lobby_id":2,"mode":"royale","map":"arena-960x640","phase":"lobby","phase_started_tick":0,"tick_sequence":12904,"seat_count":4,"seat_count_maximum":32,"filled_seat_count":1,"npc_seat_count":1,"session_count":0,"healthy":true}]},"error":null,"meta":{"protocol_version":"2.5","schema_id":"blob-royale://protocol/v2/lobby-directory","request_id":"018f47a4-9c21-7f10-8a55-4b7d1e0c33a2"}})");
}

TEST_CASE("Lobby directory encoder fails closed on a directory it could not publish",
          "[unit][protocol][v2][encoding][lobbies][validation]") {
  const auto encode = [](std::vector<protocol::LobbyListing> lobbies) {
    return [lobbies = std::move(lobbies)] {
      static_cast<void>(
          protocol::encode_lobby_directory_message(lobbies, fixture::session_request_id()));
    };
  };
  const auto expect_invalid = [&encode](std::vector<protocol::LobbyListing> lobbies) {
    fixture::require_protocol_error_code(
        encode(std::move(lobbies)), protocol::ProtocolEncodingErrorCode::kLobbyDirectoryInvalid);
  };

  // No rooms, and more rooms than the directory limit.
  expect_invalid({});
  std::vector<protocol::LobbyListing> too_many;
  for (std::uint64_t lobby_id = 1; lobby_id <= protocol::kLobbyDirectoryLimit + 1; ++lobby_id) {
    protocol::LobbyListing listing = fixture::golden_lobby_listings()[1];
    listing.lobby_id = lobby_id;
    too_many.push_back(listing);
  }
  expect_invalid(too_many);
  // Rooms are numbered by position, so a directory that disagrees with itself is refused.
  std::vector<protocol::LobbyListing> misnumbered = fixture::golden_lobby_listings();
  misnumbered[1].lobby_id = 3;
  expect_invalid(misnumbered);
  // A count no room could have, and a phase that started after the tick it was read at.
  std::vector<protocol::LobbyListing> overfilled = fixture::golden_lobby_listings();
  overfilled[0].filled_seat_count = overfilled[0].seat_count + 1;
  expect_invalid(overfilled);
  std::vector<protocol::LobbyListing> oversized = fixture::golden_lobby_listings();
  oversized[0].seat_count = oversized[0].seat_count_maximum + 1;
  expect_invalid(oversized);
  std::vector<protocol::LobbyListing> future_phase = fixture::golden_lobby_listings();
  future_phase[0].phase_started_tick = future_phase[0].tick_sequence + 1;
  expect_invalid(future_phase);
  std::vector<protocol::LobbyListing> unnamed = fixture::golden_lobby_listings();
  unnamed[0].map_name = "Arena";
  expect_invalid(unnamed);

  // A room that has published nothing lists at tick zero with every count zero, which is legal:
  // it is how an unready or failed room appears.
  std::vector<protocol::LobbyListing> unready = fixture::golden_lobby_listings();
  unready[1].tick_sequence = 0;
  unready[1].phase_started_tick = 0;
  unready[1].seat_count = 0;
  unready[1].filled_seat_count = 0;
  unready[1].npc_seat_count = 0;
  unready[1].healthy = false;
  CHECK_NOTHROW(encode(unready)());
}

TEST_CASE(
    "V2 error envelope carries the three lobby rows with their status, retryability, and room",
    "[unit][protocol][v2][encoding][lobbies]") {
  const protocol::V2HttpError not_found = protocol::V2HttpError::lobby_not_found();
  CHECK(not_found.status_code() == 404);
  CHECK(not_found.code() == "LOBBY.NOT_FOUND");
  CHECK_FALSE(not_found.retryable());
  CHECK_FALSE(not_found.lobby_id().has_value());
  const std::string encoded_not_found =
      protocol::encode_error_response_v2(not_found, fixture::session_request_id());
  CHECK(encoded_not_found.find(R"("code":"LOBBY.NOT_FOUND")") != std::string::npos);
  CHECK(encoded_not_found.find(R"("details":{})") != std::string::npos);

  const protocol::V2HttpError full = protocol::V2HttpError::lobby_full(3);
  CHECK(full.status_code() == 409);
  CHECK(full.code() == "LOBBY.FULL");
  CHECK(full.retryable());
  CHECK(full.lobby_id() == 3);
  const std::string encoded_full =
      protocol::encode_error_response_v2(full, fixture::session_request_id());
  CHECK(encoded_full.find(R"("retryable":true,"details":{"lobby_id":3})") != std::string::npos);

  const protocol::V2HttpError unavailable = protocol::V2HttpError::lobby_unavailable(2);
  CHECK(unavailable.status_code() == 503);
  CHECK(unavailable.code() == "LOBBY.UNAVAILABLE");
  CHECK(unavailable.retryable());
  const std::string encoded_unavailable =
      protocol::encode_error_response_v2(unavailable, fixture::session_request_id());
  CHECK(encoded_unavailable.find(R"("details":{"lobby_id":2})") != std::string::npos);
  CHECK(encoded_unavailable.find(R"("schema_id":"blob-royale://protocol/v2/error-response")") !=
        std::string::npos);
}
