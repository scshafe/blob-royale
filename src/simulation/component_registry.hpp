#ifndef BLOB_ROYALE_SIMULATION_COMPONENT_REGISTRY_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENT_REGISTRY_HPP

#include "component_list.hpp"
#include "components/controllable_component.hpp"
#include "components/lifetime_component.hpp"
#include "components/score_component.hpp"
#include "components/team_component.hpp"
#include "physics_body.hpp"

namespace blob_royale::simulation {

// canonical: component_registry -- the closed, ordered list of component kinds.
// @extension-point entity_component
//
// Adding a kind is a new header under `src/simulation/components/` declaring the value struct and
// its ComponentKindName, plus one type in this list. World equality, GameWorld::destroy_entity,
// and WorldSnapshot construction are generated from this list, so a new kind cannot forget to
// participate in any of them. The list is closed at compile time and ordered by declaration, so no
// iteration order depends on a runtime registry.
//
// Two implementations of the seam beyond the engine set below: `Zone` for the royale safe zone and
// `Flag` for capture the flag.
using ComponentRegistry = ComponentList<PhysicsBody, Controllable, Lifetime, Score, Team>;

} // namespace blob_royale::simulation

#endif
