#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_ROSTER_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_ROSTER_HPP

#include "component_join.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"

#include <cstddef>
#include <vector>

namespace blob_royale::gameplay {

// canonical: roster -- the two populations a mode's rules read: who is alive, and who is playing.
//
// **"Alive" is a defined term**: an entity that owns both a `PhysicsBody` and a `Controllable`
// (`docs/architecture/0005-royale-mode.md` § "Scope, vocabulary, and evaluation order"). The zone
// entity owns neither and a map's static bodies own no `Controllable`, so neither is ever counted.
// A *pending* entity -- one whose spawn was accepted but which the spawn policy has not seated --
// owns a `Controllable` and no `PhysicsBody`, so it is not alive either.
//
// **"Participant" is the second term, and it is wider**: every entity carrying a `Controllable`,
// whether it is alive, awaiting a seat, or out of play with its body erased and a respawn timer
// running. A mode whose fallen come back cannot count its field by bodies -- a hill match in which
// everyone but one is respawning has not collapsed to one -- so the field it decides by is the
// controllers still in it (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "Shared
// rules promoted, because they now have a second customer").
//
// These were `royale/royale_roster.hpp` while royale was the only mode that read them; the file
// moved here unchanged the day a second mode did, which is the rule of `README.md`. Both alive
// functions are the canonical two-store join (`component_join.hpp`) over the two engine kinds and
// add no ordering of their own: the join visits ascending `EntityId`, which is the order every
// rule resolves ties by. The participant functions walk the `Controllable` store, which is
// ascending by construction.
// related: component_join.hpp -- the ordered merge the alive functions are one application of.
// related: ../royale/royale_objective.hpp -- one of the declarations that reads the alive count.

[[nodiscard]] inline std::size_t alive_count(const simulation::GameWorld& world) {
  return simulation::count_entities_with_both(world.store<simulation::PhysicsBody>(),
                                              world.store<simulation::Controllable>());
}

// Every alive entity, ascending. Materialized because callers destroy entities afterwards, and
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

[[nodiscard]] inline std::size_t participant_count(const simulation::GameWorld& world) {
  return world.store<simulation::Controllable>().entries().size();
}

// Every participant, ascending. Materialized for the same reason `alive_entities` is.
[[nodiscard]] inline std::vector<simulation::EntityId>
participant_entities(const simulation::GameWorld& world) {
  std::vector<simulation::EntityId> participants;
  for (const simulation::ComponentStore<simulation::Controllable>::Entry& entry :
       world.store<simulation::Controllable>().entries()) {
    participants.push_back(entry.entity);
  }
  return participants;
}

} // namespace blob_royale::gameplay

#endif
