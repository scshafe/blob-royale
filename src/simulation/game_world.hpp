#ifndef BLOB_ROYALE_SIMULATION_GAME_WORLD_HPP
#define BLOB_ROYALE_SIMULATION_GAME_WORLD_HPP

#include "component_registry.hpp"
#include "component_store.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "physics_body.hpp"

#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace blob_royale::simulation {

// canonical: game_world -- the complete mutable state of one simulated match.
//
// An entity is an EntityId and nothing else; everything an entity is on a given tick is the set of
// components keyed by its id. The world therefore owns one ascending entity roster plus one store
// per registered component kind, and structural equality and entity destruction are generated over
// ComponentRegistry rather than written per kind.
// related: component_registry.hpp -- the closed list this world is generated from.
class GameWorld final {
public:
  // One entity's complete seeded state. Seeding is the loader's and the fixtures' construction
  // path; from the command step onward live entities arrive through spawn commands instead.
  struct EntitySeed final {
    EntityId entity;
    PhysicsBody body;
    ControllerId controller;

    // Seeds an entity that decides for itself: the controller id is the entity id, which is the
    // identity link every scenario row uses until sessions issue their own controller ids.
    [[nodiscard]] static EntitySeed create(EntityId entity, PhysicsBody body);
    [[nodiscard]] static EntitySeed create(EntityId entity, PhysicsBody body,
                                           ControllerId controller);

    friend bool operator==(const EntitySeed&, const EntitySeed&) = default;
  };

  // Canonicalizes caller order into strict ascending EntityId order. Every seeded entity carries a
  // PhysicsBody and a Controllable, which is exactly what makes it a player entity.
  [[nodiscard]] static GameWorld create(std::vector<EntitySeed> seeds);

  GameWorld(const GameWorld&) = default;
  GameWorld(GameWorld&&) noexcept = default;
  GameWorld& operator=(const GameWorld&) = default;
  GameWorld& operator=(GameWorld&&) noexcept = default;
  ~GameWorld() = default;

  [[nodiscard]] std::span<const EntityId> entities() const& noexcept { return entities_; }
  [[nodiscard]] std::span<const EntityId> entities() const&& = delete;

  [[nodiscard]] bool contains(EntityId entity) const noexcept;

  template <typename Component>
  [[nodiscard]] const ComponentStore<Component>& store() const& noexcept {
    return std::get<ComponentStore<Component>>(stores_);
  }
  template <typename Component>
  [[nodiscard]] const ComponentStore<Component>& store() const&& = delete;

  template <typename Component> [[nodiscard]] ComponentStore<Component>& mutable_store() noexcept {
    return std::get<ComponentStore<Component>>(stores_);
  }

  // Seats one entity at an explicit id, in ascending position. A duplicate id and an oversized
  // roster are validation failures. The per-tick id reservation replaces the explicit argument
  // when commands can create entities.
  void create_entity(EntityId entity);

  // Erases the entity from the roster and from every registered store, so a component kind cannot
  // survive the entity that carried it.
  void destroy_entity(EntityId entity) noexcept;

  friend bool operator==(const GameWorld&, const GameWorld&) = default;

private:
  GameWorld(std::vector<EntityId> entities, ComponentStores<ComponentRegistry> stores) noexcept;

  std::vector<EntityId> entities_;
  ComponentStores<ComponentRegistry> stores_;
};

} // namespace blob_royale::simulation

#endif
