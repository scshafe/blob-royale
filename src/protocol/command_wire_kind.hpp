#ifndef BLOB_ROYALE_PROTOCOL_COMMAND_WIRE_KIND_HPP
#define BLOB_ROYALE_PROTOCOL_COMMAND_WIRE_KIND_HPP

#include "protocol_v2_constants.hpp"

#include "command_registry.hpp"
#include "simulation_limits.hpp"

#include <cstddef>
#include <optional>
#include <string_view>
#include <variant>

namespace blob_royale::protocol {

// canonical: command_wire_kind -- whether one simulation command kind is client-sendable, and under
// what name.
// @extension-point command_kind
//
// **The wire vocabulary is a narrowing of the simulation's, not a copy of it.** `spawn` and
// `despawn` exist in the `Command` variant and are issued by the server on session admission and
// close; naming either on the wire would advertise a capability the boundary must refuse
// (`docs/protocol/v2.md` § "welcome"). So each kind declares an *optional* wire name, and
// `std::nullopt` means server-issued.
//
// The primary template is declared and never defined, so a command kind added to the variant
// without deciding whether a client may send it fails to compile at the decoder's use site rather
// than defaulting into either answer.
// related: src/simulation/command_registry.hpp -- the closed list of kinds this narrows.
// related: command_decoding.hpp -- the only consumer of the inbound direction.
template <typename CommandType> struct CommandWireKind;

// Server-issued: the engine chooses the EntityId inside a tick, and a session's spawn is the
// server's own consequence of the upgrade succeeding.
template <> struct CommandWireKind<simulation::SpawnCommand> {
  static constexpr std::optional<std::string_view> value = std::nullopt;
};

// Server-issued: a despawn names one entity, which is what a replay fixture or a test can do and a
// client must not. No production path submits one since `leave` exists.
template <> struct CommandWireKind<simulation::DespawnCommand> {
  static constexpr std::optional<std::string_view> value = std::nullopt;
};

// Server-issued: a leave is the server's own consequence of a session closing, enqueued by
// `CommandSink::close_session`, and a client that could send one could vacate somebody else's seat.
template <> struct CommandWireKind<simulation::LeaveCommand> {
  static constexpr std::optional<std::string_view> value = std::nullopt;
};

// Server-issued: a session asks for a seat for itself and the bot reconciliation asks for a bot's,
// and a client that could name a seat could name somebody else's.
template <> struct CommandWireKind<simulation::JoinCommand> {
  static constexpr std::optional<std::string_view> value = std::nullopt;
};

// The whole 2.0 client vocabulary. The wire name differs from the simulation kind name on purpose:
// `set_thrust` says the command *replaces* a steering intent that otherwise persists, which is the
// property a client must know to release a key correctly (`docs/protocol/v2.md` § "set_thrust").
template <> struct CommandWireKind<simulation::ThrustCommand> {
  static constexpr std::optional<std::string_view> value = kV2ClientCommandKindNames[3];
};

// The four lobby kinds, added in 2.3. **Each one is a decision that a client may operate the
// lobby**
// -- the primary template above is declared and never defined precisely so that decision has to be
// made rather than inherited, and here it is made four times in the affirmative.
//
// Each names itself, unlike `set_thrust`: none of them replaces a persistent intent, so there is no
// property a differing wire name would have to teach. The array indices are the schema's own
// ascending order (`protocol_v2_constants.hpp`), which is why they do not read in the order they
// are applied.
template <> struct CommandWireKind<simulation::SetSeatCountCommand> {
  static constexpr std::optional<std::string_view> value = kV2ClientCommandKindNames[2];
};

template <> struct CommandWireKind<simulation::ClearSeatCommand> {
  static constexpr std::optional<std::string_view> value = kV2ClientCommandKindNames[0];
};

template <> struct CommandWireKind<simulation::SeatNpcCommand> {
  static constexpr std::optional<std::string_view> value = kV2ClientCommandKindNames[1];
};

template <> struct CommandWireKind<simulation::StartMatchCommand> {
  static constexpr std::optional<std::string_view> value = kV2ClientCommandKindNames[4];
};

namespace detail {

template <std::size_t Index>
[[nodiscard]] constexpr std::optional<std::string_view>
wire_name_of_kind(const simulation::CommandKind kind) noexcept {
  using CommandType = std::variant_alternative_t<Index, simulation::Command>;
  if (simulation::CommandKindOf<CommandType>::value == kind) {
    return CommandWireKind<CommandType>::value;
  }
  if constexpr (Index + 1 < std::variant_size_v<simulation::Command>) {
    return wire_name_of_kind<Index + 1>(kind);
  } else {
    return std::nullopt;
  }
}

template <std::size_t Index>
[[nodiscard]] constexpr std::optional<simulation::CommandKind>
kind_of_wire_name(const std::string_view wire_name) noexcept {
  using CommandType = std::variant_alternative_t<Index, simulation::Command>;
  if (CommandWireKind<CommandType>::value == wire_name) {
    return simulation::CommandKindOf<CommandType>::value;
  }
  if constexpr (Index + 1 < std::variant_size_v<simulation::Command>) {
    return kind_of_wire_name<Index + 1>(wire_name);
  } else {
    return std::nullopt;
  }
}

[[nodiscard]] constexpr std::size_t client_sendable_kind_count() noexcept {
  std::size_t sendable = 0;
  for (const simulation::CommandKind kind : simulation::kCommandKinds) {
    if (wire_name_of_kind<0>(kind).has_value()) {
      ++sendable;
    }
  }
  return sendable;
}

} // namespace detail

// The wire name a client sends for one simulation command kind, or nullopt when the kind is
// server-issued. Total over the closed kind list and generated from the declarations above.
[[nodiscard]] constexpr std::optional<std::string_view>
client_command_wire_name(const simulation::CommandKind kind) noexcept {
  return detail::wire_name_of_kind<0>(kind);
}

// The simulation kind one wire name selects, or nullopt when no registered client kind bears it.
// This is admission-order step 6's first half (`docs/protocol/v2.md` § "Admission order").
[[nodiscard]] constexpr std::optional<simulation::CommandKind>
client_command_kind_of_wire_name(const std::string_view wire_name) noexcept {
  return detail::kind_of_wire_name<0>(wire_name);
}

// The declarations above and the closed schema vocabulary must name the same set. Counting rather
// than listing means a kind that gains a wire name without gaining a schema enum member -- or the
// reverse -- fails to compile.
static_assert(detail::client_sendable_kind_count() == kV2ClientCommandKindNames.size(),
              "the client-sendable command kinds and the closed v2 command_kind vocabulary must "
              "name the same set");

namespace detail {

// Every published name selects a registered kind. Counting alone is not enough: five declarations
// and five schema names agree on size while two declarations quietly share one name and a third
// name selects nothing at all. Written as a fold over the whole vocabulary rather than one assert
// per index, so a sixth kind costs no new line here.
[[nodiscard]] constexpr bool every_published_name_selects_a_kind() noexcept {
  for (const std::string_view wire_name : kV2ClientCommandKindNames) {
    if (!client_command_kind_of_wire_name(wire_name).has_value()) {
      return false;
    }
  }
  return true;
}

} // namespace detail

static_assert(detail::every_published_name_selects_a_kind(),
              "every name in the closed v2 command_kind vocabulary must select a registered kind");

// The seat bounds this file's schema transcription pins and the bound the engine actually enforces
// are one number, and this is where the mirror is checked. `protocol_v2_constants.hpp` deliberately
// includes no simulation header -- it is a transcription of the published schemas -- so the check
// lives in the first file that legitimately sees both.
static_assert(kLobbySeatCountMaximum == simulation::kMaximumLobbySeatCount,
              "the published seat-count bound and the engine's lobby bound must be one number");
static_assert(kLobbySeatIndexMaximum + 1 == simulation::kMaximumLobbySeatCount,
              "a seat index is zero-based, so its published maximum is one below the seat count");

} // namespace blob_royale::protocol

#endif
