#ifndef BLOB_ROYALE_GAMEPLAY_ROYALE_ROYALE_ROSTER_HPP
#define BLOB_ROYALE_GAMEPLAY_ROYALE_ROYALE_ROSTER_HPP

#include "component_join.hpp"
#include "components/controllable_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"

#include <cstddef>
#include <vector>

namespace blob_royale::gameplay {

// canonical: royale_alive_roster -- the one population royale's rules read.
//
// **"Alive" is a defined term**: an entity that owns both a `PhysicsBody` and a `Controllable`
// (`docs/architecture/0005-royale-mode.md` § "Scope, vocabulary, and evaluation order"). The zone
// entity owns neither and a map's static bodies own no `Controllable`, so neither is ever counted.
// A *pending* entity -- one whose spawn was accepted but which the spawn policy has not seated --
// owns a `Controllable` and no `PhysicsBody`, so it is not alive either.
//
// Three rules ask this question: the objective's `can_start` and `outcome`, and
// `placement_recorder`'s placement arithmetic and restart wipe. It is named once here rather than
// spelled as a join at each of them, because the term is load-bearing in the ADR and a second
// spelling would be a second definition.
//
// Both are the canonical two-store join (`component_join.hpp`) over the two engine kinds and add no
// ordering of their own: the join visits ascending `EntityId`, which is the order every royale rule
// resolves ties by.
// related: component_join.hpp -- the ordered merge these are one application of.
// related: royale_objective.hpp -- the declaration that reads the count.

[[nodiscard]] inline std::size_t alive_count(const simulation::GameWorld& world) {
  return simulation::count_entities_with_both(world.store<simulation::PhysicsBody>(),
                                              world.store<simulation::Controllable>());
}

// Every alive entity, ascending. Materialized because both callers destroy entities afterwards, and
// destroying erases from the very stores a live join is walking.
[[nodiscard]] inline std::vector<simulation::EntityId>
alive_entities(const simulation::GameWorld& world) {
  std::vector<simulation::EntityId> alive;
  simulation::for_each_entity_with_both(
      world.store<simulation::PhysicsBody>(), world.store<simulation::Controllable>(),
      [&alive](const simulation::EntityId entity, const simulation::PhysicsBody&,
               const simulation::Controllable&) { alive.push_back(entity); });
  return alive;
}

} // namespace blob_royale::gameplay

#endif
