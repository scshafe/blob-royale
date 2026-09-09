#ifndef BLOB_ROYALE_PROTOCOL_PROTOCOL_V2_JSON_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_PROTOCOL_V2_JSON_ENCODING_HPP

#include "controller_directory_view.hpp"
#include "lobby_listing.hpp"
#include "protocol_constants.hpp"
#include "protocol_v2_constants.hpp"
#include "request_id.hpp"
#include "session_welcome.hpp"
#include "v2_http_error.hpp"

#include "controller_id.hpp"
#include "entity_id.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace blob_royale::simulation {
class WorldSnapshot;
}

namespace blob_royale::protocol {

// canonical: protocol_v2_json_encoding -- sole production entry points for v2 JSON encoding.
//
// v1's encoders are untouched and still served; this file is additive, exactly as protocol v2 is
// additive to protocol v1 (`docs/protocol/v2.md` § "Decision"). The two versions share the bounded
// serializer, the number canonicalization, and the timestamp grammar, and share nothing else: every
// version-bearing constant, vocabulary, and envelope is its own, so widening one cannot widen the
// other.
// related: docs/protocol/v2.md -- the accepted contract every function here implements.
// related: component_encoding_registry.hpp -- the per-kind encoders the snapshot walk drives.
// related: command_decoding.hpp -- the inbound direction.

// Encodes the one `welcome` frame of a session. `message_sequence` is the const 1 the schema pins,
// so it is not a parameter: a welcome that was not message 1 would not be a welcome.
// Throws ProtocolEncodingError on an invalid timestamp or byte limit, or an oversized frame.
[[nodiscard]] std::string
encode_welcome_message(const SessionWelcome& welcome, const RequestId& request_id,
                       std::string_view sent_at_utc,
                       std::size_t output_byte_limit = kSnapshotFrameV2MaximumByteCount);

// @extension-point snapshot_encoding -- encodes one immutable snapshot as protocol v2 JSON.
//
// Every published entity in ascending `entity_id` order with the components it carries keyed by
// component kind, plus the generic `match` section. The component set of an entity is generated
// from `simulation::ComponentRegistry`, so a kind added to the world is published here without this
// function changing; the per-kind wire shape is that kind's own `ComponentWireEncoding`.
//
// `directory` supplies the two presentation values the wire `controllable` object carries and the
// controller each placement names; see `controller_directory_view.hpp` for why it is a port rather
// than the runtime's own directory.
//
// It fails closed and never truncates. Throws ProtocolEncodingError for a message sequence below
// the first snapshot's `2`, an invalid timestamp, the uncommitted tick zero, a phase start tick the
// wire cannot carry, more than 1,024 entities or placements, entity ids that are not ascending and
// distinct, a placement whose controller the directory cannot name, or a complete frame above the
// byte limit.
[[nodiscard]] std::string
encode_snapshot_message_v2(const simulation::WorldSnapshot& snapshot,
                           const ControllerDirectoryView& directory, const RequestId& request_id,
                           std::uint64_t message_sequence, std::string_view sent_at_utc,
                           std::size_t output_byte_limit = kSnapshotFrameV2MaximumByteCount);

// Encodes one complete schema-valid v2 HTTP error envelope. The V2HttpError owns status/code
// parity; this function owns only the v2 envelope around it.
// Throws ProtocolEncodingError if the caller's byte limit is invalid or exceeded.
[[nodiscard]] std::string
encode_error_response_v2(const V2HttpError& error, const RequestId& request_id,
                         std::size_t output_byte_limit = kHttpJsonResponseMaximumByteCount);

// Encodes the body of `GET /api/v2/lobbies`: every room in lobby-id order, in the v2 envelope
// (`lobby-directory-message.schema.json`, 2.4). Fails closed: throws ProtocolEncodingError with
// `LOBBY_DIRECTORY_INVALID` for no rooms, more than `kLobbyDirectoryLimit`, ids that are not
// exactly `1..N` in order, a mode or map name outside its grammar, a count outside its bound, a
// filled or NPC seat count above the seat count, or a phase start past the tick; and for a
// document above the byte limit.
[[nodiscard]] std::string
encode_lobby_directory_message(std::span<const LobbyListing> lobbies, const RequestId& request_id,
                               std::size_t output_byte_limit = kHttpJsonResponseMaximumByteCount);

// canonical: own_body_resolution -- which entity one controller currently drives.
//
// **The client's rule, implemented once on the server side too.** A client resolves its own body
// each frame by finding the entity whose `controllable.controller_id` equals its own, and must
// treat "no such entity this frame" as the ordinary state of a player who is eliminated, waiting,
// or deferred by the spawn policy (`docs/protocol/v2.md` § "Entities, controllers, and what
// survives what"). The session needs the same answer for two of its own jobs -- filling
// `welcome.entity_id` with its first body, and stamping every decoded command with its current one
// -- so the resolution lives here rather than being written a third time at the boundary.
//
// It is the canonical two-store join over the published spans: an entity that carries a
// `Controllable` naming this controller but no `PhysicsBody` is not a body and is skipped
// (`src/simulation/component_join.hpp`).
// canonical: find_controlled_entity -- the entity one controller owns, body or not.
//
// **This is a different question from `find_controlled_body` and the difference is load-bearing.**
// A body is what a client draws and what a thrust addresses, so that resolution requires a
// `PhysicsBody`. *Ownership* requires only the `Controllable` link, and the two diverge for a whole
// class of ordinary session: one deferred by a full spawn ring, one waiting between a lobby wipe
// and its reseat, one sitting in a lobby it has not been seated into yet. Such a session owns an
// entity and has no body.
//
// It exists because the close path needs the second question. A session that leaves despawns what
// it owns, and asking for a body there left every never-seated session's entity in the world
// forever -- a Controllable with no body, unrenderable, uncounted as a player, and never destroyed.
// That was reachable before the lobby existed, by joining a running royale; the lobby makes a
// bodiless session the *normal* state of somebody who is about to play, which is why it is fixed
// here. related: find_controlled_body -- the narrower question, for the welcome and the command
// stamp.
[[nodiscard]] std::optional<simulation::EntityId>
find_controlled_entity(const simulation::WorldSnapshot& snapshot,
                       simulation::ControllerId controller);

[[nodiscard]] std::optional<simulation::EntityId>
find_controlled_body(const simulation::WorldSnapshot& snapshot,
                     simulation::ControllerId controller);

// The ascending-and-distinct rule on `snapshot.entities`, as its own named check.
//
// Public because it is a normative invariant **JSON Schema cannot express** and the specification
// requires C++ conformance tests to cover it (`docs/protocol/v2.md` § "Field dictionary and
// invariants"). `WorldSnapshot` derives its roster from its own stores and so cannot represent a
// violation, which is exactly why the guard is exercised through this entry point rather than
// through a snapshot no code can build.
// Throws ProtocolEncodingError with SNAPSHOT_ENTITY_ORDER_INVALID.
void validate_ascending_unique_entities(std::span<const simulation::EntityId> entities,
                                        std::string_view context);

} // namespace blob_royale::protocol

#endif
