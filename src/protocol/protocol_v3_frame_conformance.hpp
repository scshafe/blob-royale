#ifndef BLOB_ROYALE_PROTOCOL_PROTOCOL_V3_FRAME_CONFORMANCE_HPP
#define BLOB_ROYALE_PROTOCOL_PROTOCOL_V3_FRAME_CONFORMANCE_HPP

#include <cstdint>
#include <string_view>

namespace blob_royale::protocol {

// What one encoded protocol v3 server frame violates, or `kConforms`.
//
// Each value names one **normative invariant JSON Schema cannot express**
// (`docs/protocol/v3.md` § "Field dictionary and invariants"): the envelope's data/error
// exclusivity, ascending and distinct entity ids, an entity with no components, a component kind or
// mode-state schema id outside the closed vocabularies, welcome terrain semantics, movement/result
// coverage by the containing snapshot, and the frame's own encoded size. Required member/type
// checks guard these semantic readers; JSON Schema
// remains the complete closed-shape validator.
enum class V3FrameConformance : std::uint8_t {
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
  // `meta.schema_id` is not one of the three v3 message identities.
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
  // Welcome terrain is missing or violates authored counts, names, scalar/segment geometry,
  // ground compatibility, closed-envelope containment, or canonical numeric publication.
  kWelcomeTerrainInvalid = 12,
  // Movement effective tick or a committed tuning decision's tick/revision is not covered by its
  // containing snapshot, or a required input to those semantic comparisons is absent/invalid.
  kMovementTuningCoverageInvalid = 13,
};

[[nodiscard]] constexpr std::string_view
v3_frame_conformance_name(const V3FrameConformance conformance) noexcept {
  switch (conformance) {
  case V3FrameConformance::kConforms:
    return "conforms";
  case V3FrameConformance::kNotOneJsonObject:
    return "not_one_json_object";
  case V3FrameConformance::kEnvelopeMembersInvalid:
    return "envelope_members_invalid";
  case V3FrameConformance::kDataAndErrorExclusivityViolated:
    return "data_and_error_exclusivity_violated";
  case V3FrameConformance::kProtocolVersionUnsupported:
    return "protocol_version_unsupported";
  case V3FrameConformance::kSchemaIdUnregistered:
    return "schema_id_unregistered";
  case V3FrameConformance::kMessageSequenceInvalid:
    return "message_sequence_invalid";
  case V3FrameConformance::kEntitiesNotAscending:
    return "entities_not_ascending";
  case V3FrameConformance::kEntityWithoutComponents:
    return "entity_without_components";
  case V3FrameConformance::kComponentKindUnregistered:
    return "component_kind_unregistered";
  case V3FrameConformance::kModeStateSchemaIdUnregistered:
    return "mode_state_schema_id_unregistered";
  case V3FrameConformance::kFrameTooLarge:
    return "frame_too_large";
  case V3FrameConformance::kWelcomeTerrainInvalid:
    return "welcome_terrain_invalid";
  case V3FrameConformance::kMovementTuningCoverageInvalid:
    return "movement_tuning_coverage_invalid";
  }
  return "v3_frame_conformance_invalid";
}

// canonical: v3_frame_conformance -- the executable form of protocol v3's unschematizable rules.
//
// `docs/protocol/v3.md` § "Field dictionary and invariants" lists what JSON Schema cannot prove
// about a v3 server frame and requires C++ conformance tests to cover it. This is that check, as
// one function rather than as an assertion written again in each test: the encoders' golden frames
// are checked against it, the negative documents the specification enumerates are checked against
// it, and a fuzz corpus or a Step 26 integration test drives the same entry point.
//
// It is a **fail-closed oracle over encoded bytes**, deliberately independent of the encoders: it
// re-parses the frame and re-derives every invariant from the document rather than from the values
// the encoder held, so an encoder defect cannot hide behind the check that is supposed to catch it.
// Terrain checks are bounded authored-data inspection, not terrain compilation. Agreement with
// external v1 configuration bounds is intentionally outside this single-frame oracle; the client
// checks that cross-document invariant after schema and terrain validation. Race road membership
// and checkpoint radius versus its selected corridor width also require retained terrain context:
// the encoder checks its snapshot terrain and the client checks its admitted welcome terrain.
// Movement coverage is single-frame: effective/decision ticks cannot exceed the containing tick,
// and a decision revision cannot exceed current movement revision. Pending request correlation is
// client/session state and is intentionally not inferred by this stateless oracle.
// related: protocol_v3_json_encoding.hpp -- the encoders whose output this validates.
// related: docs/protocol/schema/v3 -- the schemas that own everything this does not check.
[[nodiscard]] V3FrameConformance check_v3_server_frame(std::string_view encoded_frame);

} // namespace blob_royale::protocol

#endif
