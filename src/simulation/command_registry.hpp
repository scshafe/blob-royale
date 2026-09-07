#ifndef BLOB_ROYALE_SIMULATION_COMMAND_REGISTRY_HPP
#define BLOB_ROYALE_SIMULATION_COMMAND_REGISTRY_HPP

#include "commands/despawn_command.hpp"
#include "commands/spawn_command.hpp"
#include "commands/thrust_command.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "kind_registry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

namespace blob_royale::simulation {

// canonical: command_registry -- the closed, ordered list of command kinds.
// @extension-point command_kind
//
// Adding a command kind touches **two** existing files in this domain:
//
//   new  src/simulation/commands/<kind>_command.hpp  the value struct and its fields
//   edit src/simulation/command_registry.hpp         one type in the Command variant, one
//                                                    enumerator in CommandKind, one
//                                                    CommandKindName specialization, one
//                                                    CommandKindOf specialization, one arm of
//                                                    command_kind_application_rank, and one arm of
//                                                    addressed_identity_of
//   edit src/simulation/input_batch.cpp              the kind's value validation, if it has any
//   new  src/gameplay/...                            the system that consumes it; command meaning
//                                                    is a system's job, never a kernel sub-step
//   new  docs/protocol/schema/v2/...                 its wire schema and decoder, which is a
//                                                    protocol minor version
//
// **`kCommandKinds` is not on that list**: the kind array is derived from the variant through
// CommandKindOf, so it cannot omit a kind or carry a duplicate (engine review finding 7;
// kind_registry.hpp). **`recorded_entity_of` is not on it either**: "which identity does this
// command address?" had two implementations in two files and now has one, `addressed_identity_of`,
// below (engine review finding 5).
//
// Everything else is generated: the kernel records commands without interpreting them, and a mode
// that does not list the kind in its accepted set never sees it.
// related: command_kind_mask.hpp -- the set of kinds a mode accepts.
// related: input_batch.hpp -- the one validated command value a tick may read.
// related: kind_registry.hpp -- the derivation that keeps the kind list honest.
using Command = std::variant<SpawnCommand, DespawnCommand, ThrustCommand>;

// A variant is nothrow-move-constructible exactly when every alternative is, so asking the variant
// asks about every alternative and cannot fall behind the list the way a hand-typed conjunction
// does. That is what makes command_kind_of total and honestly noexcept rather than
// terminate-on-throw: a Command is never valueless by exception.
static_assert(std::is_nothrow_move_constructible_v<Command>,
              "every Command alternative must be nothrow-move-constructible");

// One distinct bit per kind, so a set of kinds is one integer (see command_kind_mask.hpp).
enum class CommandKind : std::uint32_t {
  kSpawn = 1u << 0,
  kDespawn = 1u << 1,
  kThrust = 1u << 2,
};

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

// The closed list of kinds in declared order, **derived from the variant** through CommandKindOf.
// CommandKindMask::all() and every diagnostic that enumerates kinds reads this, so no caller
// maintains a second list and no hand-typed entry can disagree with the variant.
inline constexpr std::array<CommandKind, std::variant_size_v<Command>> kCommandKinds =
    kinds_of_variant<Command, CommandKindOf>();

inline constexpr std::size_t kCommandKindCount = kCommandKinds.size();

// Each alternative's enumerator must be its own. Two alternatives sharing one enumerator would
// pass every size check while dropping a kind from CommandKindMask::all() and answering
// command_kind_of with a neighbour's kind.
static_assert(values_are_distinct(kCommandKinds),
              "every Command alternative must declare its own CommandKind enumerator");

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

// canonical: command_addressed_identity -- the one identity one command addresses.
//
// Two callers ask this question and they used to ask it of two different functions in two
// different files: `InputBatch::create` needs the value it orders and de-duplicates on, and kernel
// phase 0 needs the entity to record the command against. One capability, two implementations, two
// places to forget an arm -- which is engine review finding 5, and this value is its resolution.
//
// A **spawn** addresses its ControllerId, because the engine and not the command chooses the
// EntityId (`commands/spawn_command.hpp`), so it has an ordering key and no entity: phase 0 creates
// an entity for it rather than recording against one. **Every other kind** addresses the EntityId
// it names, and its ordering key is that id's value.
//
// The two identity spaces are never compared with each other. InputBatch groups by
// (application rank, ordering key) and the rank is injective over the kinds, so two kinds drawn
// from different identity spaces can never land in the same group.
// related: input_batch.hpp -- the canonical order this key defines.
// related: game_simulation.cpp -- kernel phase 0, the other caller.
class AddressedIdentity final {
public:
  // The identity of a command that names an entity, which is every kind but a spawn.
  [[nodiscard]] static AddressedIdentity of_entity(const EntityId entity) noexcept {
    return AddressedIdentity(entity, entity.value());
  }

  // The identity of a command that names the deciding agent instead of an entity.
  [[nodiscard]] static AddressedIdentity of_controller(const ControllerId controller) noexcept {
    return AddressedIdentity(std::nullopt, controller.value());
  }

  AddressedIdentity(const AddressedIdentity&) = default;
  AddressedIdentity(AddressedIdentity&&) noexcept = default;
  AddressedIdentity& operator=(const AddressedIdentity&) = default;
  AddressedIdentity& operator=(AddressedIdentity&&) noexcept = default;
  ~AddressedIdentity() = default;

  // The entity this command is recorded against, or nullopt when the kind addresses a different
  // identity space. Total: an absent entity is a defined answer and not a lookup failure.
  [[nodiscard]] std::optional<EntityId> entity() const noexcept { return entity_; }

  // The value InputBatch orders and de-duplicates on within one application rank.
  [[nodiscard]] std::uint64_t ordering_key() const noexcept { return ordering_key_; }

  friend bool operator==(const AddressedIdentity&, const AddressedIdentity&) = default;

private:
  AddressedIdentity(const std::optional<EntityId> entity, const std::uint64_t ordering_key) noexcept
      : entity_(entity), ordering_key_(ordering_key) {}

  std::optional<EntityId> entity_;
  std::uint64_t ordering_key_;
};

// The identity one command addresses. Total over the closed variant; adding a kind adds its arm
// here and nowhere else.
[[nodiscard]] inline AddressedIdentity addressed_identity_of(const Command& command) noexcept {
  return std::visit(
      []<typename CommandType>(const CommandType& value) {
        if constexpr (std::is_same_v<CommandType, SpawnCommand>) {
          return AddressedIdentity::of_controller(value.controller);
        } else {
          return AddressedIdentity::of_entity(value.entity);
        }
      },
      command);
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
// share a rank. Derived over the whole kind list rather than written out pairwise, so a fifth kind
// costs no new comparison to maintain and cannot be forgotten.
static_assert(values_are_distinct(projected_values(kCommandKinds, command_kind_application_rank)),
              "no two CommandKinds may share a phase 0 application rank");

} // namespace blob_royale::simulation

#endif
