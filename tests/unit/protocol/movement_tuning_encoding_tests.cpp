#include "fixtures/movement_tuning_wire_result_fixture.hpp"
#include "protocol_v3_test_fixture.hpp"

#include "movement_tuning_wire_result.hpp"
#include "protocol_v3_json_encoding.hpp"

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace protocol = blob_royale::protocol;
namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::protocol::v3_test_fixture;
namespace result_fixture = blob_royale::protocol::test_fixture;

namespace {

[[nodiscard]] std::string
encode(const simulation::WorldSnapshot& snapshot,
       const std::optional<protocol::MovementTuningWireResult>& result = std::nullopt) {
  return protocol::encode_snapshot_message_v3(
      snapshot, fixture::golden_directory(), result, fixture::session_request_id(),
      fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp);
}

} // namespace

TEST_CASE("movement encoder publishes current defaults limits and zero initial revision in order",
          "[unit][protocol][v3][movement_tuning][encoding]") {
  simulation::MovementTuningState movement;
  movement.current = simulation::MovementTuning::create(0, 1);
  movement.defaults = simulation::MovementTuning::create(10'000, 10'000);
  const std::string encoded = encode(fixture::tuning_snapshot(movement));
  CHECK(
      encoded.find(
          R"("start_requested":false,"movement":{"current":{"acceleration_world_units_per_second_squared":0,"normal_top_speed_world_units_per_second":1,"charge_speed_fraction":7.5E-1,"lethal_spawn_rate_per_second":0,"nonlethal_spawn_rate_per_second":0},"defaults":{"acceleration_world_units_per_second_squared":10000,"normal_top_speed_world_units_per_second":10000,"charge_speed_fraction":7.5E-1,"lethal_spawn_rate_per_second":0,"nonlethal_spawn_rate_per_second":0},"limits":{"acceleration_world_units_per_second_squared":{"minimum":0,"maximum":10000},"normal_top_speed_world_units_per_second":{"minimum":1,"maximum":10000},"charge_speed_fraction":{"minimum":0,"maximum":100000000},"lethal_spawn_rate_per_second":{"minimum":0,"maximum":5},"nonlethal_spawn_rate_per_second":{"minimum":0,"maximum":5}},"revision":0,"effective_tick":0},"outcome":)") !=
      std::string::npos);
  const auto document = boost::json::parse(encoded);
  CHECK(document.as_object().at("data").as_object().at("tuning_result").is_null());
}

TEST_CASE(
    "movement result encoder preserves every status and exact field order with a covering snapshot",
    "[unit][protocol][v3][movement_tuning][encoding]") {
  for (const auto& specimen : result_fixture::kMovementTuningWireStatuses) {
    CAPTURE(specimen.status_name);
    simulation::MovementTuningState movement;
    movement.revision = specimen.inputs.revision.value_or(0);
    if (movement.revision != 0)
      movement.effective_tick = simulation::TickSequence::create(19);
    const auto snapshot = fixture::tuning_snapshot(movement, 19);
    const auto result = specimen.inputs.create();
    const std::string encoded = encode(snapshot, result);
    const auto document = boost::json::parse(encoded);
    const auto& data = document.as_object().at("data").as_object();
    const auto& value = data.at("tuning_result").as_object();
    CHECK(value.size() == 5);
    CHECK(value.at("tuning_request_id").to_number<std::uint64_t>() == result.tuning_request_id());
    CHECK(value.at("status").as_string() == result.status_name());
    CHECK(value.at("decision_tick").is_null() == !result.decision_tick().has_value());
    CHECK(value.at("revision").is_null() == !result.revision().has_value());
    CHECK(value.at("retry_after_milliseconds").is_null() ==
          !result.retry_after_milliseconds().has_value());
    auto member = value.begin();
    for (const std::string_view name :
         {"tuning_request_id", "status", "decision_tick", "revision", "retry_after_milliseconds"}) {
      REQUIRE(member != value.end());
      CHECK(member->key() == name);
      ++member;
    }
    CHECK(protocol::encode_snapshot_message_v3(
              snapshot, fixture::golden_directory(), result, fixture::session_request_id(),
              fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp,
              encoded.size()) == encoded);
    fixture::require_protocol_error_code(
        [&] {
          return protocol::encode_snapshot_message_v3(
              snapshot, fixture::golden_directory(), result, fixture::session_request_id(),
              fixture::kSnapshotMessageSequence, fixture::kSnapshotTimestamp, encoded.size() - 1);
        },
        protocol::ProtocolEncodingErrorCode::kEncodedPayloadTooLarge);
  }
}

TEST_CASE("movement encoder publishes authored room caps and distinct five-setting values",
          "[unit][protocol][v3][movement_tuning][encoding]") {
  simulation::MovementTuningState movement;
  movement.current = simulation::MovementTuning::create(500, 750, 1.25, 0.25, 0);
  movement.defaults = simulation::MovementTuning::create(400, 600, 0.75, 0.125, 0);
  movement.charge_speed_fraction_maximum = 2;
  movement.lethal_spawn_rate_per_second_maximum = 0.5;
  movement.nonlethal_spawn_rate_per_second_maximum = 0;
  const auto document = boost::json::parse(encode(fixture::tuning_snapshot(movement)));
  const auto& published = document.as_object()
                              .at("data")
                              .as_object()
                              .at("match")
                              .as_object()
                              .at("movement")
                              .as_object();
  CHECK(
      published.at("current") ==
      boost::json::parse(
          R"({"acceleration_world_units_per_second_squared":500,"normal_top_speed_world_units_per_second":750,"charge_speed_fraction":1.25,"lethal_spawn_rate_per_second":0.25,"nonlethal_spawn_rate_per_second":0})"));
  CHECK(
      published.at("defaults") ==
      boost::json::parse(
          R"({"acceleration_world_units_per_second_squared":400,"normal_top_speed_world_units_per_second":600,"charge_speed_fraction":0.75,"lethal_spawn_rate_per_second":0.125,"nonlethal_spawn_rate_per_second":0})"));
  const auto& limits = published.at("limits").as_object();
  CHECK(limits.size() == 5);
  CHECK(limits.at("charge_speed_fraction") == boost::json::parse(R"({"minimum":0,"maximum":2})"));
  CHECK(limits.at("lethal_spawn_rate_per_second") ==
        boost::json::parse(R"({"minimum":0,"maximum":0.5})"));
  CHECK(limits.at("nonlethal_spawn_rate_per_second") ==
        boost::json::parse(R"({"minimum":0,"maximum":0})"));
}

TEST_CASE("movement encoder refuses invalid room maxima and settings outside those capabilities",
          "[unit][protocol][v3][movement_tuning][encoding][rejection]") {
  using State = simulation::MovementTuningState;
  for (auto member :
       {&State::charge_speed_fraction_maximum, &State::lethal_spawn_rate_per_second_maximum,
        &State::nonlethal_spawn_rate_per_second_maximum}) {
    for (const double invalid : {-1.0, 100'000'001.0, std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()}) {
      State movement;
      movement.*member = invalid;
      fixture::require_protocol_error_code(
          [&] { return encode(fixture::tuning_snapshot(movement)); },
          protocol::ProtocolEncodingErrorCode::kMovementTuningStateInvalid);
    }
  }
  for (auto member : {&State::current, &State::defaults}) {
    for (const auto tuning : {simulation::MovementTuning::create(400, 600, 2.01, 0, 0),
                              simulation::MovementTuning::create(400, 600, 0.75, 0.51, 0),
                              simulation::MovementTuning::create(400, 600, 0.75, 0, 0.01)}) {
      State movement;
      movement.charge_speed_fraction_maximum = 2;
      movement.lethal_spawn_rate_per_second_maximum = 0.5;
      movement.nonlethal_spawn_rate_per_second_maximum = 0;
      movement.*member = tuning;
      fixture::require_protocol_error_code(
          [&] { return encode(fixture::tuning_snapshot(movement)); },
          protocol::ProtocolEncodingErrorCode::kMovementTuningStateInvalid);
    }
  }
}

TEST_CASE("movement result encoder accepts a later snapshot without rewriting the older decision",
          "[unit][protocol][v3][movement_tuning][encoding]") {
  simulation::MovementTuningState movement;
  movement.revision = 8;
  movement.effective_tick = simulation::TickSequence::create(20);
  const auto result = result_fixture::MovementTuningWireResultInputs{}.create();
  const auto document = boost::json::parse(encode(fixture::tuning_snapshot(movement, 20), result));
  const auto& value = document.as_object().at("data").as_object().at("tuning_result").as_object();
  CHECK(value.at("revision").to_number<std::uint64_t>() == 7);
  CHECK(value.at("decision_tick").to_number<std::uint64_t>() == 19);
}

TEST_CASE(
    "movement result encoder rejects decision ticks and revisions not covered by the snapshot",
    "[unit][protocol][v3][movement_tuning][rejection]") {
  simulation::MovementTuningState movement;
  movement.revision = 1;
  movement.effective_tick = simulation::TickSequence::create(1);
  const auto snapshot = fixture::tuning_snapshot(movement);
  for (const auto& inputs :
       {result_fixture::MovementTuningWireResultInputs{
            1, protocol::MovementTuningWireResultStatus::kApplied, 2, 1, std::nullopt},
        result_fixture::MovementTuningWireResultInputs{
            1, protocol::MovementTuningWireResultStatus::kApplied, 1, 2, std::nullopt}}) {
    fixture::require_protocol_error_code(
        [&] { return encode(snapshot, inputs.create()); },
        protocol::ProtocolEncodingErrorCode::kMovementTuningResultInvalid);
  }
}

TEST_CASE("movement encoder rejects unsafe revisions and inconsistent effective ticks",
          "[unit][protocol][v3][movement_tuning][rejection]") {
  for (const auto revision :
       {protocol::kMaximumSafeInteger + 1, std::numeric_limits<std::uint64_t>::max()}) {
    simulation::MovementTuningState movement;
    movement.revision = revision;
    movement.effective_tick = simulation::TickSequence::create(1);
    fixture::require_protocol_error_code(
        [&] { return encode(fixture::tuning_snapshot(movement)); },
        protocol::ProtocolEncodingErrorCode::kMovementTuningStateInvalid);
  }
  for (const auto& [revision, tick] :
       {std::pair<std::uint64_t, std::uint64_t>{0, 1}, {1, 0}, {1, 2}}) {
    simulation::MovementTuningState movement;
    movement.revision = revision;
    movement.effective_tick =
        tick == 0 ? simulation::TickSequence::zero() : simulation::TickSequence::create(tick);
    fixture::require_protocol_error_code(
        [&] { return encode(fixture::tuning_snapshot(movement)); },
        protocol::ProtocolEncodingErrorCode::kMovementTuningStateInvalid);
  }
}
