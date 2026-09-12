#ifndef BLOB_ROYALE_SIMULATION_COMMAND_REGISTRY_HPP
#define BLOB_ROYALE_SIMULATION_COMMAND_REGISTRY_HPP

#include "commands/charge_command.hpp"
#include "commands/clear_seat_command.hpp"
#include "commands/despawn_command.hpp"
#include "commands/join_command.hpp"
#include "commands/leave_command.hpp"
#include "commands/seat_npc_command.hpp"
#include "commands/set_movement_tuning_command.hpp"
#include "commands/set_seat_count_command.hpp"
#include "commands/shield_command.hpp"
#include "commands/spawn_command.hpp"
#include "commands/start_match_command.hpp"
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
using Command = std::variant<SpawnCommand, DespawnCommand, ThrustCommand, SetSeatCountCommand,
                             ClearSeatCommand, SeatNpcCommand, StartMatchCommand, LeaveCommand,
                             JoinCommand, SetMovementTuningCommand, ShieldCommand, ChargeCommand>;

// A variant is nothrow-move-constructible exactly when every alternative is, so asking the variant
// asks about every alternative and cannot fall behind the list the way a hand-typed conjunction
// does. That is what makes command_kind_of total and honestly noexcept rather than
// terminate-on-throw: a Command is never valueless by exception.
static_assert(std::is_nothrow_move_constructible_v<Command>,
              "every Command alternative must be nothrow-move-constructible");

// One distinct bit per kind, so a set of kinds is one integer (see command_kind_mask.hpp).
// Existing bits never move. Application order is the separate closed rank function: tuning was
// appended to the bit vocabulary but applies immediately after thrust, ahead of the lobby kinds.
enum class CommandKind : std::uint32_t {
  kSpawn = 1u << 0,
  kDespawn = 1u << 1,
  kThrust = 1u << 2,
  kSetSeatCount = 1u << 3,
  kClearSeat = 1u << 4,
  kSeatNpc = 1u << 5,
  kStartMatch = 1u << 6,
  // Server-issued on session close and applied after every other kind
  // (`commands/leave_command.hpp`).
  kLeave = 1u << 7,
  // Server-issued when a controller wants a seat, applied after the lobby kinds and before a leave
  // (`commands/join_command.hpp`).
  kJoin = 1u << 8,
  kSetMovementTuning = 1u << 9,
  kShield = 1u << 10,
  // The second ability kind takes the next free bit for the reason the first one did: a mask is
  // persisted in a mode's accepted set and transmitted in `welcome`, so a bit that moved would
  // silently change what an already-written mask means (`commands/charge_command.hpp`).
  kCharge = 1u << 11,
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

template <> struct CommandKindOf<SetSeatCountCommand> {
  static constexpr CommandKind value = CommandKind::kSetSeatCount;
};

template <> struct CommandKindOf<ClearSeatCommand> {
  static constexpr CommandKind value = CommandKind::kClearSeat;
};

template <> struct CommandKindOf<SeatNpcCommand> {
  static constexpr CommandKind value = CommandKind::kSeatNpc;
};

template <> struct CommandKindOf<StartMatchCommand> {
  static constexpr CommandKind value = CommandKind::kStartMatch;
};

template <> struct CommandKindOf<LeaveCommand> {
  static constexpr CommandKind value = CommandKind::kLeave;
};

template <> struct CommandKindOf<JoinCommand> {
  static constexpr CommandKind value = CommandKind::kJoin;
};

template <> struct CommandKindOf<SetMovementTuningCommand> {
  static constexpr CommandKind value = CommandKind::kSetMovementTuning;
};

template <> struct CommandKindOf<ShieldCommand> {
  static constexpr CommandKind value = CommandKind::kShield;
};

template <> struct CommandKindOf<ChargeCommand> {
  static constexpr CommandKind value = CommandKind::kCharge;
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

// The four lobby kinds are named on the wire exactly as they are named here, unlike `thrust`, whose
// wire name says `set_thrust` because a client has to know the command replaces a persistent intent
// (`src/protocol/command_wire_kind.hpp`). These four each perform one whole act with no persistence
// to explain, so one name serves both vocabularies.
template <> struct CommandKindName<SetSeatCountCommand> {
  static constexpr std::string_view value = "set_seat_count";
};

template <> struct CommandKindName<ClearSeatCommand> {
  static constexpr std::string_view value = "clear_seat";
};

template <> struct CommandKindName<SeatNpcCommand> {
  static constexpr std::string_view value = "seat_npc";
};

template <> struct CommandKindName<StartMatchCommand> {
  static constexpr std::string_view value = "start_match";
};

// Server-issued and therefore never on the wire; the name is what a replay log and a diagnostic
// call it.
template <> struct CommandKindName<LeaveCommand> {
  static constexpr std::string_view value = "leave";
};

template <> struct CommandKindName<JoinCommand> {
  static constexpr std::string_view value = "join";
};

template <> struct CommandKindName<SetMovementTuningCommand> {
  static constexpr std::string_view value = "set_movement_tuning";
};

// Unlike `thrust`, whose wire name says `set_thrust` because the command replaces a persistent
// intent, a shield is one whole act with no persistence to explain, so one name serves both
// vocabularies the way the four lobby kinds' names do.
template <> struct CommandKindName<ShieldCommand> {
  static constexpr std::string_view value = "shield";
};

// `charge` for the same reason `shield` is not `set_charge`: the wire name says `set_thrust`
// because that command *replaces* a persistent intent a client has to reason about, and a charge
// replaces nothing -- it is one whole act whose entire effect is applied on the tick it is admitted
// (`commands/charge_command.hpp`). Carrying a direction does not change that; `set_thrust`'s prefix
// is about persistence, not about payload.
template <> struct CommandKindName<ChargeCommand> {
  static constexpr std::string_view value = "charge";
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
  case CommandKind::kSetSeatCount:
    return command_kind_name<SetSeatCountCommand>;
  case CommandKind::kClearSeat:
    return command_kind_name<ClearSeatCommand>;
  case CommandKind::kSeatNpc:
    return command_kind_name<SeatNpcCommand>;
  case CommandKind::kStartMatch:
    return command_kind_name<StartMatchCommand>;
  case CommandKind::kLeave:
    return command_kind_name<LeaveCommand>;
  case CommandKind::kJoin:
    return command_kind_name<JoinCommand>;
  case CommandKind::kSetMovementTuning:
    return command_kind_name<SetMovementTuningCommand>;
  case CommandKind::kShield:
    return command_kind_name<ShieldCommand>;
  case CommandKind::kCharge:
    return command_kind_name<ChargeCommand>;
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
// **A kind addresses whichever identity it actually carries.** A spawn addresses its ControllerId,
// because the engine and not the command chooses the EntityId (`commands/spawn_command.hpp`), so it
// has an ordering key and no entity: phase 0 creates an entity for it rather than recording against
// one. The lobby kinds and a leave address their ControllerId for a different reason: a lobby
// command acts
// on the *match*, not on a body, and the only identity it carries is the sender the boundary
// stamped it with. That choice is what makes "two clients seating the same seat resolve by the
// existing command order" true -- keying on the seat instead would collapse the two presses into
// one and silently discard the earlier sender's decision, which is a different rule and a worse
// one. It also bounds each sender to one command of each lobby kind per tick, which is the
// de-duplication a held mouse button needs. Every kind that names an entity addresses that EntityId
// and orders by its value.
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
// here and nowhere else, and forgetting to add one is a compile error rather than a wrong answer:
// the `else` arm reads `value.entity`, which a command carrying no entity does not have.
[[nodiscard]] inline AddressedIdentity addressed_identity_of(const Command& command) noexcept {
  return std::visit(
      []<typename CommandType>(const CommandType& value) {
        if constexpr (std::is_same_v<CommandType, SpawnCommand> ||
                      std::is_same_v<CommandType, SetSeatCountCommand> ||
                      std::is_same_v<CommandType, ClearSeatCommand> ||
                      std::is_same_v<CommandType, SeatNpcCommand> ||
                      std::is_same_v<CommandType, StartMatchCommand> ||
                      std::is_same_v<CommandType, LeaveCommand> ||
                      std::is_same_v<CommandType, JoinCommand> ||
                      std::is_same_v<CommandType, SetMovementTuningCommand>) {
          return AddressedIdentity::of_controller(value.controller);
        } else {
          return AddressedIdentity::of_entity(value.entity);
        }
      },
      command);
}

// The position of one kind in phase 0's application order: despawns, spawns, thrust, tuning,
// then the established relative order of lobby kinds, Join, and Leave
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
  case CommandKind::kSetMovementTuning:
    return 3;
  // The four lobby kinds run after every kind that touches an entity, and among themselves in the
  // order one seat's story is told: which seats exist, then who leaves one, then who takes one,
  // then whether to begin. That order is not decoration. `set_seat_count` first means a client may
  // grow the roster and seat the new seat in the same tick. `clear_seat` before `seat_npc` means a
  // client may replace a seat's occupant in one tick, which is the only way to replace one at all,
  // since seating never overwrites. `start_match` last means a press submitted alongside the
  // seating that completes the field is read after that seating rather than before it -- which
  // costs nothing today, because `can_start` is asked at `kLifecycle` long after this pass, and
  // which stays true the day something asks earlier.
  case CommandKind::kSetSeatCount:
    return 4;
  case CommandKind::kClearSeat:
    return 5;
  case CommandKind::kSeatNpc:
    return 6;
  case CommandKind::kStartMatch:
    return 7;
  // `join` runs after every lobby command, so the seat a join takes is the seat the same tick's
  // resize, clear, or declaration left: a bot joining the seat that declared it sees the
  // declaration, and a person joining sees the lobby as everyone else will publish it.
  case CommandKind::kJoin:
    return 8;
  // `leave` runs last of all. A spawn drained into the same batch must have created its entity
  // before the leave destroys it, or a departed session's spawn would survive it; a join alongside
  // is seated and then vacated rather than seated after it left; and a seating or a Start the
  // leaver sent alongside is still the decision it made while present.
  case CommandKind::kLeave:
    return 9;
  // A shield pulse is a pure recorded intent whose only ordering obligation is to follow a spawn,
  // so it takes the next free rank rather than a place among the kinds that act on the world. Its
  // meaning is read at `kPreKernel` by the ability system, long after this pass records it against
  // a `Controllable`; applying it after the lifecycle kinds means a body that despawned or left on
  // this tick simply has no `Controllable` to record into, which is the correct outcome for a
  // pulse pressed by a session that is already gone.
  case CommandKind::kShield:
    return 10;
  // A charge is recorded exactly as a shield pulse is, and for the same reasons, so it takes the
  // next free rank after it rather than a place among the kinds that act on the world. The two
  // ability kinds' relative order is not a priority: which one wins when both are pressed in one
  // tick is decided at `kPreKernel` by the ability system's explicit conflict rule, over the world
  // at entry, and never by which of them phase 0 recorded first
  // (`docs/reviews/2026-09-12-charge-contract.md` § "Admission, the conflict rule, and the
  // effect").
  case CommandKind::kCharge:
    return 11;
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
