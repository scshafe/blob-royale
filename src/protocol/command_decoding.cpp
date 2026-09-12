#include "command_decoding.hpp"

#include "command_wire_kind.hpp"
#include "protocol_constants.hpp"
#include "protocol_v3_constants.hpp"

#include "commands/clear_seat_command.hpp"
#include "commands/seat_npc_command.hpp"
#include "commands/set_movement_tuning_command.hpp"
#include "commands/set_seat_count_command.hpp"
#include "commands/start_match_command.hpp"
#include "commands/thrust_command.hpp"
#include "seat_roster.hpp"
#include "snake_case_identity.hpp"
#include "vector2.hpp"

#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/parse_options.hpp>
#include <boost/json/string_view.hpp>
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace blob_royale::protocol {
namespace {

namespace json = boost::json;

// The parse the boundary performs on attacker-chosen bytes.
//
// `allow_*` are all left false, so a comment, a trailing comma, an invalid UTF-8 sequence, and the
// non-standard `NaN` and `Infinity` literals are parse failures rather than accepted values -- the
// last of which is the whole reason `set-thrust-command.schema.json` says a decoder MUST reject
// them. `numbers` is `precise` because a thrust component is simulation input: the imprecise mode
// is allowed to return a double one unit in the last place away from the decimal the client wrote,
// and a deterministic tick may not begin with an approximation of what was sent.
[[nodiscard]] json::parse_options client_parse_options() noexcept {
  json::parse_options options;
  options.numbers = json::number_precision::precise;
  return options;
}

[[nodiscard]] std::optional<double> finite_number_of(const json::value& value) noexcept {
  if (value.is_double()) {
    const double parsed = value.get_double();
    return std::isfinite(parsed) ? std::optional<double>{parsed} : std::nullopt;
  }
  if (value.is_int64()) {
    return static_cast<double>(value.get_int64());
  }
  if (value.is_uint64()) {
    return static_cast<double>(value.get_uint64());
  }
  return std::nullopt;
}

// One JSON number read as an exact unsigned integer inside an inclusive bound.
// Floating-point spellings, fractions, and negative values are rejected, never coerced.
[[nodiscard]] std::optional<std::uint64_t>
bounded_unsigned_of(const json::value& value, const std::uint64_t minimum,
                    const std::uint64_t maximum) noexcept {
  std::optional<std::uint64_t> parsed;
  if (value.is_uint64()) {
    parsed = value.get_uint64();
  } else if (value.is_int64() && value.get_int64() >= 0) {
    parsed = static_cast<std::uint64_t>(value.get_int64());
  }
  if (!parsed.has_value() || *parsed < minimum || *parsed > maximum) {
    return std::nullopt;
  }
  return parsed;
}

// One closed `set_thrust` payload: `{x, y, input_generation?}`. Direction components are finite
// numbers in [-1, 1]; a present generation is a positive exact protocol-safe integer.
//
// The per-component bound admits `(1, 1)`, whose magnitude is sqrt(2). **Clamping the magnitude is
// a mode rule** applied by `thrust_steering`, not a wire rule, so this decoder must not clamp:
// clamping here and again in the system would scale twice and would not be bit-identical to scaling
// once (`docs/protocol/v3.md` § "set_thrust"; `src/simulation/commands/thrust_command.hpp`).
[[nodiscard]] CommandDecodeResult decode_set_thrust(const json::object& payload,
                                                    const simulation::EntityId stamped_entity) {
  const json::value* const encoded_generation = payload.if_contains("input_generation");
  if (payload.size() != (encoded_generation == nullptr ? 2U : 3U)) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }
  const json::value* const encoded_x = payload.if_contains("x");
  const json::value* const encoded_y = payload.if_contains("y");
  if (encoded_x == nullptr || encoded_y == nullptr) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }

  const std::optional<double> x = finite_number_of(*encoded_x);
  const std::optional<double> y = finite_number_of(*encoded_y);
  if (!x.has_value() || !y.has_value()) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }
  if (std::abs(*x) > kThrustComponentMaximumMagnitude ||
      std::abs(*y) > kThrustComponentMaximumMagnitude) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }

  std::optional<simulation::TickSequence> generation;
  if (encoded_generation != nullptr) {
    const auto value = bounded_unsigned_of(*encoded_generation, 1, kMaximumSafeInteger);
    if (!value.has_value()) {
      return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
    }
    generation = simulation::TickSequence::create(*value);
  }
  return CommandDecodeResult::accepted(
      simulation::ThrustCommand{stamped_entity, simulation::Vector2::create(*x, *y), generation});
}

// One `{seat_index}` payload, shared by `clear_seat` and `seat_npc`'s first member. The bound is
// the
// **protocol's** constant, not the live roster's size: the roster is world state and no boundary
// holds it, so an index inside this bound that names no seat in the running lobby is a disagreement
// the tick ignores rather than a frame the boundary refuses
// (`src/simulation/game_simulation.cpp`, apply_lobby_command).
[[nodiscard]] std::optional<std::uint64_t> decode_seat_index(const json::value& encoded) noexcept {
  return bounded_unsigned_of(encoded, 0, kLobbySeatIndexMaximum);
}

[[nodiscard]] CommandDecodeResult decode_set_seat_count(const json::object& payload,
                                                        const simulation::ControllerId controller) {
  if (payload.size() != 1) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }
  const json::value* const encoded = payload.if_contains("seat_count");
  if (encoded == nullptr) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }
  const std::optional<std::uint64_t> seat_count =
      bounded_unsigned_of(*encoded, 1, kLobbySeatCountMaximum);
  if (!seat_count.has_value()) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }
  return CommandDecodeResult::accepted(simulation::SetSeatCountCommand{controller, *seat_count});
}

[[nodiscard]] CommandDecodeResult decode_clear_seat(const json::object& payload,
                                                    const simulation::ControllerId controller) {
  if (payload.size() != 1) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }
  const json::value* const encoded = payload.if_contains("seat_index");
  if (encoded == nullptr) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }
  const std::optional<std::uint64_t> seat_index = decode_seat_index(*encoded);
  if (!seat_index.has_value()) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }
  return CommandDecodeResult::accepted(simulation::ClearSeatCommand{controller, *seat_index});
}

// One `seat_npc` payload: `{seat_index, npc_kind}`, closed.
//
// **`npc_kind` is checked against the very list this session's `welcome` published**, which is read
// from `ControllerRegistry` and is therefore the exact set of bots the server can build
// (`session_welcome.hpp`). That is what makes registering a bot cost no client change: the same
// value teaches the client what to offer and teaches this decoder what to accept, so the two can
// never drift.
//
// A name outside that closed list is `kPayloadInvalid` and closes the session, exactly as an
// unregistered `command_kind` or an unregistered `mode_state.schema_id` does. It is a deliberate
// difference from every *state* disagreement in the lobby -- a stale seat index, an occupied seat,
// a shrink past an occupant -- each of which is a silent no-op inside the tick. The line between
// them is whether the client could have known: the seat roster changes under a client between
// frames and racing it is unavoidable, while the NPC vocabulary is constant for the process
// lifetime and was handed to this client in its first frame. Naming something outside it is a
// defect, and v3's stance on a defect is to fail closed and say so (`docs/protocol/v3.md` §
// "Versioning and fail-closed decoding").
[[nodiscard]] CommandDecodeResult
decode_seat_npc(const json::object& payload, const simulation::ControllerId controller,
                const std::span<const std::string> npc_controller_kinds) {
  if (payload.size() != 2) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }
  const json::value* const encoded_seat_index = payload.if_contains("seat_index");
  const json::value* const encoded_npc_kind = payload.if_contains("npc_kind");
  if (encoded_seat_index == nullptr || encoded_npc_kind == nullptr ||
      !encoded_npc_kind->is_string()) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }
  const std::optional<std::uint64_t> seat_index = decode_seat_index(*encoded_seat_index);
  if (!seat_index.has_value()) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }

  const json::string& encoded_kind_name = encoded_npc_kind->get_string();
  const std::string_view npc_kind{encoded_kind_name.data(), encoded_kind_name.size()};
  const bool registered =
      std::ranges::find(npc_controller_kinds, npc_kind) != npc_controller_kinds.end();
  // The grammar check is unreachable behind the membership check -- a published kind satisfies it,
  // and `MatchSessionContext::create` refuses a list where one does not -- and it is written anyway
  // because `SeatKindName::create` throws, and a decoder that parses attacker-chosen bytes must not
  // have a throwing path at all (`command_decoding.hpp`).
  if (!registered || !simulation::is_wire_kind_name(npc_kind)) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }
  return CommandDecodeResult::accepted(simulation::SeatNpcCommand{
      controller, *seat_index, simulation::SeatKindName::create(npc_kind)});
}

// One `start_match` payload: `{}`, and the emptiness is the whole check. A member here would be a
// client saying something about a match it does not get to say -- which match, whose start, from
// which tick -- so the closed shape is the enforcement rather than a preamble to it. It is the same
// argument `lethal-on-contact-component.schema.json` makes for an empty published object.
[[nodiscard]] CommandDecodeResult decode_start_match(const json::object& payload,
                                                     const simulation::ControllerId controller) {
  if (!payload.empty()) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }
  return CommandDecodeResult::accepted(simulation::StartMatchCommand{controller});
}

[[nodiscard]] CommandDecodeResult
decode_set_movement_tuning(const json::object& payload, const simulation::ControllerId controller) {
  if (payload.size() != 4) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }
  const json::value* const encoded_id = payload.if_contains("tuning_request_id");
  const json::value* const encoded_revision = payload.if_contains("expected_revision");
  const json::value* const encoded_acceleration =
      payload.if_contains("acceleration_world_units_per_second_squared");
  const json::value* const encoded_speed =
      payload.if_contains("normal_top_speed_world_units_per_second");
  if (encoded_id == nullptr || encoded_revision == nullptr || encoded_acceleration == nullptr ||
      encoded_speed == nullptr) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }
  const auto request_id = bounded_unsigned_of(*encoded_id, 1, kMaximumSafeInteger);
  const auto revision = bounded_unsigned_of(*encoded_revision, 0, kMaximumSafeInteger);
  const auto acceleration = finite_number_of(*encoded_acceleration);
  const auto speed = finite_number_of(*encoded_speed);
  if (!request_id.has_value() || !revision.has_value() || !acceleration.has_value() ||
      !speed.has_value() || *acceleration < kMovementAccelerationMinimum ||
      *acceleration > kMovementAccelerationMaximum || *speed < kMovementNormalTopSpeedMinimum ||
      *speed > kMovementNormalTopSpeedMaximum) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kPayloadInvalid);
  }
  return CommandDecodeResult::accepted(simulation::SetMovementTuningCommand{
      controller, *request_id, *revision,
      simulation::MovementTuning::create(*acceleration, *speed)});
}

// Admission-order step 7, dispatched on the kind step 6 accepted. Total over the closed command
// vocabulary: the two server-issued kinds are unreachable here because `client_command_wire_name`
// gives them no wire name at all, and answering `kKindRejected` rather than asserting keeps this
// function total without a second opinion about which kinds a client may send.
[[nodiscard]] CommandDecodeResult
decode_payload(const simulation::CommandKind kind, const json::object& payload,
               const simulation::EntityId stamped_entity,
               const simulation::ControllerId stamped_controller,
               const std::span<const std::string> npc_controller_kinds) {
  switch (kind) {
  case simulation::CommandKind::kThrust:
    return decode_set_thrust(payload, stamped_entity);
  case simulation::CommandKind::kSetSeatCount:
    return decode_set_seat_count(payload, stamped_controller);
  case simulation::CommandKind::kClearSeat:
    return decode_clear_seat(payload, stamped_controller);
  case simulation::CommandKind::kSeatNpc:
    return decode_seat_npc(payload, stamped_controller, npc_controller_kinds);
  case simulation::CommandKind::kStartMatch:
    return decode_start_match(payload, stamped_controller);
  case simulation::CommandKind::kSetMovementTuning:
    return decode_set_movement_tuning(payload, stamped_controller);
  case simulation::CommandKind::kSpawn:
  case simulation::CommandKind::kDespawn:
  case simulation::CommandKind::kLeave:
  case simulation::CommandKind::kJoin:
    break;
  }
  return CommandDecodeResult::rejected(CommandDecodeRejection::kKindRejected);
}

} // namespace

CommandDecodeResult decode_command_envelope(
    const std::string_view frame, const simulation::CommandKindMask accepted_kinds,
    const simulation::EntityId stamped_entity, const simulation::ControllerId stamped_controller,
    const std::span<const std::string> npc_controller_kinds) {
  // Step 1. Frame size, before anything else touches the bytes.
  if (frame.size() > kClientMessageMaximumByteCount) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kMessageTooLarge);
  }

  // Step 4. One well-formed JSON document and nothing after it.
  boost::system::error_code parse_error;
  const json::value document =
      json::parse(json::string_view{frame.data(), frame.size()}, parse_error, json::storage_ptr{},
                  client_parse_options());
  if (parse_error) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kMalformed);
  }

  // Step 5. Envelope shape: exactly `kind` and `payload`, and a payload that is an object.
  if (!document.is_object()) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kMalformed);
  }
  const json::object& envelope = document.get_object();
  if (envelope.size() != 2) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kMalformed);
  }
  const json::value* const encoded_kind = envelope.if_contains("kind");
  const json::value* const encoded_payload = envelope.if_contains("payload");
  if (encoded_kind == nullptr || encoded_payload == nullptr || !encoded_payload->is_object()) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kMalformed);
  }

  // Step 6. Kind acceptance, in both of its halves: registered on the wire, and accepted by the
  // running mode. The advertisement in `welcome` is neither of them.
  if (!encoded_kind->is_string()) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kKindRejected);
  }
  const json::string& kind_name = encoded_kind->get_string();
  const std::optional<simulation::CommandKind> kind =
      client_command_kind_of_wire_name(std::string_view{kind_name.data(), kind_name.size()});
  if (!kind.has_value() || !accepted_kinds.contains(*kind)) {
    return CommandDecodeResult::rejected(CommandDecodeRejection::kKindRejected);
  }

  // Steps 7 and 8. The closed payload schema for that kind, then the server's own identity stamps.
  return decode_payload(*kind, encoded_payload->get_object(), stamped_entity, stamped_controller,
                        npc_controller_kinds);
}

} // namespace blob_royale::protocol
