#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_CONTROLLABLE_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_CONTROLLABLE_COMPONENT_HPP

#include "command_registry.hpp"
#include "component_kind_name.hpp"
#include "controller_id.hpp"

#include <string_view>
#include <vector>

namespace blob_royale::simulation {

// canonical: controllable_component -- the link from one entity to the controller deciding for it.
//
// The simulation stores the controller id and never branches on it, so a human session and a bot
// are indistinguishable to a tick. This component is the only place the EntityId and ControllerId
// identity spaces meet.
//
// Controllable has exactly two fields and will keep exactly two fields. Persistent *effect* has a
// home already -- a thrust writes PhysicsBody::acceleration, which persists until the next thrust
// -- and persistent *ability state* is per-entity durable state, which is what a component is
// (`docs/architecture/0004-gameplay-architecture.md` § "Entities, components, and stores").
struct Controllable final {
  ControllerId controller_id;
  // This tick's recorded commands for this entity: at most one of each kind, ascending
  // CommandKind. Phase 0 records them and does not interpret them, because command meaning is a
  // system's job. Nothing reads this field yet; the kernel that fills it arrives with the staged
  // tick. The default member initializer keeps `Controllable{controller_id}` -- the shape every
  // existing seating and fixture site uses -- a complete aggregate initialization.
  std::vector<Command> commands_this_tick{};

  friend bool operator==(const Controllable&, const Controllable&) = default;
};

template <> struct ComponentKindName<Controllable> {
  static constexpr std::string_view value = "controllable";
};

} // namespace blob_royale::simulation

#endif
