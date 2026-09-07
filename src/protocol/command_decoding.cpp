#include "command_decoding.hpp"

#include "command_wire_kind.hpp"
#include "protocol_v2_constants.hpp"

#include "commands/thrust_command.hpp"
#include "vector2.hpp"

#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/parse_options.hpp>
#include <boost/json/string_view.hpp>
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>

#include <cmath>
#include <cstddef>
#include <optional>
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

// One `set_thrust` payload: `{x, y}`, closed, each component a finite number in [-1, 1].
//
// The per-component bound admits `(1, 1)`, whose magnitude is sqrt(2). **Clamping the magnitude is
// a mode rule** applied by `thrust_steering`, not a wire rule, so this decoder must not clamp:
// clamping here and again in the system would scale twice and would not be bit-identical to scaling
// once (`docs/protocol/v2.md` § "set_thrust"; `src/simulation/commands/thrust_command.hpp`).
[[nodiscard]] CommandDecodeResult decode_set_thrust(const json::object& payload,
                                                    const simulation::EntityId stamped_entity) {
  if (payload.size() != 2) {
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

  return CommandDecodeResult::accepted(
      simulation::ThrustCommand{stamped_entity, simulation::Vector2::create(*x, *y)});
}

// Admission-order step 7, dispatched on the kind step 6 accepted. Total over the closed command
// vocabulary: the two server-issued kinds are unreachable here because `client_command_wire_name`
// gives them no wire name at all, and answering `kKindRejected` rather than asserting keeps this
// function total without a second opinion about which kinds a client may send.
[[nodiscard]] CommandDecodeResult decode_payload(const simulation::CommandKind kind,
                                                 const json::object& payload,
                                                 const simulation::EntityId stamped_entity) {
  switch (kind) {
  case simulation::CommandKind::kThrust:
    return decode_set_thrust(payload, stamped_entity);
  case simulation::CommandKind::kSpawn:
  case simulation::CommandKind::kDespawn:
    break;
  }
  return CommandDecodeResult::rejected(CommandDecodeRejection::kKindRejected);
}

} // namespace

CommandDecodeResult decode_command_envelope(const std::string_view frame,
                                            const simulation::CommandKindMask accepted_kinds,
                                            const simulation::EntityId stamped_entity) {
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

  // Steps 7 and 8. The closed payload schema for that kind, then the server's own entity stamp.
  return decode_payload(*kind, encoded_payload->get_object(), stamped_entity);
}

} // namespace blob_royale::protocol
