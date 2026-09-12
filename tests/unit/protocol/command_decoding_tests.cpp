#include "protocol_v3_test_fixture.hpp"

#include "command_decoding.hpp"
#include "command_wire_kind.hpp"
#include "protocol_v3_constants.hpp"

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "commands/charge_command.hpp"
#include "commands/clear_seat_command.hpp"
#include "commands/seat_npc_command.hpp"
#include "commands/set_movement_tuning_command.hpp"
#include "commands/set_seat_count_command.hpp"
#include "commands/shield_command.hpp"
#include "commands/start_match_command.hpp"
#include "commands/thrust_command.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
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

// Sandbox's shape once it declares the ability: the pulse and nothing else, so a rejection in these
// cases is the payload's answer rather than the mask's.
[[nodiscard]] simulation::CommandKindMask shield_only() {
  return simulation::CommandKindMask::create({simulation::CommandKind::kShield});
}

// The same shape for the other ability, and deliberately not a combined ability mask: a charge case
// that also accepted `shield` could not tell a payload rejection from a kind the mask happened to
// admit for the wrong reason.
[[nodiscard]] simulation::CommandKindMask charge_only() {
  return simulation::CommandKindMask::create({simulation::CommandKind::kCharge});
}

[[nodiscard]] protocol::CommandDecodeResult decode(const std::string_view frame) {
  const std::vector<std::string> npc_kinds = published_npc_kinds();
  return protocol::decode_command_envelope(frame, thrust_only(), stamped_entity(),
                                           stamped_controller(),
                                           simulation::NpcCatalogue::create(npc_kinds));
}

[[nodiscard]] protocol::CommandDecodeResult decode_lobby(const std::string_view frame) {
  const std::vector<std::string> npc_kinds = published_npc_kinds();
  return protocol::decode_command_envelope(frame, lobby_kinds(), stamped_entity(),
                                           stamped_controller(),
                                           simulation::NpcCatalogue::create(npc_kinds));
}

[[nodiscard]] protocol::CommandDecodeResult decode_charge(const std::string_view payload) {
  const std::vector<std::string> npc_kinds = published_npc_kinds();
  return protocol::decode_command_envelope(
      std::string{R"({"kind":"charge","payload":)"} + std::string{payload} + "}", charge_only(),
      stamped_entity(), stamped_controller(), simulation::NpcCatalogue::create(npc_kinds));
}

[[nodiscard]] protocol::CommandDecodeResult decode_shield(const std::string_view payload) {
  const std::vector<std::string> npc_kinds = published_npc_kinds();
  return protocol::decode_command_envelope(
      std::string{R"({"kind":"shield","payload":)"} + std::string{payload} + "}", shield_only(),
      stamped_entity(), stamped_controller(), simulation::NpcCatalogue::create(npc_kinds));
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
      simulation::EntityId::create(4'242), stamped_controller(),
      simulation::NpcCatalogue::create(npc_kinds));

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
  CHECK(protocol::client_command_wire_name(simulation::CommandKind::kShield) == "shield");
  CHECK(protocol::client_command_wire_name(simulation::CommandKind::kCharge) == "charge");
}

TEST_CASE("The closed v3 client command vocabulary is ascending and selects one kind per name",
          "[unit][protocol][v3][decoding][vocabulary]") {
  // Eight since Step 19's `charge`. The two static_asserts in `command_wire_kind.hpp` check only
  // the count and that every published name selects *a* kind, and a consistently wrong permutation
  // satisfies both -- so the mapping is written out here name by name, and **this is the only place
  // it is checked**.
  //
  // Step 18 inserted `"shield"` between `set_thrust` and `start_match` and moved one index. Step 19
  // inserted `"charge"` ahead of `"clear_seat"` -- `h` precedes `l` -- which moved all seven:
  // clear_seat 0->1, seat_npc 1->2, set_movement_tuning 2->3, set_seat_count 3->4, set_thrust 4->5,
  // shield 5->6, start_match 6->7. Each `client_command_kind_of_wire_name` line below is one of
  // those indices read back through `CommandWireKind`, so an index left behind fails here by
  // selecting a neighbour's kind rather than by failing to compile.
  CHECK(protocol::kV3ClientCommandKindNames.size() == 8);
  CHECK(std::ranges::is_sorted(protocol::kV3ClientCommandKindNames));
  CHECK(protocol::kV3ClientCommandKindNames ==
        std::array<std::string_view, 8>{"charge", "clear_seat", "seat_npc", "set_movement_tuning",
                                        "set_seat_count", "set_thrust", "shield", "start_match"});

  CHECK(protocol::client_command_kind_of_wire_name("charge") == simulation::CommandKind::kCharge);
  CHECK(protocol::client_command_kind_of_wire_name("clear_seat") ==
        simulation::CommandKind::kClearSeat);
  CHECK(protocol::client_command_kind_of_wire_name("seat_npc") ==
        simulation::CommandKind::kSeatNpc);
  CHECK(protocol::client_command_kind_of_wire_name("set_movement_tuning") ==
        simulation::CommandKind::kSetMovementTuning);
  CHECK(protocol::client_command_kind_of_wire_name("set_seat_count") ==
        simulation::CommandKind::kSetSeatCount);
  CHECK(protocol::client_command_kind_of_wire_name("set_thrust") ==
        simulation::CommandKind::kThrust);
  CHECK(protocol::client_command_kind_of_wire_name("shield") == simulation::CommandKind::kShield);
  CHECK(protocol::client_command_kind_of_wire_name("start_match") ==
        simulation::CommandKind::kStartMatch);

  CHECK(protocol::is_v3_client_command_kind("charge"));
  CHECK(protocol::is_v3_client_command_kind("shield"));
  CHECK_FALSE(protocol::client_command_kind_of_wire_name("shielded").has_value());
  CHECK_FALSE(protocol::client_command_kind_of_wire_name("charging").has_value());
  CHECK_FALSE(protocol::client_command_kind_of_wire_name("").has_value());
}

TEST_CASE("Shield decoder accepts a spelled generation and stamps the session's own entity",
          "[unit][protocol][v3][decoding][shield]") {
  const protocol::CommandDecodeResult positive = decode_shield(R"({"input_generation":100})");
  REQUIRE(positive.is_accepted());
  const auto* const pulse = std::get_if<simulation::ShieldCommand>(&*positive.command());
  REQUIRE(pulse != nullptr);
  CHECK(pulse->entity == stamped_entity());
  CHECK(pulse->input_generation == simulation::TickSequence::create(100));

  // `null` is the wire's spelling of the initial generation and decodes to absence -- the same
  // value an omitted `input_generation` gives a `set_thrust`. The member is required here rather
  // than optional because a pulse carries nothing else: an omitted key would leave `{}`, which is
  // also what a client meaning the initial generation would send, and the two must not be the same
  // bytes.
  const protocol::CommandDecodeResult initial = decode_shield(R"({"input_generation":null})");
  REQUIRE(initial.is_accepted());
  const auto* const initial_pulse = std::get_if<simulation::ShieldCommand>(&*initial.command());
  REQUIRE(initial_pulse != nullptr);
  CHECK(initial_pulse->entity == stamped_entity());
  CHECK_FALSE(initial_pulse->input_generation.has_value());

  // The top of the exact tick domain still round-trips: the bound is the protocol's safe integer,
  // not a double.
  const protocol::CommandDecodeResult maximum =
      decode_shield(R"({"input_generation":9007199254740991})");
  REQUIRE(maximum.is_accepted());
  CHECK(std::get<simulation::ShieldCommand>(*maximum.command()).input_generation ==
        simulation::TickSequence::create(simulation::TickSequence::kMaximumValue));
}

TEST_CASE("Shield decoder refuses an omitted, extra, or out-of-range generation member",
          "[unit][protocol][v3][decoding][shield][rejection]") {
  const auto require_shield_rejection = [](const std::string_view payload,
                                           const protocol::CommandDecodeRejection expected) {
    CAPTURE(payload);
    const protocol::CommandDecodeResult result = decode_shield(payload);
    CHECK_FALSE(result.is_accepted());
    CHECK(result.rejection() == expected);
    CHECK_FALSE(result.command().has_value());
  };

  // The key is required, so an empty payload is not "the initial generation" -- it is a payload
  // that failed to say which generation it meant, and the boundary cannot guess.
  require_shield_rejection("{}", protocol::CommandDecodeRejection::kPayloadInvalid);
  require_shield_rejection(R"({"generation":100})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);
  // Closed, exactly as `set_thrust` is: an entity id is a shape the payload does not have rather
  // than a member to ignore, which is what makes actor spoofing unexpressible instead of filtered.
  require_shield_rejection(R"({"input_generation":100,"entity_id":9})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);

  // A present zero is the release token, and it is invalid on a pulse for the same reason it is on
  // a thrust: the wire's minimum is 1, and absence is spelled `null`.
  require_shield_rejection(R"({"input_generation":0})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);
  require_shield_rejection(R"({"input_generation":-1})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);
  // Refused rather than truncated: the schema types this member as an integer.
  require_shield_rejection(R"({"input_generation":1.5})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);
  require_shield_rejection(R"({"input_generation":100.0})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);
  require_shield_rejection(R"({"input_generation":"100"})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);
  require_shield_rejection(R"({"input_generation":true})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);
  require_shield_rejection(R"({"input_generation":[]})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);
  // Past the exact safe integer, where a double would silently round.
  require_shield_rejection(R"({"input_generation":9007199254740992})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);

  // A non-object payload never reaches the shield arm at all: the envelope walk answers
  // `kMalformed` before any kind's closed schema runs, so the shape of the frame and the shape of
  // the payload stay two separate answers.
  require_shield_rejection("[]", protocol::CommandDecodeRejection::kMalformed);
  require_shield_rejection("null", protocol::CommandDecodeRejection::kMalformed);
  require_shield_rejection("100", protocol::CommandDecodeRejection::kMalformed);
}

TEST_CASE("A mode whose accepted mask omits the shield refuses the pulse before its payload",
          "[unit][protocol][v3][decoding][shield][rejection]") {
  // Admission-order step 6's second half, and the reason `welcome` is an advertisement rather than
  // an enforcement point. `require_rejection` carries the thrust-only mask, so a perfectly valid
  // shield envelope is `kKindRejected` -- the mode's answer -- and an invalid one is refused for
  // exactly the same reason, which keeps the refusal from telling a prober which payloads this
  // build understands.
  require_rejection(R"({"kind":"shield","payload":{"input_generation":null}})",
                    protocol::CommandDecodeRejection::kKindRejected);
  require_rejection(R"({"kind":"shield","payload":{"input_generation":0}})",
                    protocol::CommandDecodeRejection::kKindRejected);
}

TEST_CASE("Charge decoder accepts both payload shapes and stamps the session's own entity",
          "[unit][protocol][v3][decoding][charge]") {
  const protocol::CommandDecodeResult spelled =
      decode_charge(R"({"x":1,"y":0,"input_generation":100})");
  REQUIRE(spelled.is_accepted());
  const auto* const activation = std::get_if<simulation::ChargeCommand>(&*spelled.command());
  REQUIRE(activation != nullptr);
  CHECK(activation->entity == stamped_entity());
  CHECK(activation->direction == simulation::Vector2::create(1.0, 0.0));
  CHECK(activation->input_generation == simulation::TickSequence::create(100));

  // **`input_generation` is optional here, and that is the deliberate difference from `shield`.**
  // The tree's recorded discriminator keys the choice on payload shape rather than on the two
  // commands being one ability family: a shield's token is required-and-nullable only because a
  // pulse carries nothing else, so an omitted key would leave `{}` and collide with the spelling of
  // the initial generation. A charge always carries `x` and `y`, so an omitted key is unambiguously
  // "never invalidated", exactly as it is on a `set_thrust`
  // (`docs/reviews/2026-09-12-charge-contract.md` § "The component and the command").
  const protocol::CommandDecodeResult omitted = decode_charge(R"({"x":0,"y":-1})");
  REQUIRE(omitted.is_accepted());
  const auto* const initial = std::get_if<simulation::ChargeCommand>(&*omitted.command());
  REQUIRE(initial != nullptr);
  CHECK(initial->entity == stamped_entity());
  CHECK(initial->direction == simulation::Vector2::create(0.0, -1.0));
  CHECK_FALSE(initial->input_generation.has_value());

  // A `null` generation is *not* the shield's spelling of absence here: this member is optional, so
  // the closed shape has no null branch and a client that writes one has written a non-integer.
  const protocol::CommandDecodeResult spelled_null =
      decode_charge(R"({"x":1,"y":0,"input_generation":null})");
  CHECK_FALSE(spelled_null.is_accepted());
  CHECK(spelled_null.rejection() == protocol::CommandDecodeRejection::kPayloadInvalid);

  // The top of the exact tick domain still round-trips: the bound is the protocol's safe integer,
  // not a double.
  const protocol::CommandDecodeResult maximum =
      decode_charge(R"({"x":0,"y":1,"input_generation":9007199254740991})");
  REQUIRE(maximum.is_accepted());
  CHECK(std::get<simulation::ChargeCommand>(*maximum.command()).input_generation ==
        simulation::TickSequence::create(simulation::TickSequence::kMaximumValue));
}

TEST_CASE("Charge decoder carries the direction verbatim and normalizes nothing",
          "[unit][protocol][v3][decoding][charge]") {
  // A half-length vector is carried at half length. That is not a shortfall the boundary should
  // repair: `gameplay::unit_direction` is the one normalizer, and it turns this into a full-gain
  // burst to the right, which is what keeps pointer distance from becoming charge strength. A
  // decoder that normalized here as well would scale twice and would not be bit-identical to
  // scaling once -- the same argument `decode_set_thrust` makes for not clamping
  // (`docs/reviews/2026-09-12-charge-contract.md` § "Direction").
  const protocol::CommandDecodeResult subunit = decode_charge(R"({"x":0.5,"y":0})");
  REQUIRE(subunit.is_accepted());
  CHECK(std::get<simulation::ChargeCommand>(*subunit.command()).direction ==
        simulation::Vector2::create(0.5, 0.0));

  // The per-component bound is `unit_interval_scalar`, shared verbatim with `set_thrust`, so it
  // admits `(1, 1)` whose magnitude is sqrt(2). The wire's job is the component domain; the
  // magnitude is the tick's.
  const protocol::CommandDecodeResult diagonal = decode_charge(R"({"x":1,"y":1})");
  REQUIRE(diagonal.is_accepted());
  CHECK(std::get<simulation::ChargeCommand>(*diagonal.command()).direction ==
        simulation::Vector2::create(1.0, 1.0));

  // **A zero direction is a decode success and a tick-level refusal**, and the split is the point.
  // `unit_direction` answers `std::nullopt` for it and `AbilitySystem` turns that into a silent
  // no-op consuming no cooldown. Rejecting it here instead would close the connection of a client
  // whose aim vector happened to be at rest.
  const protocol::CommandDecodeResult at_rest = decode_charge(R"({"x":0,"y":0})");
  REQUIRE(at_rest.is_accepted());
  CHECK(std::get<simulation::ChargeCommand>(*at_rest.command()).direction ==
        simulation::Vector2::create(0.0, 0.0));

  // The same is true of a direction too short to normalize but not exactly zero: `1e-200` is an
  // ordinary finite double inside the unit interval, so it passes this boundary and `InputBatch`
  // intact, and it is `unit_direction` -- where the squared magnitude underflows to zero -- that
  // refuses it. The refusal band is wider than the value zero, and none of it lives here.
  const protocol::CommandDecodeResult unnormalizable = decode_charge(R"({"x":1e-200,"y":0})");
  REQUIRE(unnormalizable.is_accepted());
  CHECK(std::get<simulation::ChargeCommand>(*unnormalizable.command()).direction ==
        simulation::Vector2::create(1e-200, 0.0));
}

TEST_CASE("Charge decoder refuses a missing component, an extra member, or a bad generation",
          "[unit][protocol][v3][decoding][charge][rejection]") {
  const auto require_charge_rejection = [](const std::string_view payload,
                                           const protocol::CommandDecodeRejection expected) {
    CAPTURE(payload);
    const protocol::CommandDecodeResult result = decode_charge(payload);
    CHECK_FALSE(result.is_accepted());
    CHECK(result.rejection() == expected);
    CHECK_FALSE(result.command().has_value());
  };

  // Both components are required, so neither is defaulted to zero: a payload that named only one
  // axis is a client that failed to say where it meant to go, not a charge straight up the other.
  require_charge_rejection(R"({"y":1})", protocol::CommandDecodeRejection::kPayloadInvalid);
  require_charge_rejection(R"({"x":1})", protocol::CommandDecodeRejection::kPayloadInvalid);
  require_charge_rejection("{}", protocol::CommandDecodeRejection::kPayloadInvalid);

  // Closed, exactly as `set_thrust` is: an entity id is a shape the payload does not have rather
  // than a member to ignore, which is what makes actor spoofing unexpressible instead of filtered.
  require_charge_rejection(R"({"x":1,"y":0,"entity_id":9})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);
  require_charge_rejection(R"({"x":1,"y":0,"strength":2})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);
  require_charge_rejection(R"({"x":1,"y":0,"input_generation":100,"entity_id":9})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);

  // Outside the shared `unit_interval_scalar`. The bound is per component, so `1.0000001` is
  // refused while `(1, 1)` is not.
  require_charge_rejection(R"({"x":1.0000001,"y":0})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);
  require_charge_rejection(R"({"x":0,"y":-1.5})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);
  require_charge_rejection(R"({"x":"1","y":0})", protocol::CommandDecodeRejection::kPayloadInvalid);
  require_charge_rejection(R"({"x":true,"y":0})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);

  // A present zero is the release token, and it is invalid on an activation for the same reason it
  // is on a thrust: the wire's minimum is 1.
  require_charge_rejection(R"({"x":1,"y":0,"input_generation":0})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);
  require_charge_rejection(R"({"x":1,"y":0,"input_generation":-1})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);
  // Refused rather than truncated: the schema types this member as an integer.
  require_charge_rejection(R"({"x":1,"y":0,"input_generation":1.5})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);
  require_charge_rejection(R"({"x":1,"y":0,"input_generation":100.0})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);
  // Past the exact safe integer, where a double would silently round.
  require_charge_rejection(R"({"x":1,"y":0,"input_generation":9007199254740992})",
                           protocol::CommandDecodeRejection::kPayloadInvalid);

  // A non-finite component never reaches the charge arm: `client_parse_options` leaves every
  // `allow_*` false, so the non-standard literals are a parse failure and the answer is the
  // envelope's `kMalformed` rather than this payload's `kPayloadInvalid`. Keeping the two answers
  // separate is what stops a prober learning which kinds this build understands from the
  // difference.
  require_charge_rejection(R"({"x":NaN,"y":0})", protocol::CommandDecodeRejection::kMalformed);
  require_charge_rejection(R"({"x":Infinity,"y":0})", protocol::CommandDecodeRejection::kMalformed);
  require_charge_rejection(R"({"x":0,"y":-Infinity})",
                           protocol::CommandDecodeRejection::kMalformed);

  // A non-object payload is refused by the envelope walk before any kind's closed schema runs, so
  // the shape of the frame and the shape of the payload stay two separate answers.
  require_charge_rejection("[1,0]", protocol::CommandDecodeRejection::kMalformed);
  require_charge_rejection("null", protocol::CommandDecodeRejection::kMalformed);
  require_charge_rejection("100", protocol::CommandDecodeRejection::kMalformed);
}

TEST_CASE("A mode whose accepted mask omits the charge refuses it before its payload",
          "[unit][protocol][v3][decoding][charge][rejection]") {
  // Admission-order step 6's second half, and the reason `welcome` is an advertisement rather than
  // an enforcement point. `require_rejection` carries the thrust-only mask, so a perfectly valid
  // charge envelope is `kKindRejected` -- the mode's answer -- and an invalid one is refused for
  // exactly the same reason.
  require_rejection(R"({"kind":"charge","payload":{"x":1,"y":0}})",
                    protocol::CommandDecodeRejection::kKindRejected);
  require_rejection(R"({"kind":"charge","payload":{"x":1}})",
                    protocol::CommandDecodeRejection::kKindRejected);
}

TEST_CASE("Command decoder rejects a registered kind the running mode does not accept",
          "[unit][protocol][v3][decoding][rejection]") {
  const std::vector<std::string> npc_kinds = published_npc_kinds();
  const protocol::CommandDecodeResult result = protocol::decode_command_envelope(
      R"({"kind":"set_thrust","payload":{"x":0,"y":0}})", simulation::CommandKindMask::none(),
      stamped_entity(), stamped_controller(), simulation::NpcCatalogue::create(npc_kinds));

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
                                          stamped_controller(),
                                          simulation::NpcCatalogue::create(npc_kinds))
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
      stamped_entity(), stamped_controller(), simulation::NpcCatalogue::create(with_a_new_bot));
  REQUIRE(accepted.is_accepted());
  CHECK(std::get<simulation::SeatNpcCommand>(*accepted.command()).kind ==
        std::string_view{"ambusher"});

  // And a server that registers no bot at all accepts none, rather than falling back to a name it
  // could not build.
  const std::vector<std::string> no_bots;
  const protocol::CommandDecodeResult refused = protocol::decode_command_envelope(
      R"({"kind":"seat_npc","payload":{"seat_index":1,"npc_kind":"wanderer"}})", lobby_kinds(),
      stamped_entity(), stamped_controller(), simulation::NpcCatalogue::create(no_bots));
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
