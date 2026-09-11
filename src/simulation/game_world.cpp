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
  if (seeds.size() > kMaximumEntityCount) {
    throw SimulationValidationError(
        SimulationValidationCode::kGameWorldEntityLimitExceeded, "game_world.entities",
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
  return GameWorld(std::move(stores), RandomStreams::create(0));
}

GameWorld GameWorld::create(const SimulationConfig& configuration, const MapDefinition& map,
                            const std::uint64_t seed, std::vector<EntitySeed> seeded_entities) {
  // A spawn point that cannot seat a disc of the configured radius would fail the tick that first
  // seated an entity there, so it is rejected here instead: a map and a configuration that cannot
  // be played together fail at construction with a named cause.
  require_spawn_points_are_seatable(configuration, map);

  const std::span<const PhysicsBody> static_bodies = map.static_bodies();
  if (static_bodies.size() > kMaximumEntityCount ||
      seeded_entities.size() > kMaximumEntityCount - static_bodies.size()) {
    throw SimulationValidationError(
        SimulationValidationCode::kGameWorldEntityLimitExceeded, "game_world.entities",
        "map static body count " + std::to_string(static_bodies.size()) + " plus " +
            std::to_string(seeded_entities.size()) +
            " seeded entities exceeds the accepted entity limit");
  }

  // The id policy: `kMinimumEntityId + index` in the map's declared order. Declared order is the
  // map file's own order, so the ids a map seats are a function of the file alone.
  //
  // Seating also fills in the radius. A map declares no size -- no accepted phase reads
  // `PhysicsBody::radius()` and every one of them measures with `SimulationConfig::player_radius()`
  // (`physics_body.hpp`) -- so the configured radius is the only radius a seated static body can
  // truthfully publish, and `physics-body-component.schema.json` requires a positive one. This is
  // the same fill-in `SpawnSystem` performs for a live body, so every body in a published world has
  // the radius the kernel actually measured it with.
  std::vector<ComponentStore<PhysicsBody>::Entry> bodies;
  bodies.reserve(static_bodies.size() + seeded_entities.size());
  for (std::size_t index = 0; index < static_bodies.size(); ++index) {
    bodies.push_back(ComponentStore<PhysicsBody>::Entry{
        EntityId::create(kMinimumEntityId + static_cast<EntityId::Value>(index)),
        static_bodies[index].with_radius(configuration.player_radius())});
  }

  // The map's block is `[kMinimumEntityId, kMinimumEntityId + static_body_count)`. A seeded entity
  // inside it would overwrite map content rather than add to it, so it is named and rejected.
  const EntityId::Value first_free_id =
      kMinimumEntityId + static_cast<EntityId::Value>(static_bodies.size());
  std::vector<ComponentStore<Controllable>::Entry> controllables;
  controllables.reserve(seeded_entities.size());
  std::sort(
      seeded_entities.begin(), seeded_entities.end(),
      [](const EntitySeed& left, const EntitySeed& right) { return left.entity < right.entity; });
  for (const EntitySeed& seed_entity : seeded_entities) {
    if (seed_entity.entity.value() < first_free_id) {
      throw SimulationValidationError(
          SimulationValidationCode::kGameWorldDuplicateEntityId, "game_world.entities.entity_id",
          "seeded EntityId " + std::to_string(seed_entity.entity.value()) +
              " lies inside the map's static-body block, which ends at " +
              std::to_string(first_free_id));
    }
    if (!bodies.empty() && bodies.back().entity == seed_entity.entity) {
      throw SimulationValidationError(
          SimulationValidationCode::kGameWorldDuplicateEntityId, "game_world.entities.entity_id",
          "duplicate EntityId " + std::to_string(seed_entity.entity.value()));
    }
    bodies.push_back(ComponentStore<PhysicsBody>::Entry{seed_entity.entity, seed_entity.body});
    if (seed_entity.controller.has_value()) {
      controllables.push_back(ComponentStore<Controllable>::Entry{
          seed_entity.entity, Controllable{*seed_entity.controller}});
    }
  }

  ComponentStores<ComponentRegistry> stores;
  std::get<ComponentStore<PhysicsBody>>(stores) =
      ComponentStore<PhysicsBody>::create(std::move(bodies));
  std::get<ComponentStore<Controllable>>(stores) =
      ComponentStore<Controllable>::create(std::move(controllables));
  return GameWorld(std::move(stores), RandomStreams::create(seed));
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

GameWorld::GameWorld(ComponentStores<ComponentRegistry> stores, RandomStreams random) noexcept
    : stores_(std::move(stores)), random_(std::move(random)) {}

} // namespace blob_royale::simulation
