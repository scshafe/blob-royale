#include "command_decoding.hpp"
#include "commands/set_movement_tuning_command.hpp"
#include "protocol_v3_test_fixture.hpp"

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace protocol = blob_royale::protocol;
namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::protocol::v3_test_fixture;

namespace {

[[nodiscard]] boost::json::object tuning_envelope() {
  return {{"kind", "set_movement_tuning"},
          {"payload", boost::json::parse(
                          fixture::read_v3_golden_example("set-movement-tuning-command.json"))}};
}

[[nodiscard]] protocol::CommandDecodeResult decode_tuning(const boost::json::object& envelope) {
  return protocol::decode_command_envelope(
      boost::json::serialize(envelope), simulation::CommandKindMask::all(),
      simulation::EntityId::create(7), simulation::ControllerId::create(3), {});
}

void require_invalid_payload(const boost::json::object& envelope) {
  const auto decoded = decode_tuning(envelope);
  CHECK(decoded.rejection() == protocol::CommandDecodeRejection::kPayloadInvalid);
  CHECK(decoded.close_code() == 1008);
  CHECK_FALSE(decoded.command().has_value());
}

} // namespace

TEST_CASE(
    "tuning command decoder preserves exact absolute boundaries and stamps only its controller",
    "[unit][protocol][v3][movement_tuning][decoding]") {
  for (const double acceleration : {0.0, 10'000.0}) {
    for (const double speed : {1.0, 10'000.0}) {
      for (const auto request : {std::uint64_t{1}, protocol::kMaximumSafeInteger}) {
        for (const auto revision : {std::uint64_t{0}, protocol::kMaximumSafeInteger}) {
          auto envelope = tuning_envelope();
          auto& payload = envelope.at("payload").as_object();
          payload.at("tuning_request_id") = request;
          payload.at("expected_revision") = revision;
          payload.at("acceleration_world_units_per_second_squared") = acceleration;
          payload.at("normal_top_speed_world_units_per_second") = speed;
          const auto decoded = decode_tuning(envelope);
          REQUIRE(decoded.is_accepted());
          const auto& command = std::get<simulation::SetMovementTuningCommand>(*decoded.command());
          CHECK(command.controller == simulation::ControllerId::create(3));
          CHECK(command.tuning_request_id == request);
          CHECK(command.expected_revision == revision);
          CHECK(command.tuning.acceleration() == acceleration);
          CHECK(command.tuning.normal_top_speed() == speed);
        }
      }
    }
  }
}

TEST_CASE("tuning command decoder rejects missing members and forged routing identity",
          "[unit][protocol][v3][movement_tuning][rejection]") {
  for (const std::string_view field :
       {"tuning_request_id", "expected_revision", "acceleration_world_units_per_second_squared",
        "normal_top_speed_world_units_per_second"}) {
    auto envelope = tuning_envelope();
    envelope.at("payload").as_object().erase(field);
    require_invalid_payload(envelope);
  }
  for (const std::string_view field : {"controller_id", "entity_id", "lobby_id", "reset"}) {
    auto envelope = tuning_envelope();
    envelope.at("payload").as_object().emplace(field, 1);
    require_invalid_payload(envelope);
  }
}

TEST_CASE("tuning command decoder rejects illegal scalar types and intrinsic limits",
          "[unit][protocol][v3][movement_tuning][rejection]") {
  for (const std::string_view field :
       {"acceleration_world_units_per_second_squared", "normal_top_speed_world_units_per_second"}) {
    for (const std::string_view invalid :
         {"null", "true", "\"400\"", "[]", "{}", "-1", "10001", "1e300"}) {
      auto envelope = tuning_envelope();
      envelope.at("payload").as_object().at(field) = boost::json::parse(invalid);
      require_invalid_payload(envelope);
    }
  }
  auto envelope = tuning_envelope();
  envelope.at("payload").as_object().at("normal_top_speed_world_units_per_second") = 0;
  require_invalid_payload(envelope);
}

TEST_CASE("tuning command decoder rejects unsafe fractional and wrongly typed correlation numbers",
          "[unit][protocol][v3][movement_tuning][rejection]") {
  for (const std::string_view field : {"tuning_request_id", "expected_revision"}) {
    for (const std::string_view invalid : {"null", "true", "\"1\"", "[]", "-1", "1.5", "1.0",
                                           "9007199254740992", "18446744073709551615"}) {
      auto envelope = tuning_envelope();
      envelope.at("payload").as_object().at(field) = boost::json::parse(invalid);
      require_invalid_payload(envelope);
    }
  }
  auto envelope = tuning_envelope();
  envelope.at("payload").as_object().at("tuning_request_id") = 0;
  require_invalid_payload(envelope);
}

TEST_CASE("tuning command stays unavailable when the mode mask omits it",
          "[unit][protocol][v3][movement_tuning][rejection]") {
  const auto decoded = protocol::decode_command_envelope(
      boost::json::serialize(tuning_envelope()),
      simulation::CommandKindMask::create({simulation::CommandKind::kThrust}),
      simulation::EntityId::create(7), simulation::ControllerId::create(3), {});
  CHECK(decoded.rejection() == protocol::CommandDecodeRejection::kKindRejected);
}
