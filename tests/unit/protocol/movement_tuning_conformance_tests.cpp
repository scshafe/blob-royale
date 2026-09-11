#include "fixtures/movement_tuning_wire_result_fixture.hpp"
#include "protocol_v3_test_fixture.hpp"

#include "protocol_v3_frame_conformance.hpp"
#include "protocol_v3_json_encoding.hpp"

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/string_view.hpp>
#include <boost/json/value.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace protocol = blob_royale::protocol;
namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::protocol::v3_test_fixture;
namespace result_fixture = blob_royale::protocol::test_fixture;
namespace json = boost::json;

namespace {

constexpr result_fixture::MovementTuningWireResultInputs kCoveredInputs{};
constexpr std::uint64_t kCoveredTick = *kCoveredInputs.decision_tick;
constexpr std::uint64_t kCoveredRevision = *kCoveredInputs.revision;
constexpr std::size_t kCoverageIntegerFieldCount = 5;

// Reuse the protocol fixtures to obtain valid emitted bytes, then mutate only their parsed wire
// representation. The production encoder never sees the invalid values being checked below.
[[nodiscard]] json::value snapshot_document(
    const std::optional<protocol::MovementTuningWireResult>& result = kCoveredInputs.create(),
    const std::uint64_t later_by = 0) {
  simulation::MovementTuningState movement;
  movement.revision =
      std::max(kCoveredRevision + later_by, result ? result->revision().value_or(0) : 0);
  movement.effective_tick = simulation::TickSequence::create(kCoveredTick + later_by);
  return json::parse(protocol::encode_snapshot_message_v3(
      fixture::tuning_snapshot(movement, kCoveredTick + later_by), fixture::golden_directory(),
      result, fixture::session_request_id(), fixture::kSnapshotMessageSequence,
      fixture::kSnapshotTimestamp));
}

[[nodiscard]] json::object& snapshot_data(json::value& document) {
  return document.as_object().at("data").as_object();
}

[[nodiscard]] json::object& movement_data(json::value& document) {
  return snapshot_data(document).at("match").as_object().at("movement").as_object();
}

[[nodiscard]] json::object& result_data(json::value& document) {
  return snapshot_data(document).at("tuning_result").as_object();
}

using CoverageField = std::pair<json::object*, std::string_view>;

[[nodiscard]] std::array<CoverageField, kCoverageIntegerFieldCount>
coverage_integer_fields(json::value& document) {
  return {{{&snapshot_data(document), "tick_sequence"},
           {&movement_data(document), "revision"},
           {&movement_data(document), "effective_tick"},
           {&result_data(document), "decision_tick"},
           {&result_data(document), "revision"}}};
}

void check_coverage_rejected(const json::value& document) {
  CHECK(protocol::check_v3_server_frame(json::serialize(document)) ==
        protocol::V3FrameConformance::kMovementTuningCoverageInvalid);
}

} // namespace

TEST_CASE("movement conformance accepts a required null tuning result",
          "[unit][protocol][v3][movement_tuning][conformance]") {
  const auto document = snapshot_document(std::nullopt);
  CHECK(protocol::check_v3_server_frame(json::serialize(document)) ==
        protocol::V3FrameConformance::kConforms);
}

TEST_CASE("movement conformance accepts every covered committed and admission status",
          "[unit][protocol][v3][movement_tuning][conformance]") {
  for (const auto& specimen : result_fixture::kMovementTuningWireStatuses) {
    CAPTURE(specimen.status_name);
    const auto document = snapshot_document(specimen.inputs.create());
    CHECK(protocol::check_v3_server_frame(json::serialize(document)) ==
          protocol::V3FrameConformance::kConforms);
  }
}

TEST_CASE("movement conformance accepts an older decision in a later revision snapshot",
          "[unit][protocol][v3][movement_tuning][conformance]") {
  auto document = snapshot_document(kCoveredInputs.create(), 1);
  CHECK(result_data(document).at("decision_tick").to_number<std::uint64_t>() == kCoveredTick);
  CHECK(result_data(document).at("revision").to_number<std::uint64_t>() == kCoveredRevision);
  CHECK(protocol::check_v3_server_frame(json::serialize(document)) ==
        protocol::V3FrameConformance::kConforms);
}

TEST_CASE("movement conformance rejects an encoded effective tick beyond its snapshot",
          "[unit][protocol][v3][movement_tuning][conformance][rejection]") {
  auto document = snapshot_document();
  movement_data(document).at("effective_tick") = kCoveredTick + 1;
  check_coverage_rejected(document);
}

TEST_CASE("movement conformance rejects an encoded decision tick beyond its snapshot",
          "[unit][protocol][v3][movement_tuning][conformance][rejection]") {
  auto document = snapshot_document();
  result_data(document).at("decision_tick") = kCoveredTick + 1;
  check_coverage_rejected(document);
}

TEST_CASE("movement conformance rejects an encoded decision revision beyond movement revision",
          "[unit][protocol][v3][movement_tuning][conformance][rejection]") {
  auto document = snapshot_document();
  result_data(document).at("revision") = kCoveredRevision + 1;
  check_coverage_rejected(document);
  CHECK(protocol::v3_frame_conformance_name(
            protocol::V3FrameConformance::kMovementTuningCoverageInvalid) ==
        "movement_tuning_coverage_invalid");
}

TEST_CASE("movement conformance guards absent and nonobject coverage containers",
          "[unit][protocol][v3][movement_tuning][conformance][rejection]") {
  auto missing_movement = snapshot_document();
  snapshot_data(missing_movement).at("match").as_object().erase("movement");
  check_coverage_rejected(missing_movement);
  auto missing_result = snapshot_document();
  snapshot_data(missing_result).erase("tuning_result");
  check_coverage_rejected(missing_result);
  for (const auto& invalid :
       {json::value{false}, json::value{0}, json::value{"invalid"}, json::value{json::array{}}}) {
    auto movement = snapshot_document();
    snapshot_data(movement).at("match").as_object().at("movement") = invalid;
    check_coverage_rejected(movement);
    auto result = snapshot_document();
    snapshot_data(result).at("tuning_result") = invalid;
    check_coverage_rejected(result);
  }
  auto null_movement = snapshot_document();
  snapshot_data(null_movement).at("match").as_object().at("movement") = nullptr;
  check_coverage_rejected(null_movement);
}

TEST_CASE("movement conformance guards every missing integer before comparing coverage",
          "[unit][protocol][v3][movement_tuning][conformance][rejection]") {
  for (std::size_t index = 0; index < kCoverageIntegerFieldCount; ++index) {
    CAPTURE(index);
    auto document = snapshot_document();
    const auto [object, name] = coverage_integer_fields(document)[index];
    object->erase(json::string_view{name.data(), name.size()});
    check_coverage_rejected(document);
  }
}

TEST_CASE("movement conformance guards nonintegers and unsafe values without numeric truncation",
          "[unit][protocol][v3][movement_tuning][conformance][rejection]") {
  for (std::size_t index = 0; index < kCoverageIntegerFieldCount; ++index) {
    for (const auto& invalid :
         {json::value{nullptr}, json::value{false}, json::value{"invalid"},
          json::value{json::object{}}, json::value{json::array{}}, json::value{-1},
          json::value{0.5}, json::value{protocol::kMaximumSafeInteger + 1}}) {
      CAPTURE(index, invalid);
      auto document = snapshot_document();
      const auto [object, name] = coverage_integer_fields(document)[index];
      object->at(json::string_view{name.data(), name.size()}) = invalid;
      check_coverage_rejected(document);
    }
  }
}
