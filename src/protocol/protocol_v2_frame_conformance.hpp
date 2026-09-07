#ifndef BLOB_ROYALE_PROTOCOL_PROTOCOL_V2_FRAME_CONFORMANCE_HPP
#define BLOB_ROYALE_PROTOCOL_PROTOCOL_V2_FRAME_CONFORMANCE_HPP

#include <cstdint>
#include <string_view>

namespace blob_royale::protocol {

// What one encoded protocol v2 server frame violates, or `kConforms`.
//
// Each value names one **normative invariant JSON Schema cannot express**
// (`docs/protocol/v2.md` § "Field dictionary and invariants"): the envelope's data/error
// exclusivity, ascending and distinct entity ids, an entity with no components, a component kind or
// mode-state schema id outside the closed vocabularies, and the frame's own encoded size.
enum class V2FrameConformance : std::uint8_t {
  kConforms = 0,
  // Not one well-formed JSON object, or bytes follow the document.
  kNotOneJsonObject = 1,
  // The envelope is not exactly `data`, `error`, `meta`, or `meta` is not the accepted shape.
  kEnvelopeMembersInvalid = 2,
  // Both `data` and `error` are non-null, or both are null. Exactly one is the contract, and a
  // frame carrying both is a decoder fork: two readers of one frame can reach opposite conclusions.
  kDataAndErrorExclusivityViolated = 3,
  // `meta.protocol_version` is not the const this schema set pins. A client reads this member
  // **before** validating anything else, so it is checked first here too.
  kProtocolVersionUnsupported = 4,
  // `meta.schema_id` is not one of the three v2 message identities.
  kSchemaIdUnregistered = 5,
  // The welcome is not message 1, or a snapshot is below 2.
  kMessageSequenceInvalid = 6,
  // `data.entities` is not ascending and distinct by `entity_id`.
  kEntitiesNotAscending = 7,
  // A published entity carries no component. An entity is its component set, so an entity with an
  // empty one is unrenderable and must not be published.
  kEntityWithoutComponents = 8,
  // A `components` key is outside `common.schema.json#/$defs/component_kind`.
  kComponentKindUnregistered = 9,
  // `match.mode_state.schema_id` is outside `common.schema.json#/$defs/mode_state_schema_id`.
  kModeStateSchemaIdUnregistered = 10,
  // Above the 2,097,152-byte frame ceiling.
  kFrameTooLarge = 11,
};

[[nodiscard]] constexpr std::string_view
v2_frame_conformance_name(const V2FrameConformance conformance) noexcept {
  switch (conformance) {
  case V2FrameConformance::kConforms:
    return "conforms";
  case V2FrameConformance::kNotOneJsonObject:
    return "not_one_json_object";
  case V2FrameConformance::kEnvelopeMembersInvalid:
    return "envelope_members_invalid";
  case V2FrameConformance::kDataAndErrorExclusivityViolated:
    return "data_and_error_exclusivity_violated";
  case V2FrameConformance::kProtocolVersionUnsupported:
    return "protocol_version_unsupported";
  case V2FrameConformance::kSchemaIdUnregistered:
    return "schema_id_unregistered";
  case V2FrameConformance::kMessageSequenceInvalid:
    return "message_sequence_invalid";
  case V2FrameConformance::kEntitiesNotAscending:
    return "entities_not_ascending";
  case V2FrameConformance::kEntityWithoutComponents:
    return "entity_without_components";
  case V2FrameConformance::kComponentKindUnregistered:
    return "component_kind_unregistered";
  case V2FrameConformance::kModeStateSchemaIdUnregistered:
    return "mode_state_schema_id_unregistered";
  case V2FrameConformance::kFrameTooLarge:
    return "frame_too_large";
  }
  return "v2_frame_conformance_invalid";
}

// canonical: v2_frame_conformance -- the executable form of protocol v2's unschematizable rules.
//
// `docs/protocol/v2.md` § "Field dictionary and invariants" lists what JSON Schema cannot prove
// about a v2 server frame and requires C++ conformance tests to cover it. This is that check, as
// one function rather than as an assertion written again in each test: the encoders' golden frames
// are checked against it, the negative documents the specification enumerates are checked against
// it, and a fuzz corpus or a Step 26 integration test drives the same entry point.
//
// It is a **fail-closed oracle over encoded bytes**, deliberately independent of the encoders: it
// re-parses the frame and re-derives every invariant from the document rather than from the values
// the encoder held, so an encoder defect cannot hide behind the check that is supposed to catch it.
// related: protocol_v2_json_encoding.hpp -- the encoders whose output this validates.
// related: docs/protocol/schema/v2 -- the schemas that own everything this does not check.
[[nodiscard]] V2FrameConformance check_v2_server_frame(std::string_view encoded_frame);

} // namespace blob_royale::protocol

#endif
