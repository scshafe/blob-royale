#ifndef BLOB_ROYALE_PROTOCOL_COMMAND_WIRE_KIND_HPP
#define BLOB_ROYALE_PROTOCOL_COMMAND_WIRE_KIND_HPP

#include "protocol_v2_constants.hpp"

#include "command_registry.hpp"

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

// Server-issued: a despawn is the server's own consequence of the socket closing.
template <> struct CommandWireKind<simulation::DespawnCommand> {
  static constexpr std::optional<std::string_view> value = std::nullopt;
};

// The whole 2.0 client vocabulary. The wire name differs from the simulation kind name on purpose:
// `set_thrust` says the command *replaces* a steering intent that otherwise persists, which is the
// property a client must know to release a key correctly (`docs/protocol/v2.md` § "set_thrust").
template <> struct CommandWireKind<simulation::ThrustCommand> {
  static constexpr std::optional<std::string_view> value = kV2ClientCommandKindNames[0];
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

static_assert(client_command_kind_of_wire_name(kV2ClientCommandKindNames[0]).has_value(),
              "every name in the closed v2 command_kind vocabulary must select a registered kind");

} // namespace blob_royale::protocol

#endif
