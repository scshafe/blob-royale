#include "game_world.hpp"

#include "components/controllable_component.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace blob_royale::simulation {

GameWorld::EntitySeed GameWorld::EntitySeed::create(const EntityId entity, PhysicsBody body) {
  return EntitySeed{entity, body, ControllerId::create(entity.value())};
}

GameWorld::EntitySeed GameWorld::EntitySeed::create(const EntityId entity, PhysicsBody body,
                                                    const ControllerId controller) {
  return EntitySeed{entity, body, controller};
}

GameWorld GameWorld::create(std::vector<EntitySeed> seeds) {
  if (seeds.size() > kMaximumPlayerCount) {
    throw SimulationValidationError(
        SimulationValidationCode::kGameWorldPlayerLimitExceeded, "game_world.entities",
        "entity count " + std::to_string(seeds.size()) + " exceeds the accepted limit");
  }

  std::sort(seeds.begin(), seeds.end(), [](const EntitySeed& left, const EntitySeed& right) {
    return left.entity < right.entity;
  });

  const auto duplicate = std::adjacent_find(
      seeds.cbegin(), seeds.cend(),
      [](const EntitySeed& left, const EntitySeed& right) { return left.entity == right.entity; });
  if (duplicate != seeds.cend()) {
    throw SimulationValidationError(
        SimulationValidationCode::kGameWorldDuplicateEntityId, "game_world.entities.entity_id",
        "duplicate EntityId " + std::to_string(duplicate->entity.value()));
  }

  std::vector<EntityId> entities;
  entities.reserve(seeds.size());
  std::vector<ComponentStore<PhysicsBody>::Entry> bodies;
  bodies.reserve(seeds.size());
  std::vector<ComponentStore<Controllable>::Entry> controllables;
  controllables.reserve(seeds.size());
  for (const EntitySeed& seed : seeds) {
    entities.push_back(seed.entity);
    bodies.push_back(ComponentStore<PhysicsBody>::Entry{seed.entity, seed.body});
    controllables.push_back(
        ComponentStore<Controllable>::Entry{seed.entity, Controllable{seed.controller}});
  }

  ComponentStores<ComponentRegistry> stores;
  std::get<ComponentStore<PhysicsBody>>(stores) =
      ComponentStore<PhysicsBody>::create(std::move(bodies));
  std::get<ComponentStore<Controllable>>(stores) =
      ComponentStore<Controllable>::create(std::move(controllables));
  return GameWorld(std::move(entities), std::move(stores));
}

bool GameWorld::contains(const EntityId entity) const noexcept {
  return std::binary_search(entities_.cbegin(), entities_.cend(), entity);
}

void GameWorld::create_entity(const EntityId entity) {
  const auto position = std::lower_bound(entities_.cbegin(), entities_.cend(), entity);
  if (position != entities_.cend() && *position == entity) {
    throw SimulationValidationError(SimulationValidationCode::kGameWorldDuplicateEntityId,
                                    "game_world.entities.entity_id",
                                    "duplicate EntityId " + std::to_string(entity.value()));
  }
  if (entities_.size() >= kMaximumPlayerCount) {
    throw SimulationValidationError(SimulationValidationCode::kGameWorldPlayerLimitExceeded,
                                    "game_world.entities",
                                    "entity count exceeds the accepted limit");
  }
  entities_.insert(position, entity);
}

void GameWorld::emit(WorldEvent event) {
  if (events_.size() >= kMaximumWorldEventCount) {
    throw SimulationValidationError(
        SimulationValidationCode::kGameWorldEventLimitExceeded, "game_world.events",
        "emitting a " + std::string(world_event_kind_name_of(world_event_kind_of(event))) +
            " event would exceed the accepted per-tick limit " +
            std::to_string(kMaximumWorldEventCount));
  }
  events_.push_back(std::move(event));
}

void GameWorld::destroy_entity(const EntityId entity) noexcept {
  const auto position = std::lower_bound(entities_.cbegin(), entities_.cend(), entity);
  if (position != entities_.cend() && *position == entity) {
    entities_.erase(position);
  }

  // Generated over the registry: a kind added to ComponentRegistry participates here without any
  // edit to this function.
  ComponentRegistry::for_each_kind(
      [this, entity]<typename Component>() { mutable_store<Component>().erase(entity); });
}

GameWorld::GameWorld(std::vector<EntityId> entities,
                     ComponentStores<ComponentRegistry> stores) noexcept
    : entities_(std::move(entities)), stores_(std::move(stores)) {}

} // namespace blob_royale::simulation
