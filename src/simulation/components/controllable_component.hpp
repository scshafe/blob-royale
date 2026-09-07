#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_CONTROLLABLE_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_CONTROLLABLE_COMPONENT_HPP

#include "component_kind_name.hpp"
#include "controller_id.hpp"

#include <string_view>

namespace blob_royale::simulation {

// canonical: controllable_component -- the link from one entity to the controller deciding for it.
//
// The simulation stores the controller id and never branches on it, so a human session and a bot
// are indistinguishable to a tick. This component is the only place the EntityId and ControllerId
// identity spaces meet.
//
// The tick's recorded commands (`commands_this_tick` in
// `docs/architecture/0004-gameplay-architecture.md`) are deliberately absent until the Command
// variant exists; the field arrives with the command registry, not before it.
struct Controllable final {
  ControllerId controller_id;

  friend bool operator==(const Controllable&, const Controllable&) = default;
};

template <> struct ComponentKindName<Controllable> {
  static constexpr std::string_view value = "controllable";
};

} // namespace blob_royale::simulation

#endif
