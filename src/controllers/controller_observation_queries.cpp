#include "controller_observation_queries.hpp"

namespace blob_royale::controllers {

const simulation::PlayerSnapshot* find_observed_player(const simulation::WorldSnapshot& snapshot,
                                                       const simulation::EntityId entity) noexcept {
  for (const simulation::PlayerSnapshot& player : snapshot.players()) {
    if (player.entity_id() == entity) {
      return &player;
    }
  }
  return nullptr;
}

} // namespace blob_royale::controllers
