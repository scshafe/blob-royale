#ifndef BLOB_ROYALE_PROTOCOL_PROTOCOL_V2_CONSTANTS_HPP
#define BLOB_ROYALE_PROTOCOL_PROTOCOL_V2_CONSTANTS_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace blob_royale::protocol {

// canonical: protocol_v2_constants -- exact limits, identities, and closed vocabularies of the
// accepted protocol v2 schemas (`docs/protocol/schema/v2`).
//
// It is a separate file from `protocol_constants.hpp` for the reason `docs/protocol/v2.md`
// § "Normative language and canonical artifacts" gives for the separate error schema: v1 pins its
// own consts and a shared vocabulary would silently widen what a v1 client must accept. v1's file
// is untouched by this one and keeps meaning exactly what it meant.
//
// Every vocabulary below is **closed and mirrors one enum in the schema set**. A kind the schema
// set does not name is a decode failure on the client, so it must be an encode failure here; the
// mirrors are checked against the simulation's own registries by `static_assert` in
// `component_encoding_registry.hpp` and `command_wire_kind.hpp`, so a kind added to the simulation
// without being added to the wire fails to compile instead of shipping.
// related: docs/protocol/v2.md -- the accepted contract these values are read from.
// related: protocol_constants.hpp -- the v1 twin, deliberately not shared.

inline constexpr std::string_view kProtocolV2Version = "2.1";

inline constexpr std::string_view kWelcomeMessageSchemaId =
    "blob-royale://protocol/v2/welcome-message";
inline constexpr std::string_view kSnapshotMessageV2SchemaId =
    "blob-royale://protocol/v2/snapshot-message";
inline constexpr std::string_view kErrorResponseV2SchemaId =
    "blob-royale://protocol/v2/error-response";

// The two registered mode-state blocks. Closed, not a grammar
// (`common.schema.json#/$defs/mode_state_schema_id`).
inline constexpr std::string_view kNoModeStateSchemaId =
    "blob-royale://protocol/v2/mode-state/none";
inline constexpr std::string_view kRoyaleModeStateSchemaId =
    "blob-royale://protocol/v2/mode-state/royale";

// The welcome is always message 1 and the first snapshot is 2
// (`docs/protocol/v2.md` § "Server message model").
inline constexpr std::uint64_t kWelcomeMessageSequence = 1;
inline constexpr std::uint64_t kFirstSnapshotMessageSequence = 2;

// One published entity of any kind: a player, a bot, a map static body, or the zone entity.
// 1,024 rather than v1's 4,096 because a v2 entity is much larger than a v1 player row and 4,096
// entities plus 4,096 placements do not fit the unchanged frame ceiling
// (`docs/protocol/v2.md` § "Limits").
inline constexpr std::size_t kSnapshotEntityLimit = 1'024;
inline constexpr std::size_t kMatchPlacementLimit = 1'024;
inline constexpr std::size_t kSnapshotFrameV2MaximumByteCount = 2'097'152;
// v1's inbound bound, unchanged, now reached by real traffic
// (`docs/protocol/v2.md` § "Admission order" step 1).
inline constexpr std::size_t kClientMessageMaximumByteCount = 1'024;
inline constexpr std::size_t kDisplayNameMaximumCharacterCount = 64;
inline constexpr std::size_t kKindNameMaximumCharacterCount = 64;
inline constexpr std::size_t kMapNameMaximumCharacterCount = 64;
inline constexpr double kThrustComponentMaximumMagnitude = 1.0;

// The closed component-kind vocabulary of `common.schema.json#/$defs/component_kind`, in the
// schema's own ascending order, which is also the order `docs/protocol/v2.md`
// § "Object member order" requires component keys to be encoded in.
inline constexpr std::array<std::string_view, 8> kV2ComponentKindNames{
    "controllable", "lethal_on_contact", "lifetime", "physics_body", "score", "team",
    "zone",         "zone_exposure"};

// The client-sendable command vocabulary of `common.schema.json#/$defs/command_kind`. It names
// neither `spawn` nor `despawn`: both are server-issued on session admission and close, and
// advertising one would name a capability the boundary must refuse
// (`docs/protocol/v2.md` § "welcome").
inline constexpr std::array<std::string_view, 1> kV2ClientCommandKindNames{"set_thrust"};

// Whether a name is a registered v2 component kind. The vocabulary is closed, so this is the whole
// question and an unlisted name is a failure rather than a value to skip.
[[nodiscard]] constexpr bool is_v2_component_kind(const std::string_view kind_name) noexcept {
  return std::ranges::find(kV2ComponentKindNames, kind_name) != kV2ComponentKindNames.cend();
}

// Whether a name is a registered v2 client command kind. This is admission-order step 6's first
// half (`docs/protocol/v2.md` § "Admission order"); the mode's accepted set is the second half.
[[nodiscard]] constexpr bool is_v2_client_command_kind(const std::string_view kind_name) noexcept {
  return std::ranges::find(kV2ClientCommandKindNames, kind_name) !=
         kV2ClientCommandKindNames.cend();
}

// Whether a name is a registered v2 mode-state schema id.
[[nodiscard]] constexpr bool is_v2_mode_state_schema_id(const std::string_view schema_id) noexcept {
  return schema_id == kNoModeStateSchemaId || schema_id == kRoyaleModeStateSchemaId;
}

// The controller kind published for an entity whose controller the directory no longer names.
//
// A disconnect erases the directory entry immediately while the entity survives until the next
// tick despawns it, and a placement outlives its controller's session for the whole match, so a
// snapshot naming a closed controller is **ordinary** and not a defect
// (`src/runtime/controller_directory.hpp`). `docs/protocol/v2.md` § "Field dictionary" describes
// `controller_kind` as `session` or a registered bot kind and names no third value; failing the
// frame instead would turn one peer's disconnect into every peer's dropped frame, which is the
// remotely reachable availability failure the same document refuses elsewhere. The deviation is
// deliberate and is recorded in this step's report.
inline constexpr std::string_view kUnknownControllerKind = "unknown";
// The generated display-name fallback's prefix, completed with the entity id exactly as
// `docs/protocol/v2.md` § "Display names" rule 2 spells it.
inline constexpr std::string_view kFallbackDisplayNamePrefix = "player-";

} // namespace blob_royale::protocol

#endif
