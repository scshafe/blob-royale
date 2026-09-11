#include "protocol_v3_frame_conformance.hpp"

#include "protocol_v3_constants.hpp"
#include "simulation_limits.hpp"
#include "snake_case_identity.hpp"

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/string_view.hpp>
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace blob_royale::protocol {
namespace {

namespace json = boost::json;

[[nodiscard]] std::string_view view_of(const json::string& value) noexcept {
  return std::string_view{value.data(), value.size()};
}

[[nodiscard]] const json::value* member_of(const json::object& object,
                                           const std::string_view name) noexcept {
  return object.if_contains(json::string_view{name.data(), name.size()});
}

// The three v3 message identities. A frame naming anything else is a frame this schema set does not
// define, which a client must close on rather than interpret.
[[nodiscard]] bool is_v3_message_schema_id(const std::string_view schema_id) noexcept {
  return schema_id == kWelcomeMessageSchemaId || schema_id == kSnapshotMessageV3SchemaId ||
         schema_id == kErrorResponseV3SchemaId;
}

// Boost.JSON preserves negative floating zero but normalizes integer token `-0` to integer zero.
// This bounded pass over already-valid JSON retains that publication check without a second JSON
// parser. Quoted strings (including escaped quotes) are skipped; exponent digits do not change
// whether the mantissa denotes zero. Negative underflow is also rejected by the parsed sign check.
[[nodiscard]] bool has_negative_zero_token(const std::string_view encoded) noexcept {
  bool inside_string = false;
  for (std::size_t index = 0; index < encoded.size(); ++index) {
    const char character = encoded[index];
    if (inside_string) {
      if (character == '\\') {
        ++index;
      } else if (character == '"') {
        inside_string = false;
      }
      continue;
    }
    if (character == '"') {
      inside_string = true;
      continue;
    }
    const bool negative = character == '-';
    if (!negative && (character < '0' || character > '9')) {
      continue;
    }
    bool nonzero_mantissa = false;
    std::size_t number_end = index + (negative ? 1 : 0);
    for (; number_end < encoded.size(); ++number_end) {
      const char digit = encoded[number_end];
      if (digit == '.') {
        continue;
      }
      if (digit < '0' || digit > '9') {
        break;
      }
      nonzero_mantissa = nonzero_mantissa || digit != '0';
    }
    if (negative && !nonzero_mantissa) {
      return true;
    }
    if (number_end < encoded.size() && (encoded[number_end] == 'e' || encoded[number_end] == 'E')) {
      ++number_end;
      if (number_end < encoded.size() &&
          (encoded[number_end] == '+' || encoded[number_end] == '-')) {
        ++number_end;
      }
      while (number_end < encoded.size() && encoded[number_end] >= '0' &&
             encoded[number_end] <= '9') {
        ++number_end;
      }
    }
    index = number_end - 1;
  }
  return false;
}

[[nodiscard]] bool terrain_number(const json::object& object, const std::string_view name,
                                  double& result, const bool positive) {
  const json::value* const value = member_of(object, name);
  if (value == nullptr || !value->is_number()) {
    return false;
  }
  result = value->to_number<double>();
  return std::isfinite(result) && !std::signbit(result) &&
         result <= simulation::kMaximumWorldDimension && (positive ? result > 0.0 : result >= 0.0);
}

struct AuthoredTerrainPoint final {
  double x;
  double y;
};

[[nodiscard]] bool terrain_point(const json::value& value, const double width, const double height,
                                 AuthoredTerrainPoint& point) {
  return value.is_object() && terrain_number(value.get_object(), "x", point.x, false) &&
         terrain_number(value.get_object(), "y", point.y, false) && point.x <= width &&
         point.y <= height;
}

template <std::size_t MaximumCount>
[[nodiscard]] bool terrain_names_conform(const json::array& shapes) {
  if (shapes.size() > MaximumCount) {
    return false;
  }
  std::array<std::string_view, MaximumCount> names{};
  for (std::size_t index = 0; index < shapes.size(); ++index) {
    if (!shapes[index].is_object()) {
      return false;
    }
    const json::value* const name = member_of(shapes[index].get_object(), "name");
    if (name == nullptr || !name->is_string() ||
        !simulation::is_wire_kind_name(view_of(name->get_string()))) {
      return false;
    }
    names[index] = view_of(name->get_string());
    for (std::size_t earlier = 0; earlier < index; ++earlier) {
      if (names[earlier] == names[index]) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] bool welcome_terrain_conforms(const json::value& data) {
  if (!data.is_object()) {
    return false;
  }
  const json::value* const terrain = member_of(data.get_object(), "terrain");
  if (terrain == nullptr || !terrain->is_object()) {
    return false;
  }
  const json::object& authored = terrain->get_object();
  const json::value* const bounds = member_of(authored, "bounds");
  const json::value* const ground = member_of(authored, "ground");
  const json::value* const corridors = member_of(authored, "corridors");
  const json::value* const holes = member_of(authored, "holes");
  double width = 0.0;
  double height = 0.0;
  if (bounds == nullptr || !bounds->is_object() ||
      !terrain_number(bounds->get_object(), "width_world_units", width, true) ||
      !terrain_number(bounds->get_object(), "height_world_units", height, true) ||
      ground == nullptr || !ground->is_string() || corridors == nullptr || !corridors->is_array() ||
      holes == nullptr || !holes->is_array()) {
    return false;
  }
  const bool solid = view_of(ground->get_string()) == "solid";
  const bool corridor_ground = view_of(ground->get_string()) == "corridors";
  const json::array& authored_corridors = corridors->get_array();
  const json::array& authored_holes = holes->get_array();
  if ((!solid && !corridor_ground) || (solid && !authored_corridors.empty()) ||
      (corridor_ground && authored_corridors.empty()) ||
      !terrain_names_conform<simulation::kMaximumTerrainCorridorCount>(authored_corridors) ||
      !terrain_names_conform<simulation::kMaximumTerrainHoleCount>(authored_holes)) {
    return false;
  }
  std::size_t total_points = 0;
  std::size_t total_segments = 0;
  for (const json::value& corridor : authored_corridors) {
    const json::object& shape = corridor.get_object();
    const json::value* const points = member_of(shape, "points");
    double half_width = 0.0;
    if (!terrain_number(shape, "half_width", half_width, true) || points == nullptr ||
        !points->is_array() || points->get_array().size() < 2 ||
        points->get_array().size() > simulation::kMaximumTerrainPointCount) {
      return false;
    }
    const json::array& authored_points = points->get_array();
    total_points += authored_points.size();
    total_segments += authored_points.size() - 1;
    if (total_points > simulation::kMaximumTerrainPointCount ||
        total_segments > simulation::kMaximumTerrainSegmentCount) {
      return false;
    }
    AuthoredTerrainPoint previous{};
    for (std::size_t index = 0; index < authored_points.size(); ++index) {
      AuthoredTerrainPoint point{};
      if (!terrain_point(authored_points[index], width, height, point)) {
        return false;
      }
      if (index > 0) {
        const double dx = point.x - previous.x;
        const double dy = point.y - previous.y;
        const double squared_length = dx * dx + dy * dy;
        if (!std::isfinite(squared_length) || squared_length <= 0.0) {
          return false;
        }
      }
      previous = point;
    }
  }
  for (const json::value& hole : authored_holes) {
    const json::object& shape = hole.get_object();
    const json::value* const center = member_of(shape, "center");
    double radius = 0.0;
    AuthoredTerrainPoint point{};
    if (!terrain_number(shape, "radius", radius, true) || center == nullptr ||
        !terrain_point(*center, width, height, point)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] V3FrameConformance check_entities(const json::value& entities) {
  if (!entities.is_array()) {
    return V3FrameConformance::kEnvelopeMembersInvalid;
  }

  std::uint64_t previous_entity_id = 0;
  bool has_previous = false;
  for (const json::value& entry : entities.get_array()) {
    if (!entry.is_object()) {
      return V3FrameConformance::kEnvelopeMembersInvalid;
    }
    const json::object& entity = entry.get_object();

    const json::value* const encoded_entity_id = member_of(entity, "entity_id");
    if (encoded_entity_id == nullptr || !encoded_entity_id->is_int64()) {
      return V3FrameConformance::kEnvelopeMembersInvalid;
    }
    const auto entity_id = static_cast<std::uint64_t>(encoded_entity_id->get_int64());
    if (has_previous && entity_id <= previous_entity_id) {
      return V3FrameConformance::kEntitiesNotAscending;
    }
    previous_entity_id = entity_id;
    has_previous = true;

    const json::value* const encoded_components = member_of(entity, "components");
    if (encoded_components == nullptr || !encoded_components->is_object()) {
      return V3FrameConformance::kEnvelopeMembersInvalid;
    }
    const json::object& components = encoded_components->get_object();
    if (components.empty()) {
      return V3FrameConformance::kEntityWithoutComponents;
    }
    for (const auto& component : components) {
      if (!is_v3_component_kind(std::string_view{component.key().data(), component.key().size()})) {
        return V3FrameConformance::kComponentKindUnregistered;
      }
    }
  }
  return V3FrameConformance::kConforms;
}

[[nodiscard]] V3FrameConformance check_snapshot_data(const json::value& data) {
  if (!data.is_object()) {
    return V3FrameConformance::kEnvelopeMembersInvalid;
  }
  const json::object& snapshot = data.get_object();

  const json::value* const entities = member_of(snapshot, "entities");
  if (entities == nullptr) {
    return V3FrameConformance::kEnvelopeMembersInvalid;
  }
  if (const V3FrameConformance entity_conformance = check_entities(*entities);
      entity_conformance != V3FrameConformance::kConforms) {
    return entity_conformance;
  }

  const json::value* const match = member_of(snapshot, "match");
  if (match == nullptr || !match->is_object()) {
    return V3FrameConformance::kEnvelopeMembersInvalid;
  }
  const json::value* const mode_state = member_of(match->get_object(), "mode_state");
  if (mode_state == nullptr || !mode_state->is_object()) {
    return V3FrameConformance::kEnvelopeMembersInvalid;
  }
  const json::value* const mode_state_schema_id = member_of(mode_state->get_object(), "schema_id");
  if (mode_state_schema_id == nullptr || !mode_state_schema_id->is_string()) {
    return V3FrameConformance::kEnvelopeMembersInvalid;
  }
  if (!is_v3_mode_state_schema_id(view_of(mode_state_schema_id->get_string()))) {
    return V3FrameConformance::kModeStateSchemaIdUnregistered;
  }
  return V3FrameConformance::kConforms;
}

} // namespace

V3FrameConformance check_v3_server_frame(const std::string_view encoded_frame) {
  if (encoded_frame.size() > kSnapshotFrameV3MaximumByteCount) {
    return V3FrameConformance::kFrameTooLarge;
  }

  boost::system::error_code parse_error;
  const json::value document =
      json::parse(json::string_view{encoded_frame.data(), encoded_frame.size()}, parse_error);
  if (parse_error || !document.is_object()) {
    return V3FrameConformance::kNotOneJsonObject;
  }

  const json::object& envelope = document.get_object();
  const json::value* const data = member_of(envelope, "data");
  const json::value* const error = member_of(envelope, "error");
  const json::value* const metadata = member_of(envelope, "meta");
  if (envelope.size() != 3 || data == nullptr || error == nullptr || metadata == nullptr ||
      !metadata->is_object()) {
    return V3FrameConformance::kEnvelopeMembersInvalid;
  }

  // Exactly one of the two carries the frame. Both present is a decoder fork and both absent is a
  // frame that says nothing; the envelope promises neither is representable.
  if (data->is_null() == error->is_null()) {
    return V3FrameConformance::kDataAndErrorExclusivityViolated;
  }

  // Version first, before anything else is interpreted: it is the only ordering in which a client
  // can respond to a document it cannot validate (`docs/protocol/v3.md` § "Versioning and
  // fail-closed decoding").
  const json::object& message_metadata = metadata->get_object();
  const json::value* const protocol_version = member_of(message_metadata, "protocol_version");
  if (protocol_version == nullptr || !protocol_version->is_string()) {
    return V3FrameConformance::kEnvelopeMembersInvalid;
  }
  if (view_of(protocol_version->get_string()) != kProtocolV3Version) {
    return V3FrameConformance::kProtocolVersionUnsupported;
  }

  const json::value* const schema_id = member_of(message_metadata, "schema_id");
  if (schema_id == nullptr || !schema_id->is_string()) {
    return V3FrameConformance::kEnvelopeMembersInvalid;
  }
  const std::string_view message_schema_id = view_of(schema_id->get_string());
  if (!is_v3_message_schema_id(message_schema_id)) {
    return V3FrameConformance::kSchemaIdUnregistered;
  }

  if (message_schema_id == kErrorResponseV3SchemaId) {
    return V3FrameConformance::kConforms;
  }

  const json::value* const message_sequence = member_of(message_metadata, "message_sequence");
  if (message_sequence == nullptr || !message_sequence->is_int64()) {
    return V3FrameConformance::kEnvelopeMembersInvalid;
  }
  const std::int64_t sequence = message_sequence->get_int64();
  const bool is_welcome = message_schema_id == kWelcomeMessageSchemaId;
  const std::int64_t minimum_sequence =
      is_welcome ? static_cast<std::int64_t>(kWelcomeMessageSequence)
                 : static_cast<std::int64_t>(kFirstSnapshotMessageSequence);
  if (sequence < minimum_sequence ||
      (is_welcome && sequence != static_cast<std::int64_t>(kWelcomeMessageSequence))) {
    return V3FrameConformance::kMessageSequenceInvalid;
  }

  if (is_welcome) {
    return welcome_terrain_conforms(*data) && !has_negative_zero_token(encoded_frame)
               ? V3FrameConformance::kConforms
               : V3FrameConformance::kWelcomeTerrainInvalid;
  }
  return check_snapshot_data(*data);
}

} // namespace blob_royale::protocol
