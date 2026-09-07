#ifndef BLOB_ROYALE_SIMULATION_WORLD_EVENT_REGISTRY_HPP
#define BLOB_ROYALE_SIMULATION_WORLD_EVENT_REGISTRY_HPP

#include "events/contact_event.hpp"
#include "events/despawn_event.hpp"
#include "events/elimination_event.hpp"
#include "events/score_event.hpp"
#include "events/spawn_event.hpp"
#include "kind_registry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <variant>

namespace blob_royale::simulation {

// canonical: world_event_registry -- the closed list of in-tick event kinds.
//
// Systems within one tick communicate through the bounded, ordered event list `GameWorld` owns.
// The list is append-only during a tick and is cleared at commit, so **events are tick-local and
// never appear in a snapshot**; a consequence that must outlive the tick is written into a
// component instead (`docs/architecture/0004-gameplay-architecture.md` § "World events").
//
// Ordering is total: events are appended in production order, and every producing phase produces
// in ascending EntityId or canonical-pair order, so the list is a deterministic function of the
// tick. Overflow past kMaximumWorldEventCount is a hard simulation failure, never a silent drop,
// because a dropped event converts a failure into a differently wrong tick.
//
// Adding an event kind touches exactly one existing file in this domain, and this is it:
//
//   new  src/simulation/events/<kind>_event.hpp  the value struct and its fields
//   edit src/simulation/world_event_registry.hpp one type in the WorldEvent variant, one
//                                                enumerator in WorldEventKind, one
//                                                WorldEventKindName specialization, and one
//                                                WorldEventKindOf specialization
//   new  src/gameplay/...                        the system that consumes it; an event has no
//                                                meaning until a stage reads it
//
// `kWorldEventKinds` is not on that list: the kind array is derived from the variant through
// WorldEventKindOf, so it cannot omit a kind or carry a duplicate (engine review finding 7;
// kind_registry.hpp).
//
// related: game_world.hpp -- the owner of the tick's event list.
// related: command_registry.hpp -- the same closed-variant shape for one tick's input.
using WorldEvent =
    std::variant<ContactEvent, SpawnEvent, DespawnEvent, EliminationEvent, ScoreEvent>;

// A variant is nothrow-move-constructible exactly when every alternative is, so asking the variant
// asks about every alternative and cannot fall behind the list the way a hand-typed conjunction
// does. That is what makes world_event_kind_of total and honestly noexcept rather than
// terminate-on-throw: a WorldEvent is never valueless by exception.
static_assert(std::is_nothrow_move_constructible_v<WorldEvent>,
              "every WorldEvent alternative must be nothrow-move-constructible");

// Declaration order, which is the order the variant lists. Unlike CommandKind these are not bit
// flags: no operation takes a *set* of event kinds, so a mask would be structure without a reader.
enum class WorldEventKind : std::uint32_t {
  kContact = 0,
  kSpawn = 1,
  kDespawn = 2,
  kElimination = 3,
  kScore = 4,
};

// canonical: world_event_kind_name -- the one diagnostic name of one event kind.
//
// Declared like CommandKindName and used the same way: the primary template is declared and never
// defined, so a kind that forgot its name fails to compile at the use site instead of publishing
// an empty name.
// related: command_kind_name in command_registry.hpp -- the same pattern for command kinds.
template <typename EventType> struct WorldEventKindName;

template <> struct WorldEventKindName<ContactEvent> {
  static constexpr std::string_view value = "contact";
};

template <> struct WorldEventKindName<SpawnEvent> {
  static constexpr std::string_view value = "spawn";
};

template <> struct WorldEventKindName<DespawnEvent> {
  static constexpr std::string_view value = "despawn";
};

template <> struct WorldEventKindName<EliminationEvent> {
  static constexpr std::string_view value = "elimination";
};

template <> struct WorldEventKindName<ScoreEvent> {
  static constexpr std::string_view value = "score";
};

// The declared name of one event kind, for diagnostics and fixtures.
template <typename EventType>
inline constexpr std::string_view world_event_kind_name = WorldEventKindName<EventType>::value;

// canonical: world_event_kind_of_type -- the enumerator of one event value type.
//
// The primary template is declared and never defined, so a variant alternative that forgot its
// enumerator fails to compile rather than defaulting to a neighbouring kind.
template <typename EventType> struct WorldEventKindOf;

template <> struct WorldEventKindOf<ContactEvent> {
  static constexpr WorldEventKind value = WorldEventKind::kContact;
};

template <> struct WorldEventKindOf<SpawnEvent> {
  static constexpr WorldEventKind value = WorldEventKind::kSpawn;
};

template <> struct WorldEventKindOf<DespawnEvent> {
  static constexpr WorldEventKind value = WorldEventKind::kDespawn;
};

template <> struct WorldEventKindOf<EliminationEvent> {
  static constexpr WorldEventKind value = WorldEventKind::kElimination;
};

template <> struct WorldEventKindOf<ScoreEvent> {
  static constexpr WorldEventKind value = WorldEventKind::kScore;
};

// The closed list of kinds in declared order, **derived from the variant** through
// WorldEventKindOf, so no diagnostic maintains a second list and no hand-typed entry can disagree
// with the variant.
inline constexpr std::array<WorldEventKind, std::variant_size_v<WorldEvent>> kWorldEventKinds =
    kinds_of_variant<WorldEvent, WorldEventKindOf>();

inline constexpr std::size_t kWorldEventKindCount = kWorldEventKinds.size();

// Each alternative's enumerator must be its own. Two alternatives sharing one enumerator would
// pass every size check while answering world_event_kind_of with a neighbour's kind.
static_assert(values_are_distinct(kWorldEventKinds),
              "every WorldEvent alternative must declare its own WorldEventKind enumerator");

// The kind of one event value. Total over the closed variant and generated from WorldEventKindOf,
// so a new alternative cannot silently answer with an existing kind.
[[nodiscard]] constexpr WorldEventKind world_event_kind_of(const WorldEvent& event) noexcept {
  return std::visit(
      []<typename EventType>(const EventType&) { return WorldEventKindOf<EventType>::value; },
      event);
}

// The runtime projection of world_event_kind_name<EventType>, for validation detail and
// diagnostics.
[[nodiscard]] constexpr std::string_view
world_event_kind_name_of(const WorldEventKind kind) noexcept {
  switch (kind) {
  case WorldEventKind::kContact:
    return world_event_kind_name<ContactEvent>;
  case WorldEventKind::kSpawn:
    return world_event_kind_name<SpawnEvent>;
  case WorldEventKind::kDespawn:
    return world_event_kind_name<DespawnEvent>;
  case WorldEventKind::kElimination:
    return world_event_kind_name<EliminationEvent>;
  case WorldEventKind::kScore:
    return world_event_kind_name<ScoreEvent>;
  }
  return "world_event_kind_invalid";
}

} // namespace blob_royale::simulation

#endif
