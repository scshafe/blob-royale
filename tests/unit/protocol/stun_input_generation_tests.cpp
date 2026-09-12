#include "fixtures/stun_input_generation_fixture.hpp"

#include "command_decoding.hpp"
#include "protocol_v3_json_encoding.hpp"

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace protocol = blob_royale::protocol;
namespace simulation = blob_royale::simulation;
namespace fixture = protocol::stun_input_fixture;
namespace v3 = protocol::v3_test_fixture;

namespace {

[[nodiscard]] protocol::CommandDecodeResult decode_payload(const std::string_view payload) {
  return protocol::decode_command_envelope(
      std::string{R"({"kind":"set_thrust","payload":)"} + std::string{payload} + "}",
      simulation::CommandKindMask::create({simulation::CommandKind::kThrust}),
      simulation::EntityId::create(fixture::kEntity),
      simulation::ControllerId::create(fixture::kController), {});
}

[[nodiscard]] std::string encode(const simulation::WorldSnapshot& snapshot,
                                 const v3::StubControllerDirectory& directory) {
  return protocol::encode_snapshot_message_v3(snapshot, directory, std::nullopt,
                                              v3::session_request_id(),
                                              v3::kSnapshotMessageSequence, v3::kSnapshotTimestamp);
}

} // namespace

TEST_CASE("Thrust decoder preserves omitted and positive safe generation tokens verbatim",
          "[unit][protocol][v3][decoding][input_generation]") {
  const auto absent = decode_payload(R"({"x":1,"y":0})");
  REQUIRE(absent.is_accepted());
  CHECK_FALSE(std::get<simulation::ThrustCommand>(*absent.command()).input_generation.has_value());
  for (const auto generation : fixture::kAcceptedGenerations) {
    CAPTURE(generation);
    const auto result = decode_payload(std::string{R"({"x":1,"y":-0.5,"input_generation":)"} +
                                       std::to_string(generation) + "}");
    REQUIRE(result.is_accepted());
    const auto& thrust = std::get<simulation::ThrustCommand>(*result.command());
    CHECK(thrust.entity == simulation::EntityId::create(fixture::kEntity));
    CHECK(thrust.direction == simulation::Vector2::create(1.0, -0.5));
    CHECK(thrust.input_generation == simulation::TickSequence::create(generation));
  }
}

TEST_CASE("Thrust decoder rejects malformed generation tokens including zero releases",
          "[unit][protocol][v3][decoding][input_generation][rejection]") {
  for (const auto& specimen : fixture::kRejectedGenerations) {
    CAPTURE(specimen.name);
    const auto result = decode_payload(std::string{R"({"x":0,"y":0,"input_generation":)"} +
                                       std::string{specimen.encoded} + "}");
    CHECK(result.rejection() == protocol::CommandDecodeRejection::kPayloadInvalid);
    CHECK_FALSE(result.command().has_value());
  }
  for (const auto payload : fixture::kUnknownMemberPayloads) {
    CAPTURE(payload);
    CHECK(decode_payload(payload).rejection() == protocol::CommandDecodeRejection::kPayloadInvalid);
  }
}

TEST_CASE("Thrust generation decoder accepts the separate v3 positive example",
          "[unit][protocol][v3][decoding][input_generation][golden]") {
  const auto result =
      decode_payload(v3::read_v3_golden_example("set-thrust-input-generation-command.json"));
  REQUIRE(result.is_accepted());
  CHECK(std::get<simulation::ThrustCommand>(*result.command()).input_generation ==
        simulation::TickSequence::create(fixture::kActivation));
}

TEST_CASE("Status publication retains both window endpoints and generation while hiding input",
          "[unit][protocol][v3][encoding][stun][input_generation]") {
  const auto generation = simulation::TickSequence::create(fixture::kActivation);
  const auto private_link = fixture::private_controllable(generation);
  const auto public_link =
      simulation::ComponentPublication<simulation::Controllable>::published(private_link);
  REQUIRE_FALSE(private_link.commands_this_tick.empty());
  REQUIRE(private_link.normalized_thrust_intent.has_value());
  CHECK(public_link.commands_this_tick.empty());
  CHECK_FALSE(public_link.normalized_thrust_intent.has_value());
  CHECK(public_link.input_generation == generation);

  const auto window = simulation::TickWindow::create(generation, fixture::kDuration);
  const auto snapshot = fixture::snapshot(generation, simulation::Stun{window});
  REQUIRE(snapshot.components<simulation::Stun>().size() == 1);
  CHECK(snapshot.components<simulation::Stun>().front().value.window == window);
  CHECK(snapshot.components<simulation::Controllable>().front().value == public_link);
  const std::string encoded = encode(snapshot, fixture::directory());
  const auto document = boost::json::parse(encoded);
  const auto& components = document.as_object()
                               .at("data")
                               .as_object()
                               .at("entities")
                               .as_array()
                               .front()
                               .as_object()
                               .at("components")
                               .as_object();
  CHECK(components.at("controllable") == boost::json::parse(v3::read_v3_golden_example(
                                             "controllable-input-generation-component.json")));
  CHECK(components.at("stun") ==
        boost::json::parse(v3::read_v3_golden_example("stun-component.json")));
  CHECK(encoded.find("commands_this_tick") == std::string::npos);
  CHECK(encoded.find("normalized_thrust_intent") == std::string::npos);
  CHECK(encoded.find(R"("activation_tick":100,"expiry_tick":140)") != std::string::npos);
}

TEST_CASE(
    "Controllable generation encoding omits absence and retains it after directory retirement",
    "[unit][protocol][v3][encoding][input_generation]") {
  CHECK(encode(fixture::snapshot(std::nullopt), fixture::directory()).find("input_generation") ==
        std::string::npos);
  const auto snapshot = fixture::snapshot(simulation::TickSequence::create(fixture::kActivation));
  const std::string encoded = encode(snapshot, v3::StubControllerDirectory{});
  CHECK(encoded.find(
            R"("controller_kind":"unknown","display_name":"player-1","input_generation":100)") !=
        std::string::npos);
}

TEST_CASE("Status encoding accepts maximum safe expiry and rejects empty or zero-origin windows",
          "[unit][protocol][v3][encoding][stun][rejection]") {
  const auto activation = simulation::TickSequence::create(fixture::kActivation);
  const auto maximum_window = simulation::TickWindow::create(
      activation, simulation::TickSequence::kMaximumValue - fixture::kActivation);
  CHECK(
      encode(fixture::snapshot(activation, simulation::Stun{maximum_window}), fixture::directory())
          .find(R"("expiry_tick":9007199254740991)") != std::string::npos);
  for (const auto window : {simulation::TickWindow::create(activation, 0),
                            simulation::TickWindow::create(simulation::TickSequence::zero(), 1)}) {
    const auto snapshot = fixture::snapshot(activation, simulation::Stun{window});
    v3::require_protocol_error_code([&] { return encode(snapshot, fixture::directory()); },
                                    protocol::ProtocolEncodingErrorCode::kComponentValueOutOfRange);
  }
  const auto zero_generation = fixture::snapshot(simulation::TickSequence::zero());
  v3::require_protocol_error_code([&] { return encode(zero_generation, fixture::directory()); },
                                  protocol::ProtocolEncodingErrorCode::kComponentValueOutOfRange);
}
