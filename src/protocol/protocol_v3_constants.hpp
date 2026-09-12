#ifndef BLOB_ROYALE_PROTOCOL_PROTOCOL_V3_CONSTANTS_HPP
#define BLOB_ROYALE_PROTOCOL_PROTOCOL_V3_CONSTANTS_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace blob_royale::protocol {

// canonical: protocol_v3_constants -- exact limits, identities, and closed vocabularies of the
// accepted protocol v3 schemas (`docs/protocol/schema/v3`).
//
// It is a separate file from `protocol_constants.hpp` for the reason `docs/protocol/v3.md`
// § "Normative language and canonical artifacts" gives for the separate error schema: v1 pins its
// own consts and a shared vocabulary would silently widen what a v1 client must accept. v1's file
// is untouched by this one and keeps meaning exactly what it meant.
//
// Every vocabulary below is **closed and mirrors one enum in the schema set**. A kind the schema
// set does not name is a decode failure on the client, so it must be an encode failure here; the
// mirrors are checked against the simulation's own registries by `static_assert` in
// `component_encoding_registry.hpp` and `command_wire_kind.hpp`, so a kind added to the simulation
// without being added to the wire fails to compile instead of shipping.
// related: docs/protocol/v3.md -- the accepted contract these values are read from.
// related: protocol_constants.hpp -- the v1 twin, deliberately not shared.

// This major opens with immutable welcome terrain. Planned behaviors extend 3.0 with their
// authoritative implementation and complete wire contract; development commits are not releases.
inline constexpr std::string_view kProtocolV3Version = "3.0";

inline constexpr std::string_view kWelcomeMessageSchemaId =
    "blob-royale://protocol/v3/welcome-message";
inline constexpr std::string_view kSnapshotMessageV3SchemaId =
    "blob-royale://protocol/v3/snapshot-message";
inline constexpr std::string_view kErrorResponseV3SchemaId =
    "blob-royale://protocol/v3/error-response";
// The body of `GET /api/v3/lobbies`: an HTTP document in the v3 envelope, never a WebSocket frame
// (`docs/protocol/v3.md` § "The lobby directory"). Added in 2.4.
inline constexpr std::string_view kLobbyDirectorySchemaId =
    "blob-royale://protocol/v3/lobby-directory";

// The four registered mode-state blocks. Closed, not a grammar
// (`common.schema.json#/$defs/mode_state_schema_id`). The hill's and race's were added in 2.5.
inline constexpr std::string_view kNoModeStateSchemaId =
    "blob-royale://protocol/v3/mode-state/none";
inline constexpr std::string_view kRoyaleModeStateSchemaId =
    "blob-royale://protocol/v3/mode-state/royale";
inline constexpr std::string_view kKingOfTheHillModeStateSchemaId =
    "blob-royale://protocol/v3/mode-state/king-of-the-hill";
inline constexpr std::string_view kRaceModeStateSchemaId =
    "blob-royale://protocol/v3/mode-state/race";

// The welcome is always message 1 and the first snapshot is 2
// (`docs/protocol/v3.md` § "Server message model").
inline constexpr std::uint64_t kWelcomeMessageSequence = 1;
inline constexpr std::uint64_t kFirstSnapshotMessageSequence = 2;

// One published entity of any kind: a player, a bot, a map static body, or the zone entity.
// 1,024 rather than v1's 4,096 because a v3 entity is much larger than a v1 player row and 4,096
// entities plus 4,096 placements do not fit the unchanged frame ceiling
// (`docs/protocol/v3.md` § "Limits").
inline constexpr std::size_t kSnapshotEntityLimit = 1'024;
inline constexpr std::size_t kMatchPlacementLimit = 1'024;
// The published race gate array is bounded by the map's marker ceiling. The combined
// authored marker count is validated by MapDefinition; a schema bounds each array independently.
inline constexpr std::size_t kRaceCoursePointLimit = 4'096;
// The closed count-object keys in snapshot-data.schema.json, in explicit registry order.
// random_draw_counts_encoding.hpp asserts agreement with the simulation registry.
inline constexpr std::array<std::string_view, 2> kV3RandomStreamNames{"hazards", "hill"};
inline constexpr std::size_t kSnapshotFrameV3MaximumByteCount = 2'097'152;
// v1's inbound bound, unchanged, now reached by real traffic
// (`docs/protocol/v3.md` § "Admission order" step 1).
inline constexpr std::size_t kClientMessageMaximumByteCount = 1'024;
inline constexpr std::size_t kDisplayNameMaximumCharacterCount = 64;
inline constexpr std::size_t kKindNameMaximumCharacterCount = 64;
inline constexpr std::size_t kMapNameMaximumCharacterCount = 64;
inline constexpr double kThrustComponentMaximumMagnitude = 1.0;

// The closed component-kind vocabulary of `common.schema.json#/$defs/component_kind`, in the
// schema's own ascending order, which is also the order `docs/protocol/v3.md`
// § "Object member order" requires component keys to be encoded in.
inline constexpr std::array<std::string_view, 15> kV3ComponentKindNames{"contact_effect_admission",
                                                                        "controllable",
                                                                        "hill",
                                                                        "hill_motion",
                                                                        "hill_presence",
                                                                        "lethal_on_contact",
                                                                        "lifetime",
                                                                        "physics_body",
                                                                        "race_progress",
                                                                        "respawn_timer",
                                                                        "score",
                                                                        "stun",
                                                                        "team",
                                                                        "zone",
                                                                        "zone_exposure"};

// The client-sendable command vocabulary of `common.schema.json#/$defs/command_kind`, in the
// schema's own ascending order. It names neither `spawn` nor `despawn`: both are server-issued on
// session admission and close, and advertising one would name a capability the boundary must refuse
// (`docs/protocol/v3.md` § "welcome").
//
// Four kinds operate pre-match lobby setup. Tuning is a separate, seated cooperative mutation of
// shared movement state; actual availability always comes from the mode's accepted mask.
inline constexpr std::array<std::string_view, 6> kV3ClientCommandKindNames{
    "clear_seat", "seat_npc", "set_movement_tuning", "set_seat_count", "set_thrust", "start_match"};

inline constexpr double kMovementAccelerationMinimum = 0.0;
inline constexpr double kMovementAccelerationMaximum = 10'000.0;
inline constexpr double kMovementNormalTopSpeedMinimum = 1.0;
inline constexpr double kMovementNormalTopSpeedMaximum = 10'000.0;
inline constexpr std::uint64_t kMovementTuningMinimumIntervalMilliseconds = 500;

// canonical: lobby_seat_wire_bounds -- the seat index and seat count a v3 frame may carry.
//
// Mirrors `simulation::kMaximumLobbySeatCount`, and the mirror is checked by `static_assert` in
// `command_wire_kind.hpp` rather than by including a simulation header here, for the reason the
// component-kind and command-kind vocabularies above are mirrors too: this file is the
// transcription of the accepted schemas, and a schema bound that silently followed a C++ constant
// would be a published contract nobody could read from the published contract.
//
// A seat index is zero-based, so its inclusive maximum is one below the count.
inline constexpr std::size_t kLobbySeatCountMaximum = 64;
inline constexpr std::size_t kLobbySeatIndexMaximum = kLobbySeatCountMaximum - 1;

// How many rooms one process may run and list: the `maxItems` of the lobby directory protocol 2.4
// publishes, and the bound `[lobbies] count` is validated against at startup, because a process
// may not run more rooms than it can list. Eight is the room count ADR 0006 budgets a two-core
// host for (`docs/architecture/0006-lobbies-as-rooms.md` § "The tick-loop decision"); the
// deployment names fewer.
inline constexpr std::size_t kLobbyDirectoryLimit = 8;

// The published NPC-kind list's bound. It is **not** the number of registered bot kinds, and that
// is the whole point: `welcome.npc_controller_kinds` is read from `ControllerRegistry` so that
// registering a bot costs no client change, and a bound that tracked the registry's size would put
// a schema edit -- and therefore a protocol version -- behind every new bot. Sixty-four is a
// generous ceiling on how many kinds one build can register and is a bound the frame budget can
// afford (`docs/protocol/v3.md` § "Limits").
inline constexpr std::size_t kNpcControllerKindLimit = 64;
inline constexpr std::size_t kNpcProfileLimit = 16;

// Whether a name is a registered v3 component kind. The vocabulary is closed, so this is the whole
// question and an unlisted name is a failure rather than a value to skip.
[[nodiscard]] constexpr bool is_v3_component_kind(const std::string_view kind_name) noexcept {
  return std::ranges::find(kV3ComponentKindNames, kind_name) != kV3ComponentKindNames.cend();
}

// Whether a name is a registered v3 client command kind. This is admission-order step 6's first
// half (`docs/protocol/v3.md` § "Admission order"); the mode's accepted set is the second half.
[[nodiscard]] constexpr bool is_v3_client_command_kind(const std::string_view kind_name) noexcept {
  return std::ranges::find(kV3ClientCommandKindNames, kind_name) !=
         kV3ClientCommandKindNames.cend();
}

// Whether a name is a registered v3 mode-state schema id.
[[nodiscard]] constexpr bool is_v3_mode_state_schema_id(const std::string_view schema_id) noexcept {
  return schema_id == kNoModeStateSchemaId || schema_id == kRoyaleModeStateSchemaId ||
         schema_id == kKingOfTheHillModeStateSchemaId || schema_id == kRaceModeStateSchemaId;
}

// The controller kind published for an entity whose controller the directory no longer names.
//
// A disconnect erases the directory entry immediately while the entity survives until the next
// tick despawns it, and a placement outlives its controller's session for the whole match, so a
// snapshot naming a closed controller is **ordinary** and not a defect
// (`src/runtime/controller_directory.hpp`). `docs/protocol/v3.md` § "Field dictionary" describes
// `controller_kind` as `session` or a registered bot kind and names no third value; failing the
// frame instead would turn one peer's disconnect into every peer's dropped frame, which is the
// remotely reachable availability failure the same document refuses elsewhere. The deviation is
// deliberate and is recorded in this step's report.
inline constexpr std::string_view kUnknownControllerKind = "unknown";
// The generated display-name fallback's prefix, completed with the entity id exactly as
// `docs/protocol/v3.md` § "Display names" rule 2 spells it.
inline constexpr std::string_view kFallbackDisplayNamePrefix = "player-";

} // namespace blob_royale::protocol

#endif
