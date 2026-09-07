#include "protocol_v2_test_fixture.hpp"

#include "command_decoding.hpp"
#include "command_wire_kind.hpp"
#include "protocol_v2_constants.hpp"

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "commands/thrust_command.hpp"
#include "entity_id.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

namespace protocol = blob_royale::protocol;
namespace fixture = blob_royale::protocol::v2_test_fixture;
namespace simulation = blob_royale::simulation;

namespace {

inline constexpr std::uint64_t kStampedEntityId = 7;

[[nodiscard]] simulation::EntityId stamped_entity() {
  return simulation::EntityId::create(kStampedEntityId);
}

[[nodiscard]] simulation::CommandKindMask thrust_only() {
  return simulation::CommandKindMask::create({simulation::CommandKind::kThrust});
}

[[nodiscard]] protocol::CommandDecodeResult decode(const std::string_view frame) {
  return protocol::decode_command_envelope(frame, thrust_only(), stamped_entity());
}

void require_rejection(const std::string_view frame,
                       const protocol::CommandDecodeRejection expected) {
  const protocol::CommandDecodeResult result = decode(frame);
  CHECK_FALSE(result.is_accepted());
  CHECK(result.rejection() == expected);
  CHECK_FALSE(result.command().has_value());
}

} // namespace

TEST_CASE("Command decoder accepts the accepted golden envelope and stamps the session's entity",
          "[unit][protocol][v2][decoding][golden]") {
  const std::string golden = fixture::read_v2_golden_example("command-envelope.json");
  const protocol::CommandDecodeResult result = decode(golden);

  REQUIRE(result.is_accepted());
  REQUIRE(result.command().has_value());
  const auto* const thrust = std::get_if<simulation::ThrustCommand>(&*result.command());
  REQUIRE(thrust != nullptr);
  CHECK(thrust->entity == stamped_entity());
  CHECK(thrust->direction == simulation::Vector2::create(1.0, -0.5));
}

TEST_CASE("Command decoder stamps only the session's own entity, whatever the client sends",
          "[unit][protocol][v2][decoding]") {
  const protocol::CommandDecodeResult result =
      protocol::decode_command_envelope(R"({"kind":"set_thrust","payload":{"x":0,"y":1}})",
                                        thrust_only(), simulation::EntityId::create(4'242));

  REQUIRE(result.is_accepted());
  const auto* const thrust = std::get_if<simulation::ThrustCommand>(&*result.command());
  REQUIRE(thrust != nullptr);
  CHECK(thrust->entity == simulation::EntityId::create(4'242));
}

TEST_CASE("Command decoder carries a thrust of magnitude greater than one verbatim",
          "[unit][protocol][v2][decoding]") {
  const protocol::CommandDecodeResult result = decode(R"({"kind":"set_thrust",)"
                                                      R"("payload":{"x":1,"y":1}})");

  REQUIRE(result.is_accepted());
  const auto* const thrust = std::get_if<simulation::ThrustCommand>(&*result.command());
  REQUIRE(thrust != nullptr);
  CHECK(thrust->direction == simulation::Vector2::create(1.0, 1.0));
}

TEST_CASE("Command decoder rejects an inbound message above the 1,024-byte bound",
          "[unit][protocol][v2][decoding][rejection]") {
  const std::string padding(protocol::kClientMessageMaximumByteCount, 'a');
  const std::string oversized =
      R"({"kind":"set_thrust","payload":{"x":0,"y":0},"padding":")" + padding + R"("})";
  REQUIRE(oversized.size() > protocol::kClientMessageMaximumByteCount);

  require_rejection(oversized, protocol::CommandDecodeRejection::kMessageTooLarge);
  CHECK(decode(oversized).close_code() == 1009);
  CHECK(decode(oversized).rejection_name() == "client_message_too_large");
}

TEST_CASE("Command decoder rejects an envelope carrying an extra member",
          "[unit][protocol][v2][decoding][rejection]") {
  require_rejection(R"({"kind":"set_thrust","payload":{"x":0,"y":0},"protocol_version":"2.0"})",
                    protocol::CommandDecodeRejection::kMalformed);
  require_rejection(R"({"kind":"set_thrust","payload":{"x":0,"y":0},"data":null,"error":null})",
                    protocol::CommandDecodeRejection::kMalformed);
}

TEST_CASE("Command decoder rejects an envelope missing a member or carrying a non-object payload",
          "[unit][protocol][v2][decoding][rejection]") {
  require_rejection(R"({"kind":"set_thrust"})", protocol::CommandDecodeRejection::kMalformed);
  require_rejection(R"({"payload":{"x":0,"y":0}})", protocol::CommandDecodeRejection::kMalformed);
  require_rejection(R"({"kind":"set_thrust","payload":[0,0]})",
                    protocol::CommandDecodeRejection::kMalformed);
  require_rejection(R"([{"kind":"set_thrust","payload":{"x":0,"y":0}}])",
                    protocol::CommandDecodeRejection::kMalformed);
}

TEST_CASE("Command decoder rejects a non-finite value and every non-standard JSON literal",
          "[unit][protocol][v2][decoding][rejection]") {
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
          "[unit][protocol][v2][decoding][rejection]") {
  require_rejection(R"({"kind":"set_radius","payload":{"radius":40}})",
                    protocol::CommandDecodeRejection::kKindRejected);
  require_rejection(R"({"kind":7,"payload":{"x":0,"y":0}})",
                    protocol::CommandDecodeRejection::kKindRejected);
  CHECK(decode(R"({"kind":"set_radius","payload":{}})").close_code() == 1008);
}

TEST_CASE("Command decoder rejects the server-issued kinds the wire deliberately does not name",
          "[unit][protocol][v2][decoding][rejection]") {
  require_rejection(R"({"kind":"spawn","payload":{}})",
                    protocol::CommandDecodeRejection::kKindRejected);
  require_rejection(R"({"kind":"despawn","payload":{}})",
                    protocol::CommandDecodeRejection::kKindRejected);

  CHECK_FALSE(protocol::client_command_wire_name(simulation::CommandKind::kSpawn).has_value());
  CHECK_FALSE(protocol::client_command_wire_name(simulation::CommandKind::kDespawn).has_value());
  CHECK(protocol::client_command_wire_name(simulation::CommandKind::kThrust) == "set_thrust");
}

TEST_CASE("Command decoder rejects a registered kind the running mode does not accept",
          "[unit][protocol][v2][decoding][rejection]") {
  const protocol::CommandDecodeResult result =
      protocol::decode_command_envelope(R"({"kind":"set_thrust","payload":{"x":0,"y":0}})",
                                        simulation::CommandKindMask::none(), stamped_entity());

  CHECK_FALSE(result.is_accepted());
  CHECK(result.rejection() == protocol::CommandDecodeRejection::kKindRejected);
}

TEST_CASE("Command decoder rejects a thrust component outside the closed interval",
          "[unit][protocol][v2][decoding][rejection]") {
  require_rejection(R"({"kind":"set_thrust","payload":{"x":1.0000001,"y":0}})",
                    protocol::CommandDecodeRejection::kPayloadInvalid);
  require_rejection(R"({"kind":"set_thrust","payload":{"x":0,"y":-1.5}})",
                    protocol::CommandDecodeRejection::kPayloadInvalid);
  require_rejection(R"({"kind":"set_thrust","payload":{"x":2,"y":0}})",
                    protocol::CommandDecodeRejection::kPayloadInvalid);
}

TEST_CASE("Command decoder rejects a payload carrying an entity id or any other extra member",
          "[unit][protocol][v2][decoding][rejection]") {
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
          "[unit][protocol][v2][decoding]") {
  const std::string envelope = R"({"kind":"set_thrust","payload":{"x":0,"y":0}})";
  const std::string spaced =
      envelope + std::string(protocol::kClientMessageMaximumByteCount - envelope.size(), ' ');
  REQUIRE(spaced.size() == protocol::kClientMessageMaximumByteCount);

  CHECK(protocol::decode_command_envelope(spaced, thrust_only(), stamped_entity()).is_accepted());
}
