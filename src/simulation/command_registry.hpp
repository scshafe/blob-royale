#ifndef BLOB_ROYALE_SIMULATION_COMMAND_REGISTRY_HPP
#define BLOB_ROYALE_SIMULATION_COMMAND_REGISTRY_HPP

#include "commands/despawn_command.hpp"
#include "commands/spawn_command.hpp"
#include "commands/thrust_command.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <variant>

namespace blob_royale::simulation {

// canonical: command_registry -- the closed, ordered list of command kinds.
// @extension-point command_kind
//
// Adding a command kind touches exactly one existing file in this domain, and this is it:
//
//   new  src/simulation/commands/<kind>_command.hpp  the value struct and its fields
//   edit src/simulation/command_registry.hpp         one enumerator in CommandKind, one type in
//                                                    the Command variant, one CommandKindName
//                                                    specialization, one CommandKindOf
//                                                    specialization, one kCommandKinds entry, and
//                                                    one arm of command_kind_application_rank
//   edit src/simulation/input_batch.cpp              the kind's value validation and the identity
//                                                    it addresses, in addressed_identity_of
//   new  src/gameplay/...                            the system that consumes it; command meaning
//                                                    is a system's job, never a kernel sub-step
//   new  docs/protocol/schema/v2/...                 its wire schema and decoder, which is a
//                                                    protocol minor version
//
// Everything else is generated: the kernel records commands without interpreting them, and a mode
// that does not list the kind in its accepted set never sees it.
// related: command_kind_mask.hpp -- the set of kinds a mode accepts.
// related: input_batch.hpp -- the one validated command value a tick may read.
using Command = std::variant<SpawnCommand, DespawnCommand, ThrustCommand>;

// Every alternative is nothrow-move-constructible, so a Command is never valueless by exception.
// That is what makes command_kind_of total and honestly noexcept rather than terminate-on-throw.
static_assert(std::is_nothrow_move_constructible_v<SpawnCommand> &&
                  std::is_nothrow_move_constructible_v<DespawnCommand> &&
                  std::is_nothrow_move_constructible_v<ThrustCommand>,
              "every Command alternative must be nothrow-move-constructible");

// One distinct bit per kind, so a set of kinds is one integer (see command_kind_mask.hpp).
enum class CommandKind : std::uint32_t {
  kSpawn = 1u << 0,
  kDespawn = 1u << 1,
  kThrust = 1u << 2,
};

// The closed list of kinds in declared order. CommandKindMask::all() and every diagnostic that
// enumerates kinds reads this, so no caller maintains a second list.
inline constexpr std::array<CommandKind, 3> kCommandKinds{
    CommandKind::kSpawn, CommandKind::kDespawn, CommandKind::kThrust};

inline constexpr std::size_t kCommandKindCount = kCommandKinds.size();

static_assert(std::variant_size_v<Command> == kCommandKindCount,
              "every Command alternative must declare exactly one CommandKind");

// canonical: command_kind_name -- the one wire name of one command kind.
//
// Declared like ComponentKindName and used the same way: the primary template is declared and
// never defined, so a kind that forgot its name fails to compile at the use site instead of
// publishing an empty name. Unlike a component, a command kind also needs an enumerator and a
// variant alternative, both of which live here, so the name lives beside them rather than in the
// value-struct header.
// related: component_kind_name.hpp -- the same pattern for component kinds.
template <typename CommandType> struct CommandKindName;

template <> struct CommandKindName<SpawnCommand> {
  static constexpr std::string_view value = "spawn";
};

template <> struct CommandKindName<DespawnCommand> {
  static constexpr std::string_view value = "despawn";
};

template <> struct CommandKindName<ThrustCommand> {
  static constexpr std::string_view value = "thrust";
};

// The declared wire name of one command kind, for encoders, diagnostics, and fixtures.
template <typename CommandType>
inline constexpr std::string_view command_kind_name = CommandKindName<CommandType>::value;

// canonical: command_kind_of_type -- the enumerator of one command value type.
//
// The primary template is declared and never defined, so a variant alternative that forgot its
// enumerator fails to compile rather than defaulting to a neighbouring kind.
template <typename CommandType> struct CommandKindOf;

template <> struct CommandKindOf<SpawnCommand> {
  static constexpr CommandKind value = CommandKind::kSpawn;
};

template <> struct CommandKindOf<DespawnCommand> {
  static constexpr CommandKind value = CommandKind::kDespawn;
};

template <> struct CommandKindOf<ThrustCommand> {
  static constexpr CommandKind value = CommandKind::kThrust;
};

// The kind of one command value. Total over the closed variant and generated from CommandKindOf,
// so a new alternative cannot silently answer with an existing kind.
[[nodiscard]] constexpr CommandKind command_kind_of(const Command& command) noexcept {
  return std::visit(
      []<typename CommandType>(const CommandType&) { return CommandKindOf<CommandType>::value; },
      command);
}

// The runtime projection of command_kind_name<CommandType>, for validation detail and diagnostics.
[[nodiscard]] constexpr std::string_view command_kind_name_of(const CommandKind kind) noexcept {
  switch (kind) {
  case CommandKind::kSpawn:
    return command_kind_name<SpawnCommand>;
  case CommandKind::kDespawn:
    return command_kind_name<DespawnCommand>;
  case CommandKind::kThrust:
    return command_kind_name<ThrustCommand>;
  }
  return "command_kind_invalid";
}

// The position of one kind in phase 0's application order: despawns, then spawns, then every
// remaining kind in ascending enumerator value
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick"). InputBatch
// canonicalizes to this rank so phase 0 is one forward pass that neither sorts nor regroups.
//
// A new kind that has no reason to run before the others takes the next rank after kThrust.
[[nodiscard]] constexpr std::uint32_t
command_kind_application_rank(const CommandKind kind) noexcept {
  switch (kind) {
  case CommandKind::kDespawn:
    return 0;
  case CommandKind::kSpawn:
    return 1;
  case CommandKind::kThrust:
    return 2;
  }
  return static_cast<std::uint32_t>(kCommandKindCount);
}

// The rank must be injective: InputBatch groups the commands it de-duplicates by (rank, addressed
// identity), which is the same grouping as (kind, addressed identity) only while no two kinds
// share a rank.
static_assert(command_kind_application_rank(CommandKind::kDespawn) !=
                  command_kind_application_rank(CommandKind::kSpawn) &&
              command_kind_application_rank(CommandKind::kDespawn) !=
                  command_kind_application_rank(CommandKind::kThrust) &&
              command_kind_application_rank(CommandKind::kSpawn) !=
                  command_kind_application_rank(CommandKind::kThrust));

} // namespace blob_royale::simulation

#endif
