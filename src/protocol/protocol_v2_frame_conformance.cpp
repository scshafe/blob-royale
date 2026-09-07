#include "protocol_v2_frame_conformance.hpp"

#include "protocol_v2_constants.hpp"

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/string_view.hpp>
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>

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

// The three v2 message identities. A frame naming anything else is a frame this schema set does not
// define, which a client must close on rather than interpret.
[[nodiscard]] bool is_v2_message_schema_id(const std::string_view schema_id) noexcept {
  return schema_id == kWelcomeMessageSchemaId || schema_id == kSnapshotMessageV2SchemaId ||
         schema_id == kErrorResponseV2SchemaId;
}

[[nodiscard]] V2FrameConformance check_entities(const json::value& entities) {
  if (!entities.is_array()) {
    return V2FrameConformance::kEnvelopeMembersInvalid;
  }

  std::uint64_t previous_entity_id = 0;
  bool has_previous = false;
  for (const json::value& entry : entities.get_array()) {
    if (!entry.is_object()) {
      return V2FrameConformance::kEnvelopeMembersInvalid;
    }
    const json::object& entity = entry.get_object();

    const json::value* const encoded_entity_id = member_of(entity, "entity_id");
    if (encoded_entity_id == nullptr || !encoded_entity_id->is_int64()) {
      return V2FrameConformance::kEnvelopeMembersInvalid;
    }
    const auto entity_id = static_cast<std::uint64_t>(encoded_entity_id->get_int64());
    if (has_previous && entity_id <= previous_entity_id) {
      return V2FrameConformance::kEntitiesNotAscending;
    }
    previous_entity_id = entity_id;
    has_previous = true;

    const json::value* const encoded_components = member_of(entity, "components");
    if (encoded_components == nullptr || !encoded_components->is_object()) {
      return V2FrameConformance::kEnvelopeMembersInvalid;
    }
    const json::object& components = encoded_components->get_object();
    if (components.empty()) {
      return V2FrameConformance::kEntityWithoutComponents;
    }
    for (const auto& component : components) {
      if (!is_v2_component_kind(std::string_view{component.key().data(), component.key().size()})) {
        return V2FrameConformance::kComponentKindUnregistered;
      }
    }
  }
  return V2FrameConformance::kConforms;
}

[[nodiscard]] V2FrameConformance check_snapshot_data(const json::value& data) {
  if (!data.is_object()) {
    return V2FrameConformance::kEnvelopeMembersInvalid;
  }
  const json::object& snapshot = data.get_object();

  const json::value* const entities = member_of(snapshot, "entities");
  if (entities == nullptr) {
    return V2FrameConformance::kEnvelopeMembersInvalid;
  }
  if (const V2FrameConformance entity_conformance = check_entities(*entities);
      entity_conformance != V2FrameConformance::kConforms) {
    return entity_conformance;
  }

  const json::value* const match = member_of(snapshot, "match");
  if (match == nullptr || !match->is_object()) {
    return V2FrameConformance::kEnvelopeMembersInvalid;
  }
  const json::value* const mode_state = member_of(match->get_object(), "mode_state");
  if (mode_state == nullptr || !mode_state->is_object()) {
    return V2FrameConformance::kEnvelopeMembersInvalid;
  }
  const json::value* const mode_state_schema_id = member_of(mode_state->get_object(), "schema_id");
  if (mode_state_schema_id == nullptr || !mode_state_schema_id->is_string()) {
    return V2FrameConformance::kEnvelopeMembersInvalid;
  }
  if (!is_v2_mode_state_schema_id(view_of(mode_state_schema_id->get_string()))) {
    return V2FrameConformance::kModeStateSchemaIdUnregistered;
  }
  return V2FrameConformance::kConforms;
}

} // namespace

V2FrameConformance check_v2_server_frame(const std::string_view encoded_frame) {
  if (encoded_frame.size() > kSnapshotFrameV2MaximumByteCount) {
    return V2FrameConformance::kFrameTooLarge;
  }

  boost::system::error_code parse_error;
  const json::value document =
      json::parse(json::string_view{encoded_frame.data(), encoded_frame.size()}, parse_error);
  if (parse_error || !document.is_object()) {
    return V2FrameConformance::kNotOneJsonObject;
  }

  const json::object& envelope = document.get_object();
  const json::value* const data = member_of(envelope, "data");
  const json::value* const error = member_of(envelope, "error");
  const json::value* const metadata = member_of(envelope, "meta");
  if (envelope.size() != 3 || data == nullptr || error == nullptr || metadata == nullptr ||
      !metadata->is_object()) {
    return V2FrameConformance::kEnvelopeMembersInvalid;
  }

  // Exactly one of the two carries the frame. Both present is a decoder fork and both absent is a
  // frame that says nothing; the envelope promises neither is representable.
  if (data->is_null() == error->is_null()) {
    return V2FrameConformance::kDataAndErrorExclusivityViolated;
  }

  // Version first, before anything else is interpreted: it is the only ordering in which a client
  // can respond to a document it cannot validate (`docs/protocol/v2.md` § "Versioning and
  // fail-closed decoding").
  const json::object& message_metadata = metadata->get_object();
  const json::value* const protocol_version = member_of(message_metadata, "protocol_version");
  if (protocol_version == nullptr || !protocol_version->is_string()) {
    return V2FrameConformance::kEnvelopeMembersInvalid;
  }
  if (view_of(protocol_version->get_string()) != kProtocolV2Version) {
    return V2FrameConformance::kProtocolVersionUnsupported;
  }

  const json::value* const schema_id = member_of(message_metadata, "schema_id");
  if (schema_id == nullptr || !schema_id->is_string()) {
    return V2FrameConformance::kEnvelopeMembersInvalid;
  }
  const std::string_view message_schema_id = view_of(schema_id->get_string());
  if (!is_v2_message_schema_id(message_schema_id)) {
    return V2FrameConformance::kSchemaIdUnregistered;
  }

  if (message_schema_id == kErrorResponseV2SchemaId) {
    return V2FrameConformance::kConforms;
  }

  const json::value* const message_sequence = member_of(message_metadata, "message_sequence");
  if (message_sequence == nullptr || !message_sequence->is_int64()) {
    return V2FrameConformance::kEnvelopeMembersInvalid;
  }
  const std::int64_t sequence = message_sequence->get_int64();
  const bool is_welcome = message_schema_id == kWelcomeMessageSchemaId;
  const std::int64_t minimum_sequence =
      is_welcome ? static_cast<std::int64_t>(kWelcomeMessageSequence)
                 : static_cast<std::int64_t>(kFirstSnapshotMessageSequence);
  if (sequence < minimum_sequence ||
      (is_welcome && sequence != static_cast<std::int64_t>(kWelcomeMessageSequence))) {
    return V2FrameConformance::kMessageSequenceInvalid;
  }

  if (is_welcome) {
    return V2FrameConformance::kConforms;
  }
  return check_snapshot_data(*data);
}

} // namespace blob_royale::protocol
