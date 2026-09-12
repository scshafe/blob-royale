#include "../simulation/fixtures/npc_declaration_fixture.hpp"
#include "protocol_v3_test_fixture.hpp"

#include "command_decoding.hpp"

#include <boost/json/serialize.hpp>
#include <catch2/catch_test_macros.hpp>

namespace protocol = blob_royale::protocol;
namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::npc_declaration_fixture;
namespace v3 = protocol::v3_test_fixture;

namespace {

[[nodiscard]] protocol::CommandDecodeResult decode(const std::string_view payload) {
  return protocol::decode_command_envelope(
      std::string{R"({"kind":"seat_npc","payload":)"} + std::string{payload} + "}",
      simulation::CommandKindMask::all(), simulation::EntityId::create(fixture::kController),
      simulation::ControllerId::create(fixture::kController), fixture::catalogue());
}

[[nodiscard]] protocol::SessionWelcome welcome(const simulation::NpcCatalogue& catalogue) {
  return protocol::SessionWelcome::create(
      simulation::EntityId::create(v3::kPlayerEntityId),
      simulation::ControllerId::create(v3::kPlayerControllerId),
      std::string{v3::kPlayerDisplayName}, std::string{v3::kGoldenModeName},
      std::string{v3::kGoldenMapName}, simulation::CommandKindMask::all(), catalogue,
      v3::kGoldenLobbyId, v3::kGoldenSeatCountMaximum, v3::golden_terrain());
}

[[nodiscard]] simulation::WorldSnapshot
seat_snapshot(const std::optional<simulation::ControllerId> controller) {
  auto world = simulation::GameWorld::create({});
  world.mutable_match().seats = simulation::SeatRoster::of_size(fixture::kSeatCount);
  const auto declaration = fixture::profiled();
  world.mutable_match().seats.assign_seat(
      fixture::kSeatIndex,
      simulation::NpcSeat{declaration.kind, controller, declaration.profile_name});
  auto game = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16), std::move(world));
  game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  return game.snapshot();
}

} // namespace

TEST_CASE("NPC profile decoder admits the complete declared selection and preserves plain omission",
          "[unit][protocol][npc_profile][decoding]") {
  const auto profiled = decode(R"({"seat_index":1,"npc_kind":"tactical","profile_name":"steady"})");
  REQUIRE(profiled.is_accepted());
  CHECK(std::get<simulation::SeatNpcCommand>(*profiled.command()).declaration() ==
        fixture::profiled());
  const auto plain = decode(R"({"seat_index":1,"npc_kind":"wanderer"})");
  REQUIRE(plain.is_accepted());
  CHECK(std::get<simulation::SeatNpcCommand>(*plain.command()).declaration() == fixture::plain());
  const auto empty = protocol::decode_command_envelope(
      R"({"kind":"seat_npc","payload":{"seat_index":1,"npc_kind":"wanderer"}})",
      simulation::CommandKindMask::all(), simulation::EntityId::create(fixture::kController),
      simulation::ControllerId::create(fixture::kController));
  CHECK(empty.rejection() == protocol::CommandDecodeRejection::kPayloadInvalid);
}

TEST_CASE("NPC profile decoder rejects missing null unknown mismatched and extra profile data",
          "[unit][protocol][npc_profile][decoding][rejection]") {
  for (const auto payload :
       {R"({"seat_index":1,"npc_kind":"tactical"})",
        R"({"seat_index":1,"npc_kind":"tactical","profile_name":null})",
        R"({"seat_index":1,"npc_kind":"tactical","profile_name":1})",
        R"({"seat_index":1,"npc_kind":"tactical","profile_name":""})",
        R"({"seat_index":1,"npc_kind":"tactical","profile_name":"unknown"})",
        R"({"seat_index":1,"npc_kind":"wanderer","profile_name":"steady"})",
        R"({"seat_index":1,"npc_kind":"tactical","profile_name":"steady","extra":0})",
        R"({"seat_index":1,"npc_kind":"tactical","profile":"steady"})"}) {
    CAPTURE(payload);
    const auto result = decode(payload);
    CHECK(result.rejection() == protocol::CommandDecodeRejection::kPayloadInvalid);
    CHECK_FALSE(result.command().has_value());
  }
}

TEST_CASE("Welcome profile choices preserve both catalogue projections in declaration order",
          "[unit][protocol][npc_profile][encoding]") {
  const auto value = welcome(fixture::catalogue());
  CHECK(value.npc_catalogue() == fixture::catalogue());
  const std::string encoded =
      protocol::encode_welcome_message(value, v3::session_request_id(), v3::kWelcomeTimestamp);
  CHECK(
      encoded.find(
          R"("npc_controller_kinds":["wanderer","chaser"],"npc_profiles":[{"npc_kind":"tactical","profile_name":"steady"},{"npc_kind":"tactical","profile_name":"quick"}])") !=
      std::string::npos);
  const std::string plain = protocol::encode_welcome_message(
      v3::golden_welcome(), v3::session_request_id(), v3::kWelcomeTimestamp);
  CHECK(plain.find("npc_profiles") == std::string::npos);
  const auto maximum_catalogue = simulation::NpcCatalogue::create(
      fixture::plain_names(simulation::kMaximumUnprofiledNpcKindCount),
      fixture::profile_names(simulation::kMaximumNpcProfileCount));
  const auto maximum = protocol::encode_welcome_message(
      welcome(maximum_catalogue), v3::session_request_id(), v3::kWelcomeTimestamp);
  const auto document = boost::json::parse(maximum);
  const auto& data = document.as_object().at("data").as_object();
  CHECK(data.at("npc_controller_kinds").as_array().size() ==
        simulation::kMaximumUnprofiledNpcKindCount);
  CHECK(data.at("npc_profiles").as_array().size() == simulation::kMaximumNpcProfileCount);
}

TEST_CASE("NPC profile remains public on both pending and occupied seats",
          "[unit][protocol][npc_profile][encoding]") {
  for (const auto controller :
       {std::optional<simulation::ControllerId>{},
        std::optional{simulation::ControllerId::create(fixture::kBotController)}}) {
    const auto snapshot = seat_snapshot(controller);
    const auto encoded = protocol::encode_snapshot_message_v3(
        snapshot, v3::golden_directory(), std::nullopt, v3::session_request_id(),
        v3::kSnapshotMessageSequence, v3::kSnapshotTimestamp);
    const auto document = boost::json::parse(encoded);
    const auto& seats =
        document.as_object().at("data").as_object().at("match").as_object().at("seats").as_array();
    const auto& seat = seats[fixture::kSeatIndex].as_object();
    CHECK(seat.at("npc_kind").as_string() == fixture::kProfileKind);
    CHECK(seat.at("profile_name").as_string() == fixture::kProfileName);
    CHECK(seat.at("controller_id").is_null() == !controller.has_value());
    CHECK_FALSE(seats.front().as_object().contains("profile_name"));
  }
}
