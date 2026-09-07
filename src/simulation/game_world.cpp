#include "game_world.hpp"

#include "components/controllable_component.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "spawn_system.hpp"

#include <algorithm>
#include <cstddef>
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

  std::vector<ComponentStore<PhysicsBody>::Entry> bodies;
  bodies.reserve(seeds.size());
  std::vector<ComponentStore<Controllable>::Entry> controllables;
  controllables.reserve(seeds.size());
  for (const EntitySeed& seed : seeds) {
    bodies.push_back(ComponentStore<PhysicsBody>::Entry{seed.entity, seed.body});
    if (seed.controller.has_value()) {
      controllables.push_back(
          ComponentStore<Controllable>::Entry{seed.entity, Controllable{*seed.controller}});
    }
  }

  ComponentStores<ComponentRegistry> stores;
  std::get<ComponentStore<PhysicsBody>>(stores) =
      ComponentStore<PhysicsBody>::create(std::move(bodies));
  std::get<ComponentStore<Controllable>>(stores) =
      ComponentStore<Controllable>::create(std::move(controllables));
  return GameWorld(std::move(stores), DeterministicRandom::create(0));
}

GameWorld GameWorld::create(const SimulationConfig& configuration, const MapDefinition& map,
                            const std::uint64_t seed) {
  // A spawn point that cannot seat a disc of the configured radius would fail the tick that first
  // seated an entity there, so it is rejected here instead: a map and a configuration that cannot
  // be played together fail at construction with a named cause.
  require_spawn_points_are_seatable(configuration, map);

  const std::span<const PhysicsBody> static_bodies = map.static_bodies();
  if (static_bodies.size() > kMaximumPlayerCount) {
    throw SimulationValidationError(
        SimulationValidationCode::kGameWorldPlayerLimitExceeded, "game_world.entities",
        "map static body count " + std::to_string(static_bodies.size()) +
            " exceeds the accepted entity limit");
  }

  // The id policy: `kMinimumEntityId + index` in the map's declared order. Declared order is the
  // map file's own order, so the ids a map seats are a function of the file alone.
  std::vector<ComponentStore<PhysicsBody>::Entry> bodies;
  bodies.reserve(static_bodies.size());
  for (std::size_t index = 0; index < static_bodies.size(); ++index) {
    bodies.push_back(ComponentStore<PhysicsBody>::Entry{
        EntityId::create(kMinimumEntityId + static_cast<EntityId::Value>(index)),
        static_bodies[index]});
  }

  ComponentStores<ComponentRegistry> stores;
  std::get<ComponentStore<PhysicsBody>>(stores) =
      ComponentStore<PhysicsBody>::create(std::move(bodies));
  return GameWorld(std::move(stores), DeterministicRandom::create(seed));
}

EntityId GameWorld::create_entity() {
  // Throws SimulationValidationError when exhausted, which is the hard failure the reservation
  // documents: no id is reused, wrapped, or invented.
  const EntityId drawn = reservation_.draw_next();
  if (contains(drawn)) {
    throw SimulationValidationError(
        SimulationValidationCode::kGameWorldDuplicateEntityId, "game_world.entities.entity_id",
        "the tick's reservation issued EntityId " + std::to_string(drawn.value()) +
            ", which already names a live entity");
  }
  return drawn;
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
  // Generated over the registry: a kind added to ComponentRegistry participates here without any
  // edit to this function. Erasing from every store is also what removes the entity from the
  // derived roster, so destruction has exactly one effect to get right.
  ComponentRegistry::for_each_kind(
      [this, entity]<typename Component>() { mutable_store<Component>().erase(entity); });
}

GameWorld::GameWorld(ComponentStores<ComponentRegistry> stores, DeterministicRandom random) noexcept
    : stores_(std::move(stores)), random_(random) {}

} // namespace blob_royale::simulation
