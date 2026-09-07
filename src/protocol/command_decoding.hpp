#ifndef BLOB_ROYALE_PROTOCOL_COMMAND_DECODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMMAND_DECODING_HPP

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "entity_id.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace blob_royale::protocol {

// Why one inbound application data message was refused, and therefore which close the session owes.
//
// Every value but `kAccepted` is terminal for the connection. The names are the stable close
// reasons of `docs/protocol/v2.md` § "Close codes", so the session maps a rejection to a close
// without a second table.
enum class CommandDecodeRejection : std::uint8_t {
  kAccepted = 0,
  // Over 1,024 bytes: close 1009 `client_message_too_large`. Admission-order step 1, before any
  // buffering completes and long before any parsing.
  kMessageTooLarge = 1,
  // Not one well-formed JSON command envelope: close 1008 `command_malformed`. This covers a parse
  // failure, a non-standard literal such as `NaN` or `Infinity`, trailing bytes after the document,
  // a non-object document, a missing or extra envelope member, and a non-object payload.
  kMalformed = 2,
  // `kind` is unregistered, or registered and not accepted by this mode: close 1008
  // `command_kind_rejected`.
  kKindRejected = 3,
  // `payload` failed the closed schema for its kind: close 1008 `command_payload_invalid`.
  kPayloadInvalid = 4,
};

[[nodiscard]] constexpr std::string_view
command_decode_rejection_name(const CommandDecodeRejection rejection) noexcept {
  switch (rejection) {
  case CommandDecodeRejection::kAccepted:
    return "accepted";
  case CommandDecodeRejection::kMessageTooLarge:
    return "client_message_too_large";
  case CommandDecodeRejection::kMalformed:
    return "command_malformed";
  case CommandDecodeRejection::kKindRejected:
    return "command_kind_rejected";
  case CommandDecodeRejection::kPayloadInvalid:
    return "command_payload_invalid";
  }
  return "command_decode_rejection_invalid";
}

// The WebSocket close code one rejection carries.
[[nodiscard]] constexpr std::uint16_t
command_decode_close_code(const CommandDecodeRejection rejection) noexcept {
  return rejection == CommandDecodeRejection::kMessageTooLarge ? std::uint16_t{1009}
                                                               : std::uint16_t{1008};
}

// canonical: command_decode_result -- the total answer to one inbound client data message.
//
// Either one stamped `simulation::Command` or one named rejection, never both and never neither.
// The decoder does not throw: it parses bytes an attacker chose, and an exception on that path
// would make the failure mode of a hostile frame different from the failure mode of a malformed
// one, which is exactly the difference an attacker probes for.
class CommandDecodeResult final {
public:
  [[nodiscard]] static CommandDecodeResult accepted(simulation::Command command) noexcept {
    return CommandDecodeResult{std::move(command)};
  }

  [[nodiscard]] static CommandDecodeResult
  rejected(const CommandDecodeRejection rejection) noexcept {
    return CommandDecodeResult{rejection};
  }

  [[nodiscard]] bool is_accepted() const noexcept {
    return rejection_ == CommandDecodeRejection::kAccepted;
  }
  [[nodiscard]] CommandDecodeRejection rejection() const noexcept { return rejection_; }
  [[nodiscard]] std::string_view rejection_name() const noexcept {
    return command_decode_rejection_name(rejection_);
  }
  [[nodiscard]] std::uint16_t close_code() const noexcept {
    return command_decode_close_code(rejection_);
  }

  // The stamped command, present exactly when the result is accepted.
  [[nodiscard]] const std::optional<simulation::Command>& command() const& noexcept {
    return command_;
  }
  [[nodiscard]] const std::optional<simulation::Command>& command() const&& = delete;

private:
  explicit CommandDecodeResult(simulation::Command command) noexcept
      : command_(std::move(command)) {}
  explicit CommandDecodeResult(const CommandDecodeRejection rejection) noexcept
      : rejection_(rejection) {}

  std::optional<simulation::Command> command_;
  CommandDecodeRejection rejection_{CommandDecodeRejection::kAccepted};
};

// canonical: command_envelope_decoding -- the only place a client-chosen byte becomes a Command.
//
// Runs admission-order steps 1 and 4 through 8 of `docs/protocol/v2.md` § "Admission order" in
// exactly that order. Steps 2 (transport validity) and 3 (the rate token) belong to the session:
// UTF-8 and framing are the WebSocket layer's, and the token **must** be charged before this
// function is called, because charging after parsing is what would let one token buy unbounded
// parser work.
//
// **The envelope names no entity.** `stamped_entity` is the session's own current body, supplied by
// the caller, and it is the only entity a decoded command can ever address. There is no wire field
// for a client to put someone else's id in, so "ignore the client's entity id" is not a check a
// refactor can drop -- it is a shape that does not exist
// (`docs/protocol/v2.md` § "Client command model").
//
// `accepted_kinds` is the running mode's mask. A kind that is registered on the wire but absent
// from the mask is `kKindRejected`, which is the second of the two independent enforcement points;
// `InputBatch::create` is the third and the `welcome` advertisement is none of them.
// related: command_wire_kind.hpp -- which simulation kinds a client may send, and under what name.
// related: src/simulation/input_batch.hpp -- the revalidation inside the runtime.
[[nodiscard]] CommandDecodeResult
decode_command_envelope(std::string_view frame, simulation::CommandKindMask accepted_kinds,
                        simulation::EntityId stamped_entity);

} // namespace blob_royale::protocol

#endif
