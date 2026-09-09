#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_RESPAWN_TIMER_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_RESPAWN_TIMER_COMPONENT_HPP

#include "component_kind_name.hpp"

#include <cstdint>
#include <string_view>

namespace blob_royale::simulation {

// canonical: respawn_timer_component -- how many more ticks an entity is out of play before it is
// offered a seat again.
//
// `shared/respawn_system` attaches one to an entity whose body it erased on an `EliminationEvent`
// and decrements it on every tick after; at zero the timer is erased, and the entity -- which still
// carries its `Controllable`, its score, and whatever else it owned -- is exactly what the engine's
// `SpawnSystem` calls "awaiting a body" (`spawn_system.hpp`). **An absent timer means "not
// respawning."** Nothing seeds one, a configured delay of zero attaches none, and a mode's spawn
// policy defers an entity that carries one, which is the one predicate that expresses "out of
// play" (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "Where the framework has to
// move").
//
// It is a component for the reasons `ZoneExposure` is: per entity, durable across ticks, erased
// with the entity by `destroy_entity`, and snapshot-visible, so a client can count a return down
// from the number the tick actually holds and two runs that diverge in a respawn diverge visibly.
// A tick count rather than a time value, because blob_simulation names no clock.
//
// It is the fourth kind beyond the engine's own five and, like `LethalOnContact`, belongs to no
// mode: royale never attaches one, because attrition destroys the eliminated rather than returning
// them, and every mode whose fallen come back declares the one system that does.
// related: ../../gameplay/shared/respawn_system.hpp -- the only writer.
// related: component_registry.hpp -- the closed list this kind is registered in.
// related: docs/protocol/schema/v2/respawn-timer-component.schema.json -- the wire shape.
struct RespawnTimer final {
  // Committed ticks left before the entity is offered a seat again. Never zero on a committed
  // world: the tick that would decrement it to zero erases it instead.
  std::uint64_t ticks_remaining{};

  friend bool operator==(const RespawnTimer&, const RespawnTimer&) = default;
};

template <> struct ComponentKindName<RespawnTimer> {
  static constexpr std::string_view value = "respawn_timer";
};

} // namespace blob_royale::simulation

#endif
