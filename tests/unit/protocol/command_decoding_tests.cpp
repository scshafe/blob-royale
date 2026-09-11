#include "protocol_v3_test_fixture.hpp"

#include "command_decoding.hpp"
#include "command_wire_kind.hpp"
#include "protocol_v3_constants.hpp"

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "commands/clear_seat_command.hpp"
#include "commands/seat_npc_command.hpp"
#include "commands/set_movement_tuning_command.hpp"
#include "commands/set_seat_count_command.hpp"
#include "commands/start_match_command.hpp"
#include "commands/thrust_command.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "vector2.hpp"
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace protocol = blob_royale::protocol;
namespace fixture = blob_royale::protocol::v3_test_fixture;
namespace simulation = blob_royale::simulation;

namespace {

inline constexpr std::uint64_t kStampedEntityId = 7;
inline constexpr std::uint64_t kStampedControllerId = 3;

[[nodiscard]] simulation::EntityId stamped_entity() {
  return simulation::EntityId::create(kStampedEntityId);
}

[[nodiscard]] simulation::ControllerId stamped_controller() {
  return simulation::ControllerId::create(kStampedControllerId);
}

// The list a `welcome` published to this session, standing in for `ControllerRegistry`'s.
[[nodiscard]] std::vector<std::string> published_npc_kinds() { return {"wanderer", "chaser"}; }

[[nodiscard]] simulation::CommandKindMask thrust_only() {
  return simulation::CommandKindMask::create({simulation::CommandKind::kThrust});
}

[[nodiscard]] simulation::CommandKindMask lobby_kinds() {
  return simulation::CommandKindMask::create(
      {simulation::CommandKind::kThrust, simulation::CommandKind::kSetSeatCount,
       simulation::CommandKind::kClearSeat, simulation::CommandKind::kSeatNpc,
       simulation::CommandKind::kStartMatch, simulation::CommandKind::kSetMovementTuning});
}

[[nodiscard]] protocol::CommandDecodeResult decode(const std::string_view frame) {
  const std::vector<std::string> npc_kinds = published_npc_kinds();
  return protocol::decode_command_envelope(frame, thrust_only(), stamped_entity(),
                                           stamped_controller(), npc_kinds);
}

[[nodiscard]] protocol::CommandDecodeResult decode_lobby(const std::string_view frame) {
  const std::vector<std::string> npc_kinds = published_npc_kinds();
  return protocol::decode_command_envelope(frame, lobby_kinds(), stamped_entity(),
                                           stamped_controller(), npc_kinds);
}

void require_rejection(const std::string_view frame,
                       const protocol::CommandDecodeRejection expected) {
  const protocol::CommandDecodeResult result = decode(frame);
  CHECK_FALSE(result.is_accepted());
  CHECK(result.rejection() == expected);
  CHECK_FALSE(result.command().has_value());
}

void require_lobby_rejection(const std::string_view frame,
                             const protocol::CommandDecodeRejection expected) {
  const protocol::CommandDecodeResult result = decode_lobby(frame);
  CHECK_FALSE(result.is_accepted());
  CHECK(result.rejection() == expected);
  CHECK_FALSE(result.command().has_value());
}

} // namespace

TEST_CASE("Command decoder accepts the accepted golden envelope and stamps the session's entity",
          "[unit][protocol][v3][decoding][golden]") {
  const std::string golden = fixture::read_v3_golden_example("command-envelope.json");
  const protocol::CommandDecodeResult result = decode(golden);

  REQUIRE(result.is_accepted());
  REQUIRE(result.command().has_value());
  const auto* const thrust = std::get_if<simulation::ThrustCommand>(&*result.command());
  REQUIRE(thrust != nullptr);
  CHECK(thrust->entity == stamped_entity());
  CHECK(thrust->direction == simulation::Vector2::create(1.0, -0.5));
}

TEST_CASE("Command decoder stamps only the session's own entity, whatever the client sends",
          "[unit][protocol][v3][decoding]") {
  const std::vector<std::string> npc_kinds = published_npc_kinds();
  const protocol::CommandDecodeResult result = protocol::decode_command_envelope(
      R"({"kind":"set_thrust","payload":{"x":0,"y":1}})", thrust_only(),
      simulation::EntityId::create(4'242), stamped_controller(), npc_kinds);

  REQUIRE(result.is_accepted());
  const auto* const thrust = std::get_if<simulation::ThrustCommand>(&*result.command());
  REQUIRE(thrust != nullptr);
  CHECK(thrust->entity == simulation::EntityId::create(4'242));
}

TEST_CASE("Command decoder carries a thrust of magnitude greater than one verbatim",
          "[unit][protocol][v3][decoding]") {
  const protocol::CommandDecodeResult result = decode(R"({"kind":"set_thrust",)"
                                                      R"("payload":{"x":1,"y":1}})");

  REQUIRE(result.is_accepted());
  const auto* const thrust = std::get_if<simulation::ThrustCommand>(&*result.command());
  REQUIRE(thrust != nullptr);
  CHECK(thrust->direction == simulation::Vector2::create(1.0, 1.0));
}

TEST_CASE("Command decoder rejects an inbound message above the 1,024-byte bound",
          "[unit][protocol][v3][decoding][rejection]") {
  const std::string padding(protocol::kClientMessageMaximumByteCount, 'a');
  const std::string oversized =
      R"({"kind":"set_thrust","payload":{"x":0,"y":0},"padding":")" + padding + R"("})";
  REQUIRE(oversized.size() > protocol::kClientMessageMaximumByteCount);

  require_rejection(oversized, protocol::CommandDecodeRejection::kMessageTooLarge);
  CHECK(decode(oversized).close_code() == 1009);
  CHECK(decode(oversized).rejection_name() == "client_message_too_large");
}

TEST_CASE("Command decoder rejects an envelope carrying an extra member",
          "[unit][protocol][v3][decoding][rejection]") {
  require_rejection(R"({"kind":"set_thrust","payload":{"x":0,"y":0},"protocol_version":"2.0"})",
                    protocol::CommandDecodeRejection::kMalformed);
  require_rejection(R"({"kind":"set_thrust","payload":{"x":0,"y":0},"data":null,"error":null})",
                    protocol::CommandDecodeRejection::kMalformed);
}

TEST_CASE("Command decoder rejects an envelope missing a member or carrying a non-object payload",
          "[unit][protocol][v3][decoding][rejection]") {
  require_rejection(R"({"kind":"set_thrust"})", protocol::CommandDecodeRejection::kMalformed);
  require_rejection(R"({"payload":{"x":0,"y":0}})", protocol::CommandDecodeRejection::kMalformed);
  require_rejection(R"({"kind":"set_thrust","payload":[0,0]})",
                    protocol::CommandDecodeRejection::kMalformed);
  require_rejection(R"([{"kind":"set_thrust","payload":{"x":0,"y":0}}])",
                    protocol::CommandDecodeRejection::kMalformed);
}

TEST_CASE("Command decoder rejects a non-finite value and every non-standard JSON literal",
          "[unit][protocol][v3][decoding][rejection]") {
  require_rejection(R"({"kind":"set_thrust","payload":{"x":NaN,"y":0}})",
                    protocol::CommandDecodeRejection::kMalformed);
  require_rejection(R"({"kind":"set_thrust","payload":{"x":Infinity,"y":0}})",
                    protocol::CommandDecodeRejection::kMalformed);
  require_rejection(R"({"kind":"set_thrust","payload":{"x":-Infinity,"y":0}})",
                    protocol::CommandDecodeRejection::kMalformed);
  require_rejection(R"({"kind":"set_thrust","payload":{"x":0,"y":0}} trailing)",
                    protocol::CommandDecodeRejection::kMalformed);
  require_rejection("", protocol::CommandDecodeRejection::kMalformed);
}

TEST_CASE("Command decoder rejects an unknown command kind",
          "[unit][protocol][v3][decoding][rejection]") {
  require_rejection(R"({"kind":"set_radius","payload":{"radius":40}})",
                    protocol::CommandDecodeRejection::kKindRejected);
  require_rejection(R"({"kind":7,"payload":{"x":0,"y":0}})",
                    protocol::CommandDecodeRejection::kKindRejected);
  CHECK(decode(R"({"kind":"set_radius","payload":{}})").close_code() == 1008);
}

TEST_CASE("Command decoder rejects the server-issued kinds the wire deliberately does not name",
          "[unit][protocol][v3][decoding][rejection]") {
  require_rejection(R"({"kind":"spawn","payload":{}})",
                    protocol::CommandDecodeRejection::kKindRejected);
  require_rejection(R"({"kind":"despawn","payload":{}})",
                    protocol::CommandDecodeRejection::kKindRejected);
  require_rejection(R"({"kind":"leave","payload":{}})",
                    protocol::CommandDecodeRejection::kKindRejected);
  // A client that could send a join could name a seat, which is exactly what the kind exists to
  // keep out of a client's hands.
  require_rejection(R"({"kind":"join","payload":{"seat_index":0}})",
                    protocol::CommandDecodeRejection::kKindRejected);

  CHECK_FALSE(protocol::client_command_wire_name(simulation::CommandKind::kSpawn).has_value());
  CHECK_FALSE(protocol::client_command_wire_name(simulation::CommandKind::kDespawn).has_value());
  CHECK_FALSE(protocol::client_command_wire_name(simulation::CommandKind::kLeave).has_value());
  CHECK_FALSE(protocol::client_command_wire_name(simulation::CommandKind::kJoin).has_value());
  CHECK(protocol::client_command_wire_name(simulation::CommandKind::kThrust) == "set_thrust");
  CHECK(protocol::client_command_wire_name(simulation::CommandKind::kSetSeatCount) ==
        "set_seat_count");
  CHECK(protocol::client_command_wire_name(simulation::CommandKind::kClearSeat) == "clear_seat");
  CHECK(protocol::client_command_wire_name(simulation::CommandKind::kSeatNpc) == "seat_npc");
  CHECK(protocol::client_command_wire_name(simulation::CommandKind::kStartMatch) == "start_match");
}

TEST_CASE("Command decoder rejects a registered kind the running mode does not accept",
          "[unit][protocol][v3][decoding][rejection]") {
  const std::vector<std::string> npc_kinds = published_npc_kinds();
  const protocol::CommandDecodeResult result = protocol::decode_command_envelope(
      R"({"kind":"set_thrust","payload":{"x":0,"y":0}})", simulation::CommandKindMask::none(),
      stamped_entity(), stamped_controller(), npc_kinds);

  CHECK_FALSE(result.is_accepted());
  CHECK(result.rejection() == protocol::CommandDecodeRejection::kKindRejected);
}

TEST_CASE("Command decoder rejects a thrust component outside the closed interval",
          "[unit][protocol][v3][decoding][rejection]") {
  require_rejection(R"({"kind":"set_thrust","payload":{"x":1.0000001,"y":0}})",
                    protocol::CommandDecodeRejection::kPayloadInvalid);
  require_rejection(R"({"kind":"set_thrust","payload":{"x":0,"y":-1.5}})",
                    protocol::CommandDecodeRejection::kPayloadInvalid);
  require_rejection(R"({"kind":"set_thrust","payload":{"x":2,"y":0}})",
                    protocol::CommandDecodeRejection::kPayloadInvalid);
}

TEST_CASE("Command decoder rejects a payload carrying an entity id or any other extra member",
          "[unit][protocol][v3][decoding][rejection]") {
  require_rejection(R"({"kind":"set_thrust","payload":{"x":1,"y":0,"entity_id":9}})",
                    protocol::CommandDecodeRejection::kPayloadInvalid);
  require_rejection(R"({"kind":"set_thrust","payload":{"x":1}})",
                    protocol::CommandDecodeRejection::kPayloadInvalid);
  require_rejection(R"({"kind":"set_thrust","payload":{}})",
                    protocol::CommandDecodeRejection::kPayloadInvalid);
  require_rejection(R"({"kind":"set_thrust","payload":{"x":"1","y":0}})",
                    protocol::CommandDecodeRejection::kPayloadInvalid);
  require_rejection(R"({"kind":"set_thrust","payload":{"x":true,"y":0}})",
                    protocol::CommandDecodeRejection::kPayloadInvalid);
}

TEST_CASE("Command decoder accepts a frame at exactly the inbound byte bound",
          "[unit][protocol][v3][decoding]") {
  const std::string envelope = R"({"kind":"set_thrust","payload":{"x":0,"y":0}})";
  const std::string spaced =
      envelope + std::string(protocol::kClientMessageMaximumByteCount - envelope.size(), ' ');
  REQUIRE(spaced.size() == protocol::kClientMessageMaximumByteCount);

  const std::vector<std::string> npc_kinds = published_npc_kinds();
  CHECK(protocol::decode_command_envelope(spaced, thrust_only(), stamped_entity(),
                                          stamped_controller(), npc_kinds)
            .is_accepted());
}

TEST_CASE("Command decoder stamps the session's own controller on every lobby command",
          "[unit][protocol][v3][decoding][lobby]") {
  // The lobby envelopes name no sender, exactly as `set_thrust` names no entity, so "ignore the
  // client's controller id" is a shape that does not exist rather than a check to maintain.
  const protocol::CommandDecodeResult seat_count =
      decode_lobby(R"({"kind":"set_seat_count","payload":{"seat_count":6}})");
  REQUIRE(seat_count.is_accepted());
  const auto* const set_seat_count =
      std::get_if<simulation::SetSeatCountCommand>(&*seat_count.command());
  REQUIRE(set_seat_count != nullptr);
  CHECK(set_seat_count->controller == stamped_controller());
  CHECK(set_seat_count->seat_count == 6);

  const protocol::CommandDecodeResult cleared =
      decode_lobby(R"({"kind":"clear_seat","payload":{"seat_index":2}})");
  REQUIRE(cleared.is_accepted());
  const auto* const clear_seat = std::get_if<simulation::ClearSeatCommand>(&*cleared.command());
  REQUIRE(clear_seat != nullptr);
  CHECK(clear_seat->controller == stamped_controller());
  CHECK(clear_seat->seat_index == 2);

  const protocol::CommandDecodeResult seated =
      decode_lobby(R"({"kind":"seat_npc","payload":{"seat_index":0,"npc_kind":"chaser"}})");
  REQUIRE(seated.is_accepted());
  const auto* const seat_npc = std::get_if<simulation::SeatNpcCommand>(&*seated.command());
  REQUIRE(seat_npc != nullptr);
  CHECK(seat_npc->controller == stamped_controller());
  CHECK(seat_npc->seat_index == 0);
  CHECK(seat_npc->kind == std::string_view{"chaser"});

  const protocol::CommandDecodeResult started =
      decode_lobby(R"({"kind":"start_match","payload":{}})");
  REQUIRE(started.is_accepted());
  const auto* const start_match = std::get_if<simulation::StartMatchCommand>(&*started.command());
  REQUIRE(start_match != nullptr);
  CHECK(start_match->controller == stamped_controller());
}

TEST_CASE("Command decoder accepts exactly the NPC kinds the welcome published",
          "[unit][protocol][v3][decoding][lobby]") {
  // **This is the acceptance test for "registering a bot costs no client change".** The decoder is
  // handed the same list the welcome frame published, so what a client may name and what the server
  // can build are one value; a kind outside it is refused whatever its grammar.
  CHECK(decode_lobby(R"({"kind":"seat_npc","payload":{"seat_index":1,"npc_kind":"wanderer"}})")
            .is_accepted());
  CHECK(decode_lobby(R"({"kind":"seat_npc","payload":{"seat_index":1,"npc_kind":"chaser"}})")
            .is_accepted());
  require_lobby_rejection(
      R"({"kind":"seat_npc","payload":{"seat_index":1,"npc_kind":"scripted_replay"}})",
      protocol::CommandDecodeRejection::kPayloadInvalid);
  require_lobby_rejection(R"({"kind":"seat_npc","payload":{"seat_index":1,"npc_kind":"Chaser"}})",
                          protocol::CommandDecodeRejection::kPayloadInvalid);

  // A build with one more registered bot accepts one more kind, from the same one list and with no
  // schema, decoder, or client edit -- which is the whole property, demonstrated rather than
  // asserted about.
  const std::vector<std::string> with_a_new_bot{"wanderer", "chaser", "ambusher"};
  const protocol::CommandDecodeResult accepted = protocol::decode_command_envelope(
      R"({"kind":"seat_npc","payload":{"seat_index":1,"npc_kind":"ambusher"}})", lobby_kinds(),
      stamped_entity(), stamped_controller(), with_a_new_bot);
  REQUIRE(accepted.is_accepted());
  CHECK(std::get<simulation::SeatNpcCommand>(*accepted.command()).kind ==
        std::string_view{"ambusher"});

  // And a server that registers no bot at all accepts none, rather than falling back to a name it
  // could not build.
  const std::vector<std::string> no_bots;
  const protocol::CommandDecodeResult refused = protocol::decode_command_envelope(
      R"({"kind":"seat_npc","payload":{"seat_index":1,"npc_kind":"wanderer"}})", lobby_kinds(),
      stamped_entity(), stamped_controller(), no_bots);
  CHECK_FALSE(refused.is_accepted());
  CHECK(refused.rejection() == protocol::CommandDecodeRejection::kPayloadInvalid);
}

TEST_CASE("Command decoder bounds a seat index and a seat count and refuses a fractional one",
          "[unit][protocol][v3][decoding][lobby][rejection]") {
  // The published constants, not the running lobby's size: the boundary holds no world, and an
  // index inside this bound that names no seat is a disagreement the tick ignores rather than a
  // frame that closes a connection.
  CHECK(decode_lobby(R"({"kind":"clear_seat","payload":{"seat_index":63}})").is_accepted());
  require_lobby_rejection(R"({"kind":"clear_seat","payload":{"seat_index":64}})",
                          protocol::CommandDecodeRejection::kPayloadInvalid);
  require_lobby_rejection(R"({"kind":"clear_seat","payload":{"seat_index":-1}})",
                          protocol::CommandDecodeRejection::kPayloadInvalid);
  require_lobby_rejection(R"({"kind":"clear_seat","payload":{"seat_index":1.5}})",
                          protocol::CommandDecodeRejection::kPayloadInvalid);

  CHECK(decode_lobby(R"({"kind":"set_seat_count","payload":{"seat_count":1}})").is_accepted());
  CHECK(decode_lobby(R"({"kind":"set_seat_count","payload":{"seat_count":64}})").is_accepted());
  require_lobby_rejection(R"({"kind":"set_seat_count","payload":{"seat_count":0}})",
                          protocol::CommandDecodeRejection::kPayloadInvalid);
  require_lobby_rejection(R"({"kind":"set_seat_count","payload":{"seat_count":65}})",
                          protocol::CommandDecodeRejection::kPayloadInvalid);
  // `2.0` is refused rather than truncated: the schema types this member as an integer, and a
  // decoder that rounded would accept a value the published contract does not describe.
  require_lobby_rejection(R"({"kind":"set_seat_count","payload":{"seat_count":2.0}})",
                          protocol::CommandDecodeRejection::kPayloadInvalid);
}

TEST_CASE("Command decoder refuses any member a lobby payload does not declare",
          "[unit][protocol][v3][decoding][lobby][rejection]") {
  require_lobby_rejection(R"({"kind":"start_match","payload":{"seat_index":0}})",
                          protocol::CommandDecodeRejection::kPayloadInvalid);
  require_lobby_rejection(R"({"kind":"set_seat_count","payload":{"seat_count":4,"force":true}})",
                          protocol::CommandDecodeRejection::kPayloadInvalid);
  require_lobby_rejection(R"({"kind":"seat_npc","payload":{"seat_index":0}})",
                          protocol::CommandDecodeRejection::kPayloadInvalid);
  require_lobby_rejection(R"({"kind":"clear_seat","payload":{}})",
                          protocol::CommandDecodeRejection::kPayloadInvalid);
  // A controller id is not merely ignored, it is a closed-schema violation -- the same rule that
  // makes an entity id in a `set_thrust` payload a rejection.
  require_lobby_rejection(R"({"kind":"start_match","payload":{"controller_id":9}})",
                          protocol::CommandDecodeRejection::kPayloadInvalid);
}

TEST_CASE("A mode that accepts no lobby kind refuses every lobby command",
          "[unit][protocol][v3][decoding][lobby][rejection]") {
  // Sandbox's shape: the kinds are registered on the wire and absent from the mode's mask, which is
  // admission-order step 6's second half.
  require_rejection(R"({"kind":"start_match","payload":{}})",
                    protocol::CommandDecodeRejection::kKindRejected);
  require_rejection(R"({"kind":"set_seat_count","payload":{"seat_count":4}})",
                    protocol::CommandDecodeRejection::kKindRejected);
}
