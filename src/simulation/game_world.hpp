#ifndef BLOB_ROYALE_SIMULATION_GAME_WORLD_HPP
#define BLOB_ROYALE_SIMULATION_GAME_WORLD_HPP

#include "component_registry.hpp"
#include "component_store.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "physics_body.hpp"
#include "world_event_registry.hpp"

#include <optional>
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
//
// The world also owns the tick's WorldEvent list, which is how systems within one tick communicate.
// The list is append-only during a tick and the kernel clears it at commit, so events are
// tick-local and never appear in a snapshot; a consequence that must outlive the tick is written
// into a component instead
// (`docs/architecture/0004-gameplay-architecture.md` § "World events").
// related: component_registry.hpp -- the closed list this world is generated from.
// related: world_event_registry.hpp -- the closed list of event kinds this world carries.
class GameWorld final {
public:
  // One entity's complete seeded state. Seeding is the loader's and the fixtures' construction
  // path; from the command step onward live entities arrive through spawn commands instead.
  struct EntitySeed final {
    EntityId entity;
    PhysicsBody body;
    // Absent for a wall or obstacle. A static body is not driven by anyone, so it carries no
    // Controllable, which is also what keeps it out of the protocol v1 player projection.
    std::optional<ControllerId> controller;

    // Seeds an entity that decides for itself: the controller id is the entity id, which is the
    // identity link every scenario row uses until sessions issue their own controller ids.
    [[nodiscard]] static EntitySeed create(EntityId entity, PhysicsBody body);
    [[nodiscard]] static EntitySeed create(EntityId entity, PhysicsBody body,
                                           ControllerId controller);

    // Seeds one of a map's static bodies. Rejects a body that is not static, because a dynamic
    // body with no controller is an entity nothing can ever drive.
    //
    // This is the construction path until Step 19's `GameWorld::create(configuration, map, seed)`
    // seats `MapDefinition::static_bodies()` itself, which is where the id policy for map content
    // belongs.
    [[nodiscard]] static EntitySeed create_static(EntityId entity, PhysicsBody body);

    friend bool operator==(const EntitySeed&, const EntitySeed&) = default;
  };

  // Canonicalizes caller order into strict ascending EntityId order. A seeded entity carries a
  // PhysicsBody, and one that named a controller also carries a Controllable, which is exactly
  // what makes it a player entity rather than a wall.
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

  // This tick's events in production order. Empty on every committed world, because the kernel
  // clears the list at commit.
  [[nodiscard]] std::span<const WorldEvent> events() const& noexcept { return events_; }
  [[nodiscard]] std::span<const WorldEvent> events() const&& = delete;

  // Appends one event. Throws SimulationValidationError when the list would exceed
  // kMaximumWorldEventCount: overflow is a hard simulation failure, never a silent drop, because
  // a dropped event would convert a failure into a differently wrong tick.
  void emit(WorldEvent event);

  // Structural equality includes the pending event list. Two committed worlds therefore compare on
  // their roster and stores alone, because a committed world's list is always empty.
  friend bool operator==(const GameWorld&, const GameWorld&) = default;

private:
  // The commit phase is the only caller: `emit` and `events()` are the whole system-facing surface
  // (`docs/architecture/0004-gameplay-architecture.md` § "World events").
  friend class GameSimulation;

  void clear_events() noexcept { events_.clear(); }

  GameWorld(std::vector<EntityId> entities, ComponentStores<ComponentRegistry> stores) noexcept;

  std::vector<EntityId> entities_;
  ComponentStores<ComponentRegistry> stores_;
  std::vector<WorldEvent> events_;
};

} // namespace blob_royale::simulation

#endif
