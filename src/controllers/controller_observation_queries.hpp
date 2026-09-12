#ifndef BLOB_ROYALE_CONTROLLERS_CONTROLLER_OBSERVATION_QUERIES_HPP
#define BLOB_ROYALE_CONTROLLERS_CONTROLLER_OBSERVATION_QUERIES_HPP

#include "entity_id.hpp"
#include "player_snapshot.hpp"
#include "world_snapshot.hpp"

namespace blob_royale::controllers {

// canonical: controller_observation_queries -- ordered lookups in a retained public snapshot.
// A player is the published body/Controllable join, not every PhysicsBody. Returns nullptr for
// absence, including a bodyless Controllable. Returned pointers borrow the supplied snapshot.
[[nodiscard]] const simulation::PlayerSnapshot*
find_observed_player(const simulation::WorldSnapshot& snapshot,
                     simulation::EntityId entity) noexcept;
const simulation::PlayerSnapshot* find_observed_player(const simulation::WorldSnapshot&&,
                                                       simulation::EntityId) = delete;

// Looks up one component without changing its domain into the player join. The first matching
// entity in the canonical ordered store wins; no allocation, mutation, or gameplay admission.
template <typename Component>
[[nodiscard]] const Component* find_observed_component(const simulation::WorldSnapshot& snapshot,
                                                       const simulation::EntityId entity) noexcept {
  for (const auto& entry : snapshot.components<Component>()) {
    if (entry.entity == entity) {
      return &entry.value;
    }
  }
  return nullptr;
}

template <typename Component>
const Component* find_observed_component(const simulation::WorldSnapshot&&,
                                         simulation::EntityId) = delete;

} // namespace blob_royale::controllers

#endif
