#ifndef BLOB_ROYALE_SIMULATION_GAME_WORLD_HPP
#define BLOB_ROYALE_SIMULATION_GAME_WORLD_HPP

#include "component_registry.hpp"
#include "component_store.hpp"
#include "controller_id.hpp"
#include "deterministic_random.hpp"
#include "entity_id.hpp"
#include "entity_id_reservation.hpp"
#include "entity_roster.hpp"
#include "map_definition.hpp"
#include "match_state.hpp"
#include "physics_body.hpp"
#include "simulation_config.hpp"
#include "world_event_registry.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace blob_royale::simulation {

// canonical: game_world -- the complete mutable state of one simulated match.
//
// An entity is an EntityId and nothing else; everything an entity is on a given tick is the set of
// components keyed by its id. The world therefore owns one store per registered component kind,
// and structural equality and entity destruction are generated over ComponentRegistry rather than
// written per kind.
//
// **There is one answer to "which entities exist" and it is the stores.** `entities()` and
// `contains` are derived rather than stored, so a store write cannot desynchronize a roster and a
// snapshot cannot publish a component for an entity its own `entities()` omits (engine review
// finding 2; `entity_roster.hpp`). The consequence to know is that an entity comes into existence
// when its first component is written: `create_entity()` draws an id, and phase 0 writes the
// `Controllable` that makes the drawn id an entity in the same statement.
//
// The world also owns everything else one match's state consists of:
//
//   * the tick's WorldEvent list, which is how systems within one tick communicate. It is
//     append-only during a tick and the kernel clears it at commit, so events are tick-local and
//     never appear in a snapshot
//     (`docs/architecture/0004-gameplay-architecture.md` § "World events").
//   * `MatchState`, the match-wide state the engine's lifecycle system writes and a mode's systems
//     read.
//   * `DeterministicRandom`, seeded from match configuration. It is the only randomness source
//     inside a tick.
//   * **this tick's EntityIdReservation**, which is what makes `create_entity()` work. The kernel
//     installs the tick's reservation at the start of the tick and clears it at commit, so a world
//     outside a tick can create nothing and a system inside one draws only ids the tick's input
//     value named (engine review finding 3;
//     `docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick").
//
// A `GameWorld&` exists only inside `GameSimulation::step`, so ADR 0002's single-writer property is
// unchanged: systems receive the mutable reference the tick already holds and nothing outside a
// tick can obtain one.
// related: entity_roster.hpp -- the one derivation of "which entities exist".
// related: component_registry.hpp -- the closed list this world is generated from.
// related: world_event_registry.hpp -- the closed list of event kinds this world carries.
class GameWorld final {
public:
  // One entity's complete seeded state.
  //
  // Seeding is **the scenario loader's construction path and nothing else**: `ScenarioLoader`
  // reads a CSV of dynamic player rows and needs a way to place them at explicit ids with explicit
  // positions, which no spawn policy can express. Every other construction site now goes through
  // `create(configuration, map, seed)` for a map's static content and through spawn commands for
  // live entities, so `EntitySeed::create_static` is retired: seating a map's static bodies is the
  // world's own id policy, not a caller's.
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

    friend bool operator==(const EntitySeed&, const EntitySeed&) = default;
  };

  // Canonicalizes caller order into strict ascending EntityId order. A seeded entity carries a
  // PhysicsBody, and one that named a controller also carries a Controllable, which is exactly
  // what makes it a player entity rather than a wall. The generator is seeded at zero, because
  // this path has no match configuration to read a seed from.
  [[nodiscard]] static GameWorld create(std::vector<EntitySeed> seeds);

  // The production construction path: seats the map's static bodies and seeds the generator.
  //
  // **The id policy for map content is owned here.** A static body takes the id
  // `kMinimumEntityId + index` in the map's declared order, so a map's entities are a deterministic
  // function of the map file alone and no caller can choose them. Live entities arrive through
  // spawn commands and draw their ids from each tick's reservation, so the process's monotonic
  // EntityId allocator must issue ids above this block; the allocator arrives with the runtime
  // mailbox in Step 22 and that is where the two meet.
  //
  // The configuration is read to reject a map whose spawn points cannot seat a disc of the
  // configured radius, which is the one place a spawn point meets a radius and turns a
  // mid-match bounds failure into a startup rejection with a named cause.
  [[nodiscard]] static GameWorld create(const SimulationConfig& configuration,
                                        const MapDefinition& map, std::uint64_t seed);

  GameWorld(const GameWorld&) = default;
  GameWorld(GameWorld&&) noexcept = default;
  GameWorld& operator=(const GameWorld&) = default;
  GameWorld& operator=(GameWorld&&) noexcept = default;
  ~GameWorld() = default;

  // Every entity that carries at least one registered component, ascending and distinct.
  //
  // Returned **by value** because it is derived: there is no stored vector to hand out a span
  // into, and a cached one would be the second source of truth this design exists to remove. It
  // allocates, so a kernel phase walks the store it actually reads and only a caller that
  // genuinely needs every entity calls this.
  [[nodiscard]] std::vector<EntityId> entities() const { return roster_of(stores_); }

  // Whether this id names a live entity. Allocation-free: one binary search per registered kind.
  [[nodiscard]] bool contains(const EntityId entity) const noexcept {
    return roster_contains(stores_, entity);
  }

  template <typename Component>
  [[nodiscard]] const ComponentStore<Component>& store() const& noexcept {
    return std::get<ComponentStore<Component>>(stores_);
  }
  template <typename Component>
  [[nodiscard]] const ComponentStore<Component>& store() const&& = delete;

  template <typename Component> [[nodiscard]] ComponentStore<Component>& mutable_store() noexcept {
    return std::get<ComponentStore<Component>>(stores_);
  }

  // Brings one entity into existence, drawing its id from **this tick's** EntityIdReservation.
  //
  // The returned id carries no component yet, so the entity exists from the moment its caller
  // writes the first one -- which for a spawn command is the Controllable phase 0 writes, and for
  // a projectile is the PhysicsBody the firing system writes. Writing none leaves the id
  // consumed and no entity created, which is what "an entity is the set of its components" means.
  //
  // Throws SimulationValidationError when the reservation is exhausted, which is a hard simulation
  // failure and never a silent skip: a reused id would graft one entity's components onto
  // another. A world outside a tick holds the empty reservation, so nothing but a tick can create.
  [[nodiscard]] EntityId create_entity();

  // Erases the entity from every registered store, so a component kind cannot survive the entity
  // that carried it. Total: an id naming no entity is a no-op.
  void destroy_entity(EntityId entity) noexcept;

  // This tick's events in production order. Empty on every committed world, because the kernel
  // clears the list at commit.
  [[nodiscard]] std::span<const WorldEvent> events() const& noexcept { return events_; }
  [[nodiscard]] std::span<const WorldEvent> events() const&& = delete;

  // Appends one event. Throws SimulationValidationError when the list would exceed
  // kMaximumWorldEventCount: overflow is a hard simulation failure, never a silent drop, because
  // a dropped event would convert a failure into a differently wrong tick.
  void emit(WorldEvent event);

  [[nodiscard]] const MatchState& match() const& noexcept { return match_; }
  [[nodiscard]] const MatchState& match() const&& = delete;
  [[nodiscard]] MatchState& mutable_match() noexcept { return match_; }

  [[nodiscard]] DeterministicRandom& random() & noexcept { return random_; }
  [[nodiscard]] const DeterministicRandom& random() const& noexcept { return random_; }
  [[nodiscard]] const DeterministicRandom& random() const&& = delete;

  // What is left of this tick's reservation. A committed world always holds `none()`.
  [[nodiscard]] EntityIdReservation entity_id_reservation() const noexcept { return reservation_; }

  // Structural equality over every part of one match's state. Two **committed** worlds compare on
  // their stores, match state, and generator alone, because a committed world's event list is
  // empty and its reservation is `none()`.
  friend bool operator==(const GameWorld&, const GameWorld&) = default;

private:
  // The kernel is the only caller: `emit`, `events()`, and `create_entity()` are the whole
  // system-facing surface of the tick-local state below
  // (`docs/architecture/0004-gameplay-architecture.md` § "World events").
  friend class GameSimulation;

  // Installs the tick's input reservation, which is what makes `create_entity()` succeed for the
  // duration of one tick.
  void open_tick(const EntityIdReservation reservation) noexcept { reservation_ = reservation; }

  // Discards every tick-local value the commit does not publish: the event list and whatever the
  // tick did not draw from its reservation.
  void close_tick() noexcept {
    events_.clear();
    reservation_ = EntityIdReservation::none();
  }

  GameWorld(ComponentStores<ComponentRegistry> stores, DeterministicRandom random) noexcept;

  ComponentStores<ComponentRegistry> stores_;
  MatchState match_;
  std::vector<WorldEvent> events_;
  DeterministicRandom random_;
  EntityIdReservation reservation_{EntityIdReservation::none()};
};

} // namespace blob_royale::simulation

#endif
