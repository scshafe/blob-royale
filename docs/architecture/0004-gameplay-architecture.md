<!-- canonical: gameplay_architecture -- entity, system, mode, and controller framework -->

# 4. Adopt a component, system, and mode gameplay framework

* **Status:** Accepted
* **Date:** 2026-09-06
* **Deciders:** Project owner

## Context and Problem Statement

The foundation is a one-game engine. `GameWorld` owns `Player` values, each `Player` composes one
`PhysicsBody`, and `GameSimulation::step` executes one fixed phase list
(`src/simulation/game_world.hpp`, `src/simulation/player.hpp`,
`src/simulation/game_simulation.hpp`). ADR 0005 adds thrust, drag, a shrinking zone, elimination,
and a four-phase match, and under the current shape every one of those rules lands as a named phase
inside `GameSimulation`. That is the right shape for one game and the wrong shape for the
requirement the project owner stated on 2026-09-06: once multiplayer with working inputs runs, it
must be easy to build various games on this engine and to add mechanics, obstacles, and ways of
interacting **by creating new files and registering them rather than editing core code**, and
computer-controlled entities must be able to act exactly as users do, so bots can carry behaviors,
later personalities, and AI-driven policies.

Both earlier decisions deferred this seam on purpose and named the condition that would discharge
the deferral. ADR 0002 § "Adopt an ECS or actor-per-player model" rejected component storage because
"the repository has one meaningful entity type and no demonstrated component-query requirement," and
ADR 0005 § "Justified extension points and what-if stress" answered the second-mode question with "a
second mode earns a policy seam when it exists; one mode does not." The requirement now names the
second and third games as the reason the foundation exists, so the seam is no longer speculative.
This ADR is the moment those deferrals are discharged, and it is written before the first game ships
precisely so that the first game is built through the framework rather than migrated onto it later.

Two design disciplines govern the result and are cited by name where they bind. *Engineering for
emergence* requires that every seam be justified by at least two plausible distinct implementations,
that precedence never be an insertion accident, and that a new capability be reachable by creating
new files and registering them. *Agent-first engineering* requires one capability in one location
under one name, `canonical:` markers on authoritative implementations, `@extension-point` tags an
agent can grep, and names that survive the water-cooler test — if someone says "the contact rules,"
exactly one thing in the tree answers to that phrase.

This decision applies the general principles "separation of policy from mechanism" — the physics
kernel is mechanism and every game rule is declared policy layered around it; "explicit lifecycle
over implicit" — stage membership, system order, and rule precedence are declared ordered lists,
never registration or allocation order; and "total functions over partial" — an unmatched contact
pair, an unknown map marker, a mode with no objective, and a controller with no live entity each
have a defined result.

These invariants are given, not chosen, and every option below is measured against them:

* Deterministic single-writer simulation, with `GameSimulation::step(FixedDelta, const InputBatch&)`
  as the only mutation entry point.
* Value semantics for everything a tick reads or writes, and ascending `EntityId` iteration
  everywhere.
* No Boost, JSON, logging, or threads inside `blob_simulation`.
* Complete immutable snapshots after each committed tick.
* The server holds only `const SnapshotPublication&` and a write-only `CommandSink&`.
* Every new seam carries an `@extension-point` tag and at least two plausible distinct
  implementations; interfaces stay as small as the capability allows; names pass the water-cooler
  test.

## Considered Options

* **A. Typed component stores, a fixed physics kernel with declared policy sockets, staged systems,
  and modes as declared bundles.** Entities are bare `EntityId` values; components are typed value
  structs in per-kind stores; game rules are `SimulationSystem` values registered at three named
  stages around an unchanged physics kernel; contact behavior is a declared, ordered rule table; a
  `GameMode` declares its systems, rules, commands, spawn policy, and objective; controllers decide
  outside the tick from snapshots. Adding a mechanic is new files plus one registration line.
* **B. Keep one hardcoded game with gameplay phases inside `GameSimulation`.** Cheapest today and
  exactly what ADR 0003's amendment already describes. Rejected: the second game rewrites the tick,
  the third rewrites it again, and every mode-specific branch inside the kernel weakens the one
  property the kernel exists to provide — being the deterministic oracle that no game rule can bend.
  The requirement is explicitly that new games arrive as new files, which this forecloses.
* **C. Classic entity inheritance hierarchy (`Entity` → `Player` → `Blob`, `Entity` → `Obstacle`).**
  Rejected: it is a bet that one taxonomy is correct forever, and this repository already lost that
  bet once — the deleted `Player -> GamePiece` hierarchy is the documented motivation for ADR 0002's
  OOP policy. A capture-the-flag flag is a body, is scored, is carried, and is a spawn anchor; no
  single ancestor chain expresses that without either duplicating behavior across branches or
  hoisting every capability into the base class.
* **D. General dynamic ECS with runtime-registered component types.** Rejected: it trades static
  typing and compile-time completeness for dynamic loading that nothing in this project needs.
  Component access becomes a runtime lookup that can fail, structural `operator==` and the
  compile-time-complete snapshot are lost, and store iteration order becomes a property of a runtime
  registry rather than of the type list — exactly the class of ordering accident that ADR 0003
  § "Floating-point contract" forbids. Option A keeps the storage layout of an ECS and none of its
  dynamism.
* **E. Embed a scripting language for modes.** Deferred, not foreclosed. A script host is a new
  third-party dependency inside or beside the simulation and a new nondeterminism surface inside the
  tick, and it would have to be validated against the same 100-fresh-run bit-identity rule. The
  `GameMode`, `SimulationSystem`, and `ContactRule` contracts of Option A are precisely what a
  future script binding would target, so choosing A costs nothing here and buys the interfaces a
  binding needs.
* **F. Run bots inside the tick as a `SimulationSystem`.** Rejected on three counts and revisited in
  § "Controllers": it puts AI cost inside a 2.5 ms quantum, it entangles bot nondeterminism with the
  oracle so the 100-fresh-run rule can no longer be asserted, and it destroys the symmetry the
  requirement asks for, because a human player cannot be a system.

## Decision Outcome

**Chosen: Option A.**

### Entities, components, and stores

An **entity** is an `EntityId` and nothing else. It owns no fields, no base class, and no behavior.
Everything an entity *is* on a given tick is the set of components keyed by its id.

A **component** is a typed value struct with no methods beyond construction and comparison. Each
kind lives in its own header under `src/simulation/components/` and declares its wire name in the
same file, so the registry never has to be told anything the component header does not already say:

```cpp
// src/simulation/components/physics_body_component.hpp
// canonical: physics_body_component -- the one physical body value in the game.
struct PhysicsBody final {
  Vector2 position;             // wu
  Vector2 velocity;             // wu/s
  Vector2 acceleration;         // wu/s^2, stored state per ADR 0003
  double radius{};              // wu
  double mass{};                // unit mass 1.0 for every baseline dynamic body
  CollisionLayer collision_layer{};
  CollisionLayer collision_mask{};
  bool is_static{};             // walls and obstacles: never integrated, never dragged

  friend bool operator==(const PhysicsBody&, const PhysicsBody&) = default;
};

template <> struct ComponentKindName<PhysicsBody> {
  static constexpr std::string_view value = "physics_body";
};
```

`PhysicsBody` is the existing `src/simulation/physics_body.hpp` value, extended with radius, mass,
the collision layer and mask pair, and the static flag. It is not a second body type, so there is
exactly one thing in the tree anyone can call "the body." `CollisionLayer` is a `std::uint32_t`
bitmask value; a canonical pair is admitted to the contact phase only when `(a.collision_mask &
b.collision_layer)` and `(b.collision_mask & a.collision_layer)` are both nonzero, which is a pure
integer predicate and adds no ordering.

The initial registered component set is everything the engine itself needs. A mode contributes
further kinds to the same one registry — `Zone` for royale, `Flag` for capture the flag, `Hill` for
king of the hill — which is why the list is closed at compile time but not fixed for all time:

| Component | Fields | Owned by | Purpose |
|---|---|---|---|
| `PhysicsBody` | position, velocity, acceleration, radius, mass, collision layer, collision mask, `is_static` | engine | Everything the physics kernel reads or writes. |
| `Controllable` | `controller_id`, `commands_this_tick` | engine | Links an entity to its deciding controller and carries this tick's recorded commands. |
| `Lifetime` | `ticks_remaining` | engine | Self-expiring entities: projectiles, effects, timed pickups. |
| `Score` | `points` | engine | One integer scoreboard cell, on a player entity or a team entity. |
| `Team` | `team_id` | engine | Optional side membership; absent means unaligned. |

Components are stored per kind, never per entity, in one store template that every kind reuses:

```cpp
// canonical: component_store -- the only component storage implementation.
template <typename Component>
class ComponentStore final {
public:
  struct Entry final {
    EntityId entity;
    Component value;
    friend bool operator==(const Entry&, const Entry&) = default;
  };

  // Canonicalizes to strict ascending EntityId order; a duplicate id is a validation failure.
  [[nodiscard]] static ComponentStore create(std::vector<Entry> entries);

  [[nodiscard]] std::span<const Entry> entries() const& noexcept;
  [[nodiscard]] std::span<const Entry> entries() const&& = delete;
  [[nodiscard]] const Component* find(EntityId entity) const& noexcept;
  [[nodiscard]] const Component* find(EntityId entity) const&& = delete;
  [[nodiscard]] std::size_t size() const noexcept;

  void insert_or_assign(EntityId entity, Component value);
  void erase(EntityId entity) noexcept;

  friend bool operator==(const ComponentStore&, const ComponentStore&) = default;
};
```

`entries()` is ascending by construction, so **every loop in the codebase that walks a store is
ascending-`EntityId` for free**, which is the ordering ADR 0003 § "Canonical tick" requires of every
phase. `find` is a binary search over the same ordered vector; a system that needs two components
performs an ordered merge of two ascending spans, not a hash lookup.

The registry is one alias in one file. This file is the closed list of the game's vocabulary:

```cpp
// src/simulation/component_registry.hpp
// canonical: component_registry -- the closed, ordered list of component kinds.
// @extension-point entity_component
using ComponentRegistry = ComponentList<PhysicsBody, Controllable, Lifetime, Score, Team>;
```

`GameWorld` holds `ComponentStores<ComponentRegistry>`, a `std::tuple<ComponentStore<PhysicsBody>,
...>`. Because the list is a type list, three behaviors are generated rather than maintained:
structural `operator==` over the whole world, `destroy_entity` erasing from every store, and
snapshot construction visiting every kind. **Adding a component kind therefore cannot forget to
participate in entity destruction, world equality, or the snapshot** — the three places a
hand-written registry would rot.

`GameWorld` keeps its name and becomes the complete mutable state of one simulated match: it owns
entities, every component store, `MatchState`, the tick's event list, and the seeded generator.

```cpp
// canonical: game_world -- the complete mutable state of one simulated match.
class GameWorld final {
public:
  // Seeds the map's static bodies and the generator; live entities arrive through commands.
  [[nodiscard]] static GameWorld create(SimulationConfig configuration, const MapDefinition& map,
                                        std::uint64_t seed);

  template <typename Component>
  [[nodiscard]] const ComponentStore<Component>& store() const& noexcept;
  template <typename Component>
  [[nodiscard]] ComponentStore<Component>& mutable_store() noexcept;

  [[nodiscard]] std::span<const EntityId> entities() const& noexcept;   // ascending
  [[nodiscard]] EntityId create_entity();                  // draws from this tick's reservation
  void destroy_entity(EntityId entity) noexcept;           // erases from every registered store

  [[nodiscard]] const MatchState& match() const& noexcept;
  [[nodiscard]] MatchState& mutable_match() noexcept;

  [[nodiscard]] std::span<const WorldEvent> events() const& noexcept;
  void emit(WorldEvent event);

  [[nodiscard]] DeterministicRandom& random() noexcept;

  friend bool operator==(const GameWorld&, const GameWorld&) = default;
};
```

A `GameWorld&` exists only inside `GameSimulation::step`, so the single-writer property of ADR 0002
§ "Ownership and lifecycle" is unchanged: systems receive the mutable reference the tick already
holds, and nothing outside the tick can obtain one. `GameSimulation` keeps its shape —

```cpp
[[nodiscard]] static GameSimulation create(SimulationConfig configuration, MapDefinition map,
                                           std::unique_ptr<const GameMode> mode,
                                           GameWorld initial_world);
void step(FixedDelta fixed_delta, const InputBatch& input_batch);
[[nodiscard]] WorldSnapshot snapshot() const;
[[nodiscard]] CommandKindMask accepted_command_kinds() const noexcept;
```

— so `step` remains the only mutation entry point and the mode is injected once, at construction.

**Why not an inheritance hierarchy.** An entity's capabilities are a set, not a path. A
capture-the-flag flag has a body, a team, a score contribution, and a carry relationship; a
king-of-the-hill hill has a body and no controller; a projectile has a body, a lifetime, and a
damage value. Composition expresses each as a set of components; a taxonomy has to choose one axis
to be the spine and then either duplicate capabilities across branches or hoist them all into the
base class, which is the failure ADR 0002 recorded for the deleted `Player -> GamePiece` chain.

**Why not a general dynamic ECS.** Everything valuable in an ECS here is the storage layout —
per-kind contiguous arrays in stable id order — and that is exactly what `ComponentStore` provides.
Runtime component registration would additionally cost static typing at every access site, the
defaulted structural equality that makes fixture comparison free, and the guarantee that snapshot
content is complete at compile time. It would also make iteration order a property of a runtime
registry, which is a class of nondeterminism ADR 0003 forbids by name. The closed type list keeps
the layout and rejects the dynamism.

### The tick: one fixed kernel, three named stages

A tick is a fixed kernel with three declared seams cut into it. The kernel is mechanism and no mode
can reorder, skip, or replace it. The stages are policy and hold the mode's ordered systems:

```
  kernel intake      phase 0   despawn, spawn seating (SpawnPolicy), record commands
  ---- kPreKernel ----         mode systems, declared order
  kernel physics     phase 1   stored acceleration and drag
                     phase 2   canonical candidate pairs
                     phase 3   contact resolution (ContactRuleTable)
                     phase 4   world bounds
                     phase 5   position integration
                     phase 6   spatial reindex
  ---- kPostKernel ---         mode systems, declared order
  ---- kLifecycle ----         mode systems, declared order, then MatchLifecycleSystem
  kernel commit      phase 10  validate, canonicalize signed zero, clear events, publish
```

Phase numbers are ADR 0003 § "Canonical tick" numbers, unchanged. The accepted phases 7 through 9 —
zone radius, elimination, and the single match transition — are not deleted; they are re-expressed
as the `royale` mode's `kPostKernel` and `kLifecycle` systems in the same relative order, so the
committed physical values are identical (§ "Consequences").

The kernel has exactly **two policy sockets**, both evaluated at a fixed point against declared
data: the mode's `SpawnPolicy` in phase 0 and the mode's `ContactRuleTable` in phase 3. There is no
third. A mode that wants to change anything else changes it with a system at a stage.

A system is one interface with one operation:

```cpp
// canonical: simulation_system -- one named unit of game rule executed inside one tick.
// @extension-point simulation_system
class SimulationSystem {
public:
  virtual ~SimulationSystem() = default;
  SimulationSystem(const SimulationSystem&) = delete;
  SimulationSystem& operator=(const SimulationSystem&) = delete;

  // Stable, unique, snake_case identity used by the pipeline, diagnostics, and fixtures.
  [[nodiscard]] virtual std::string_view name() const noexcept = 0;

  // The system's whole effect. Must be a pure function of (world, context) onto world.
  virtual void apply(GameWorld& world, const TickContext& context) const = 0;

protected:
  SimulationSystem() = default;
};
```

`apply` is `const` on purpose: **a system may hold immutable configuration and nothing else.** Every
value a system mutates is world-owned, so the tick's result is a function of the committed world and
the tick's `InputBatch` alone, which is what makes the 100-fresh-run bit-identity rule of ADR 0003
survive an arbitrary number of registered systems. A system that wants per-entity state across ticks
registers a component; a system that wants match-wide state across ticks writes `MatchState`.

Mode configuration is *constructor state of the systems the mode builds*. `ZoneShrinkSystem` is
constructed with the validated royale configuration and holds it as a `const` member. There is no
configuration lookup, no opaque blob in the context, and no downcast, and `TickContext` therefore
stays mode-agnostic and tiny:

```cpp
// canonical: tick_context -- everything a system may read that is not game-world state.
class TickContext final {
public:
  [[nodiscard]] TickSequence tick_sequence() const noexcept;   // the sequence this tick commits
  [[nodiscard]] FixedDelta fixed_delta() const noexcept;
  [[nodiscard]] const SimulationConfig& simulation_config() const& noexcept;
  [[nodiscard]] const MapDefinition& map() const& noexcept;
};
```

`TickContext` deliberately does not expose the `InputBatch`. Commands reach systems only as
`Controllable::commands_this_tick` and as world events, so no system can observe a half-applied
batch or reinterpret the intake order. It also exposes no clock, by construction rather than by
discipline.

Stage membership and order are one declared list. The pipeline stable-partitions by stage and
preserves the declared order inside each stage, so **precedence is a property of the mode's written
list and never of insertion, allocation, or static-initialization order**
(`0002-simulation-architecture.md` § "Extension points"):

```cpp
enum class SystemStage : std::uint8_t { kPreKernel = 0, kPostKernel = 1, kLifecycle = 2 };

// canonical: system_pipeline -- the ordered systems of one mode, grouped by stage.
class SystemPipeline final {
public:
  struct StagedSystem final {
    SystemStage stage;
    std::unique_ptr<const SimulationSystem> system;
  };

  // Stable-partitions by stage. Rejects a duplicate system name so a failure names one system.
  [[nodiscard]] static SystemPipeline create(std::vector<StagedSystem> declared_systems);

  [[nodiscard]] std::span<const StagedSystem> systems_at(SystemStage stage) const& noexcept;
  [[nodiscard]] std::span<const StagedSystem> systems_at(SystemStage) const&& = delete;
};
```

Value semantics govern everything a tick reads or writes — components, `GameWorld`, `InputBatch`,
snapshots. The rule set is different in kind: it is immutable-after-construction polymorphic policy
owned by `GameSimulation` for the life of the match and never copied per tick, so
`std::unique_ptr<const SimulationSystem>` here does not weaken the value-semantics invariant that
applies to state.

**Why exactly three stages.** Two is too few and four is too many, and the boundary that decides it
is what a system can *see*.

* `kPreKernel` systems read start-of-tick positions and this tick's recorded commands, and write
  body intent — stored acceleration, forces, newly created entities. Nothing has moved yet.
* `kPostKernel` systems read committed positions, the rebuilt spatial index, and this tick's contact
  events, and write consequences — damage, score, elimination marks, pickups, captures.
* `kLifecycle` systems read the tick's final world and write roster and match bookkeeping — lifetime
  expiry, respawn queues, placement records. The engine's `MatchLifecycleSystem` is appended last
  and is not removable.

Merging `kPreKernel` and `kPostKernel` would make correctness depend on a system's position in one
long list rather than on a named guarantee about which world it observes. Merging `kPostKernel` and
`kLifecycle` would put the engine's match transition into the same list as mode scoring, so whether
the transition observed this tick's eliminations would be an ordering convention rather than a
declaration — precisely the accident ADR 0003 § "Canonical tick" phases 8 and 9 were written to
prevent. A fourth pre-contact stage was considered and rejected: interior physics ordering is the
oracle, and the contact phase's policy socket is the rule table, not a stage.

### World events

Systems within one tick communicate through a bounded, ordered event list owned by `GameWorld`. The
list is append-only during a tick and is cleared at commit, so **events are tick-local and never
appear in a snapshot**; a consequence that must outlive the tick is written into a component or into
`MatchState`.

```cpp
// src/simulation/world_event_registry.hpp
// canonical: world_event_registry -- the closed list of in-tick event kinds.
using WorldEvent = std::variant<ContactEvent, DespawnEvent, EliminationEvent>;
```

**Correction, 2026-09-07 (plan Step 21).** This section first listed five kinds. `SpawnEvent` and
`ScoreEvent` were registered ahead of any producer and are removed, because a kind nothing emits is
a switch arm, a name, and a header that no reader can reach, and a mode author grepping for how a
kind is used finds nothing (`docs/reviews/2026-09-06-engine-kernel-review.md` finding 17). The three
above each have a producer: the contact phase emits `ContactEvent`, royale's `zone_elimination`
emits `EliminationEvent`, and royale's `placement_recorder` emits `DespawnEvent`, which the commit
applies. Re-adding a kind is its header plus four registry lines, so the rule this states is "every
registered kind has a producer", not "the list is fixed".

`ContactEvent` carries the canonical pair, the contact normal, the relative normal speed, and the
name of the rule row that matched, which is what lets one generic contact phase feed many different
consuming systems. Ordering is total: events are appended in production order, and every producing
phase produces in ascending `EntityId` or canonical-pair order, so the list is a deterministic
function of the tick. `GameWorld::emit` and `GameWorld::events()` are the only two operations; the
list is bounded by `kMaximumWorldEventCount` and overflow is a hard simulation failure, never a
silent drop, because a dropped event is a fallback that would convert a failure into a differently
wrong tick.

### Contact rules

Phase 3 keeps every guarantee ADR 0003 § "Player-pair policy" makes about *mechanism* — canonical
`(lower id, higher id)` pairs, lexicographic order, one evaluation per pair, sequential writes
visible to later pairs, `ε_position` and `ε_velocity` comparisons, and the coincident-center
fallback. What becomes policy is only *which pure equation a matched pair uses*. The equations
themselves stay named pure functions in `physics.hpp`; the table selects among them and contains no
physics. The wall and integration phases are not dispatched through the table at all
(`0002-simulation-architecture.md` § "Extension points").

```cpp
// canonical: contact_rule -- one row of the contact chain of responsibility.
// @extension-point contact_rule
class ContactRule final {
public:
  struct Subject final {
    EntityId entity;
    PhysicsBody body;
  };

  // Free function pointers, not std::function: a predicate or response structurally cannot
  // capture state, which is how purity is enforced rather than merely requested.
  using Predicate = bool (*)(const GameWorld& world, EntityId entity);
  using Response = ContactResponse (*)(const Subject& first, const Subject& second,
                                       const PlayerPairContact& contact, const TickContext& context);

  [[nodiscard]] static ContactRule create(std::string_view name, Predicate first_predicate,
                                          Predicate second_predicate, Response response);

  [[nodiscard]] std::string_view name() const noexcept;
  [[nodiscard]] Predicate first_predicate() const noexcept;
  [[nodiscard]] Predicate second_predicate() const noexcept;
  [[nodiscard]] Response response() const noexcept;
};

// canonical: contact_response -- the complete effect one matched contact may have.
class ContactResponse final {
public:
  [[nodiscard]] static ContactResponse unchanged() noexcept;
  [[nodiscard]] static ContactResponse create(PhysicsBody first_body, PhysicsBody second_body,
                                              std::vector<WorldEvent> events);

  [[nodiscard]] const PhysicsBody& first_body() const& noexcept;
  [[nodiscard]] const PhysicsBody& second_body() const& noexcept;
  [[nodiscard]] std::span<const WorldEvent> events() const& noexcept;
};

// canonical: contact_rule_table -- the ordered chain of responsibility for one mode.
class ContactRuleTable final {
public:
  // Rejects an empty name and a duplicate name. Row order is the declared precedence.
  [[nodiscard]] static ContactRuleTable create(std::vector<ContactRule> rows);

  // The two ADR 0003 rows: elastic_disc, then reflect_static.
  [[nodiscard]] static ContactRuleTable built_in();

  [[nodiscard]] std::span<const ContactRule> rows() const& noexcept;
};
```

**A response may change only the two bodies.** Every other consequence leaves as an event for a
`kPostKernel` system to apply. That is what keeps the kernel's mutation surface exactly what ADR
0003 pinned, and it is why a flag pickup is expressible without giving the contact phase write
access to scores, teams, or the roster.

**Evaluation is a chain of responsibility, stated explicitly.** For each canonical pair `(a, b)`
with `a.id < b.id`, the kernel walks `rows()` in order. A row matches in the canonical orientation
when `first_predicate(a) && second_predicate(b)`, and in the swapped orientation when
`first_predicate(b) && second_predicate(a)`. **Canonical orientation is tried before swapped
orientation, and the first matching (row, orientation) wins.** A matched swapped row receives its
arguments in row orientation and the kernel maps the returned bodies back to the canonical pair. A
pair that matches no row is unchanged, which makes the table total without a default row.

The two built-in rows are the accepted baseline:

| Row | First predicate | Second predicate | Response |
|---|---|---|---|
| `elastic_disc` | dynamic body | dynamic body | ADR 0003's equal-mass frictionless normal-component exchange, plus one `ContactEvent`. |
| `reflect_static` | dynamic body | static body | Reflect the dynamic body's normal component about the contact normal; the static body is unchanged; plus one `ContactEvent`. |

`PhysicsBody` carries `radius` and `mass` so the framework's shape does not have to change when
unequal discs arrive, but the accepted baseline still requires every dynamic body to carry the
configured common radius and unit mass, and `elastic_disc` is defined only for that case. A
general-impulse row for unequal masses is a versioned physics change on the path ADR 0003
§ "Justified extension points and what-if stress" already documents; it is a new row, not a new
seam.

The mode returns the whole table. The engine never appends a row the mode did not list, so a mode
that wants the defaults writes them into its own declared order —
`ContactRuleTable::create(concat(my_rows, ContactRuleTable::built_in().rows()))` — and precedence
between mode rows and built-in rows is visible in the mode's source rather than hidden in engine
composition.

### Game modes and the match lifecycle

A `GameMode` is the complete ruleset of one playable game and the one accepted hierarchy in this
framework (`0002-simulation-architecture.md` § "OOP policy"). Inheritance is honest here: every mode
genuinely *is* a `GameMode`, the simulation calls the same operations on each, and a mode is
substitutable at composition without any other target changing.

```cpp
// canonical: game_mode -- the complete declared ruleset of one playable game.
// @extension-point game_mode
class GameMode {
public:
  virtual ~GameMode() = default;
  GameMode(const GameMode&) = delete;
  GameMode& operator=(const GameMode&) = delete;

  [[nodiscard]] virtual std::string_view name() const noexcept = 0;
  [[nodiscard]] virtual SystemPipeline systems() const = 0;
  [[nodiscard]] virtual ContactRuleTable contact_rules() const = 0;
  [[nodiscard]] virtual CommandKindMask accepted_command_kinds() const noexcept = 0;
  [[nodiscard]] virtual std::unique_ptr<const SpawnPolicy> spawn_policy() const = 0;
  [[nodiscard]] virtual std::unique_ptr<const MatchObjective> objective() const = 0;

  // Rejects a map this mode cannot play, naming the missing marker kind or spawn point.
  virtual void validate_map(const MapDefinition& map) const = 0;

protected:
  GameMode() = default;
};
```

`GameSimulation::create` calls each declaration **exactly once, at construction**, and stores the
results. **Nothing calls into the mode during a tick.** A mode therefore cannot participate in a
tick except through the systems, rules, policy, and objective it declared, which is what makes the
tick's determinism a property of the engine rather than a property of every mode author's
discipline.

A mode is constructed with its own validated configuration and holds it; that configuration is what
it hands to the systems it builds. `validate_map` is the seventh member and earns its place by
turning "capture the flag needs two flag homes" from a runtime surprise into a startup rejection
with a named cause.

The two sub-interfaces a mode declares are each as small as their capability allows:

```cpp
// canonical: spawn_policy -- chooses which map spawn point an entity is seated at.
class SpawnPolicy {
public:
  virtual ~SpawnPolicy() = default;

  // Returns an index into map.spawn_points(), or nullopt to defer this entity one tick.
  // Pure: the shared SpawnSystem owns ascending-EntityId iteration, the occupancy test, the
  // world-owned rotation counter, and the seating write.
  [[nodiscard]] virtual std::optional<std::size_t>
  choose_spawn_point(const GameWorld& world, const TickContext& context, EntityId entity,
                     std::size_t rotation_counter, std::span<const bool> spawn_point_is_free) const = 0;
};

// canonical: match_objective -- the mode's complete contract with the generic match lifecycle.
class MatchObjective {
public:
  virtual ~MatchObjective() = default;

  [[nodiscard]] virtual bool can_start(const GameWorld& world) const = 0;
  [[nodiscard]] virtual MatchOutcome outcome(const GameWorld& world) const = 0;
  [[nodiscard]] virtual MatchLifecycleDurations durations() const noexcept = 0;
};
```

`durations()` is the third member of `MatchObjective` rather than a seventh member of `GameMode`
because `countdown_ticks` and `restart_delay_ticks` are exactly what the generic machine needs and
nothing else reads them. The objective is the mode's whole answer to "when may a match start, when
is it decided, and how long are the two engine-timed phases."

The lifecycle itself is engine-owned and generic:

```cpp
enum class MatchPhase : std::uint8_t { kLobby = 0, kCountdown = 1, kRunning = 2, kEnded = 3 };

// canonical: match_outcome -- who won, expressed once for every mode.
class MatchOutcome final {
public:
  [[nodiscard]] static MatchOutcome undecided() noexcept;
  [[nodiscard]] static MatchOutcome won_by_entity(EntityId winner) noexcept;
  [[nodiscard]] static MatchOutcome won_by_team(TeamId winner) noexcept;
  [[nodiscard]] static MatchOutcome drawn() noexcept;

  [[nodiscard]] bool is_decided() const noexcept;
  [[nodiscard]] std::optional<EntityId> winning_entity() const noexcept;
  [[nodiscard]] std::optional<TeamId> winning_team() const noexcept;

  friend bool operator==(const MatchOutcome&, const MatchOutcome&) = default;
};
```

`MatchLifecycleSystem` is a `SimulationSystem` like any other — there is no second kind of tick
participant — constructed by the engine from the mode's objective and appended last at `kLifecycle`.
It commits **at most one phase transition per tick**, the totality rule that ADR 0003 § "Canonical
tick" phase 9 already requires: `lobby → countdown` when `can_start`; `countdown → lobby` when
`can_start` becomes false; `countdown → running` after `countdown_ticks`; `running → ended` when
`outcome` is decided; `ended → lobby` after `restart_delay_ticks`. `MatchState` — phase, phase start
tick, running start tick, and the last committed outcome — is world state, so any committed snapshot
determines the whole future of the machine.

**Proving the interface with three wildly different modes.** The test the interface has to pass is
that three modes with almost nothing in common inhabit it without changing it. Two changes were made
*because* of this table and are already above: `MatchOutcome::won_by_team`, which royale alone would
never have needed, and team-tagged spawn points.

| Declaration | `sandbox` | `royale` | `capture_the_flag` |
|---|---|---|---|
| `name()` | `sandbox` | `royale` | `capture_the_flag` |
| Components used | `PhysicsBody`, `Controllable` | `PhysicsBody`, `Controllable`, `Zone`, `ZoneExposure` | `PhysicsBody`, `Controllable`, `Team`, `Score`, `Flag`, `RespawnTimer` |
| `kPreKernel` systems | `thrust_steering` | `thrust_steering` | `thrust_steering`, `flag_carry_follow` |
| `kPostKernel` systems | none | `zone_shrink`, `zone_elimination` | `flag_pickup`, `flag_capture`, `flag_return` |
| `kLifecycle` systems | none | `placement_recorder` | `respawn_timer` |
| `contact_rules()` | built-in two | built-in two | `flag_pickup` (pass-through + event), then built-in two |
| `accepted_command_kinds()` | spawn, despawn, thrust | spawn, despawn, thrust | spawn, despawn, thrust |
| `spawn_policy()` | `AnyFreeSpawnPoint`, seats in every phase | `RotatingRingSpawnPolicy`, defers unless `lobby` or `countdown` | `TeamSpawnPolicy`, matches the point's `team_id` |
| `objective().can_start` | always true | alive count at or above `lobby_minimum_players` | at least one alive entity on each of two teams |
| `objective().outcome` | always `undecided` | alive `1` → `won_by_entity`; alive `0` → `drawn` | team `Score` at `captures_to_win` → `won_by_team`; running past `time_limit_ticks` → higher score or `drawn` |
| `objective().durations` | `0` / `0` | `2,000` / `3,200` | `2,000` / `3,200` |
| `validate_map()` | at least one spawn point | at least one spawn point | at least one spawn point per team and one `flag_home` marker per team |
| Mode snapshot state | none | `royale_placements` | `team_scores` |

Sandbox never leaves `running`, has no post-kernel work at all, and seats joiners mid-match. Royale
ends by attrition, holds joiners pending, and ranks losers. Capture the flag ends by score or clock,
respawns the dead, carries an entity that follows another entity, and wins as a team. Nothing in the
seven declarations bends to accommodate any of them.

The zone is worth calling out because it is the clearest evidence that the component model is
carrying its weight: **the royale safe zone is an entity with a `Zone { center, radius }`
component**, not a special field on the world. `zone_shrink` writes it, `zone_elimination` reads it,
the snapshot publishes it like any other component, and the client renders it through the same
component renderer registry that draws blobs. A moving zone, a second zone, or a per-team zone is
then a value change, not an architecture change.

### Maps as data

A map is content, not code. It is a validated value that any mode can play if it provides what that
mode's `validate_map` requires.

```cpp
// canonical: map_definition -- the static, mode-independent content of one arena.
// @extension-point map_definition
class MapDefinition final {
public:
  struct Marker final {
    std::string kind;        // snake_case, e.g. "spawn", "flag_home", "hill_center"
    Vector2 position;
    std::optional<TeamId> team;
    MapMetadata metadata;    // bounded ordered key/value pairs
  };

  [[nodiscard]] static MapDefinition create(std::string name, ArenaBounds bounds,
                                            std::vector<PhysicsBody> static_bodies,
                                            std::vector<Marker> markers, MapMetadata metadata);

  [[nodiscard]] std::string_view name() const noexcept;
  [[nodiscard]] const ArenaBounds& bounds() const& noexcept;
  [[nodiscard]] std::span<const PhysicsBody> static_bodies() const& noexcept;
  [[nodiscard]] std::span<const Marker> markers() const& noexcept;
  [[nodiscard]] std::span<const Marker> spawn_points() const& noexcept;  // markers of kind "spawn"
  [[nodiscard]] const MapMetadata& metadata() const& noexcept;
};
```

**Markers are the one authoring concept.** `spawn_points()` is the ordered projection of markers
whose kind is `spawn`, computed once at load because every mode needs it; it is a convenience, not a
second way to author a point. A mode reads the marker kinds it understands and ignores the rest,
which is what lets any mode play any map; a mode that *requires* a kind rejects the map in
`validate_map` at startup rather than discovering the absence mid-match.

Maps are loaded by the application layer's existing strict loader family — `ApplicationConfigLoader`
for INI and `ScenarioLoader` for CSV (`src/application/README.md`). **No new JSON boundary is
created outside `blob_protocol`.** A map is a data directory: `map.ini` for name, bounds, and
metadata; `static_bodies.csv` for obstacles; `markers.csv` for spawn points and mode props. Adding a
map is adding a directory and naming it in configuration; no code changes at all.

The current scenario CSV is the degenerate map. It stays the canonical initial-state seed that ADR
0003 § "Fixture contract and expected outcomes" depends on, and it is additionally read as a map
with the configured arena bounds, no static bodies, and one `spawn` marker at each row's position.
One file, two derived values, one loader — `ScenarioLoader` is extended, never duplicated.

Arena geometry is authored in the map and consumed by `SimulationConfig::create`, so the
`[simulation]` INI section retires its `world_width` and `world_height` keys and the map file
becomes the single authoring home for arena size (§ "Consequences", operational).

### Commands

`Command` is a closed variant over registered kinds. Adding a kind edits this file and one other,
`input_batch.cpp`; the exact set is at the end of this section:

```cpp
// src/simulation/command_registry.hpp
// canonical: command_registry -- the closed, ordered list of command kinds.
// @extension-point command_kind
using Command = std::variant<SpawnCommand, DespawnCommand, ThrustCommand>;

enum class CommandKind : std::uint32_t { kSpawn = 1u << 0, kDespawn = 1u << 1, kThrust = 1u << 2 };

// canonical: command_kind_mask -- the set of kinds a mode accepts.
class CommandKindMask final { /* bitset over CommandKind, with contains/insert and equality */ };
```

`InputBatch` canonicalizes one tick's commands, exactly as ADR 0003 § "Accepted simulation input"
requires, and now also filters by the mode's accepted set:

```cpp
// canonical: input_batch -- the one validated command value a tick may read.
class InputBatch final {
public:
  // Canonicalizes: ascending EntityId within each kind; the last command of a kind for an entity
  // wins; an entity that both spawns and despawns is a rejection; every component finite and in
  // range. A command whose kind is absent from accepted_kinds is rejected here: the sink already
  // refuses one at submission, so its arrival means the boundary and the engine disagree.
  [[nodiscard]] static InputBatch create(std::vector<Command> commands, CommandKindMask accepted_kinds,
                                         EntityIdReservation entity_id_reservation);

  [[nodiscard]] std::span<const Command> commands() const& noexcept;
  [[nodiscard]] EntityIdReservation entity_id_reservation() const noexcept;
};
```

Kernel phase 0 applies despawns, then spawns, then records every remaining command for its entity
into `Controllable::commands_this_tick`. It does not interpret them. **Command meaning is a system's
job**, which is why a new kind adds a consuming system rather than a new kernel sub-step:

```cpp
struct Controllable final {
  ControllerId controller_id{};
  // At most one per kind, in the batch's one canonical order — phase 0's application order.
  std::vector<Command> commands_this_tick;

  friend bool operator==(const Controllable&, const Controllable&) = default;
};
```

`Controllable` has exactly two fields and will keep exactly two fields, because the two things a
command kind might otherwise want to store already have better homes. Persistent *effect* lives
where ADR 0003 already put it — a thrust writes `PhysicsBody::acceleration`, which persists until
the next thrust, so `ThrustSteeringSystem` at `kPreKernel` produces the identical stored
acceleration ADR 0003 phase 0 produces. Persistent *ability state* — a dash cooldown, an ammunition
count, a held item — is a component, because it is per-entity durable state and that is what
components are.

Adding a command kind is therefore: a new value-type header; six additions in
`command_registry.hpp` — one type in the `Command` variant, one enumerator, one `CommandKindName`,
one `CommandKindOf`, one application rank, and one `addressed_identity_of` arm; its value validation
inside `InputBatch::create`; its consuming system; and its protocol schema. **Two existing files are
edited**, which is the true count and the one § "Libraries, and where a new thing goes" reports;
everything else is new or derived.

The mode's accepted set is published so the boundary can reject early and the client can render only
what it can send: `GameSimulation::accepted_command_kinds()` is copied into the runtime at
construction, `CommandSink` rejects an unaccepted kind at submission, and the protocol v2 `welcome`
message carries the set.

### Controllers

Every command source is a controller. **Controller is a role, not a base class.** A controller turns
observations into commands for the one entity it owns; its source stamps that `EntityId`, so no
controller can command a foreign entity (`0002-simulation-architecture.md` § "Ownership and
lifecycle"). Three things fill the role, and only two of them implement an interface:

* An **in-process controller** implements the `Controller` interface below. `Controller`,
  `Observation`, and `ControllerHost` are `blob_controllers` types, and every bot behavior, later
  personality, and AI-driven policy is a `blob_controllers` implementation.
* A **networked player's session** in `blob_server` fills the role without implementing that
  interface and without depending on `blob_controllers`. It holds only what the role needs: the
  write-only `CommandSink`, the registered `Command` kinds, and the ownership stamp. `blob_server`
  must not depend on `blob_controllers` and does not have to, because what is shared is the sink and
  the command vocabulary, not the interface (`0002-simulation-architecture.md` § "Ownership and
  lifecycle").
* A **scripted replay controller** is a `blob_controllers` implementation used by tests. It is the
  third real implementation on which the interface is accepted
  (`0002-simulation-architecture.md` § "OOP policy").

```cpp
// src/controllers/controller.hpp
// canonical: controller -- one deciding agent for one entity, in-process form.
// @extension-point controller
class Controller {
public:
  virtual ~Controller() = default;

  [[nodiscard]] virtual std::string_view kind() const noexcept = 0;   // "wanderer", "chaser", ...
  [[nodiscard]] virtual EntityId entity() const noexcept = 0;

  // Must return without blocking. An asynchronous controller returns the latest decision it has.
  [[nodiscard]] virtual std::vector<Command> decide(const Observation& observation) = 0;
};

// src/controllers/observation.hpp
// canonical: observation -- everything a controller may see.
class Observation final {
public:
  [[nodiscard]] static Observation create(std::shared_ptr<const WorldSnapshot> snapshot,
                                          EntityId entity);

  [[nodiscard]] const WorldSnapshot& snapshot() const& noexcept;
  [[nodiscard]] EntityId entity() const noexcept;
  [[nodiscard]] TickSequence tick_sequence() const noexcept;   // snapshot().tick_sequence()
};
```

`decide` is deliberately **not** `const`: a controller owns behavior state — a seeded generator, a
personality, a pending asynchronous result — and that is precisely why it lives outside the tick.

`ControllerId` is the durable identity of the deciding agent and `EntityId` is the identity of a
body. They are different because a controller outlives the entities it drives: an eliminated player
that respawns is a new `EntityId` under the same `ControllerId`, which is what makes score
attribution and personality state survive a death without a lookup table. `Controllable` is the only
place the two meet, and the snapshot publishes the link.

A controller whose entity is absent from `snapshot().entities()` is pending or eliminated. `decide`
is still called and may return an empty vector or a spawn, so "a controller with no live entity" is
a defined case rather than a caller precondition.

**Humans and bots are the same thing to the simulation.** A human player's controller is the
`blob_server` session carrying their decisions; a bot's controller is an in-process
`blob_controllers` value hosted by `ControllerHost`; a fixture's controller is `ScriptedController`,
also in `blob_controllers`. All three hold exactly the two capabilities the server holds — `const
SnapshotPublication&` to read and `CommandSink&` to write — and all three arrive at the world as
indistinguishable `Command` values in one `InputBatch`. The simulation stores
`Controllable::controller_id` and **never branches on it and never learns whether a controller is a
human or a bot**, which makes the symmetry a testable invariant rather than an aspiration.

```cpp
// src/controllers/controller_host.hpp
// canonical: controller_host -- runs in-process controllers at presentation cadence.
class ControllerHost final {
public:
  ControllerHost(const runtime::SnapshotPublication& publication, runtime::CommandSink& sink);

  void add(std::unique_ptr<Controller> controller);

  // One decision pass: acquire the latest snapshot once, let every hosted controller decide in
  // ascending entity() order, and submit their commands in that same order.
  void decide_once();
};
```

`ControllerHost` never receives `GameSimulation&`, `GameWorld&`, or `SimulationRuntime&`, so neither
its cadence nor its thread choice can reach a tick. It acquires the snapshot once per pass so every
controller in a pass observes the same world.

**Deterministic and asynchronous bots both work, for the same reason.** A deterministic bot owns a
`DeterministicRandom` seeded from match configuration and is reproducible on its own. An LLM-driven
or learned-policy bot computes off-thread, in another process, or over the network, and `decide`
returns whatever decision is currently available. Neither breaks replay, because **replay replays
the recorded command log, not the controller.** Nondeterminism is confined to the production of
commands; the tick that consumes them is unchanged. Personalities are controller configuration
values — an aggression weight, a reaction delay in presentation frames, a target-selection bias —
not new types.

Match configuration declares the bot roster, resolved through one registry of `kind` name to
factory:

```
[match]
mode=royale
map=maps/arena-960x640
seed=20260906
bots=wanderer:2,chaser:1
```

**Why bots are not systems inside the tick.** This was considered and rejected on three counts. An
AI decision does not fit a 2.5 ms quantum, and a system that blocks on one destroys the fixed-step
contract. A policy called from inside `step` becomes part of the oracle, so any nondeterminism in it
— a thread pool, a network call, a floating-point library difference — destroys ADR 0003's
100-fresh-run bit-identity rule and, with it, every gameplay fixture. And it breaks the symmetry the
requirement asks for, because a human player cannot be a system: making bots systems would give bots
a capability humans do not have and force two code paths for one concept. The one controller that
*is* deterministic by construction — `ScriptedController`, which replays a literal command list — is
what fixtures use, and it is still hosted outside the tick like every other controller.

### Snapshots and protocol shape

Names only; the wire contract is protocol v2, a separate specification
(`.claude/plans/2026-09-06-playable-prototype-tailnet.md` Step 13).

`WorldSnapshot` is still one complete immutable copy taken after a committed tick, and it gains two
things: entities with their components by kind, and a generic match section.

```cpp
class WorldSnapshot final {
public:
  [[nodiscard]] TickSequence tick_sequence() const noexcept;
  [[nodiscard]] std::span<const EntityId> entities() const& noexcept;   // ascending

  template <typename Component>
  [[nodiscard]] std::span<const typename ComponentStore<Component>::Entry>
  component_entries() const& noexcept;

  [[nodiscard]] const MatchSnapshot& match() const& noexcept;
  [[nodiscard]] std::uint64_t random_draw_count() const noexcept;

  friend bool operator==(const WorldSnapshot&, const WorldSnapshot&) = default;
};
```

`MatchSnapshot` carries the mode name, the lifecycle phase, the phase start tick, the running start
tick, the committed `MatchOutcome`, and mode-specific state identified by a `schema_id` and held in
one registered variant. **Mode state should be components wherever it can be** — the zone, flags,
and hills all are — and the mode-specific block carries only what is genuinely not entity-shaped,
such as royale's ordered placement list and capture-the-flag's per-team scores.

Each component kind owns its own schema and its own encoder in `blob_protocol`, and the client
renders through a renderer registry keyed by component kind (`@extension-point entity_renderer`,
client side). Adding a component kind is a new renderer module plus one registration line; the
canvas code does not change.

**Versioning discipline.** Adding a component kind, a command kind, or a mode-state schema is a
**protocol minor version** — `2.1`, `2.2` — and the `welcome` message states the server's exact
version. A client **fails closed** on a kind or schema id it does not know: it reports the unknown
kind once at `warn` with its name and then closes the connection, rather than rendering a world it
cannot fully represent. Removing a kind, renaming one, or changing what one means is a **major
version** and a new route and subprotocol, exactly as v1 to v2 is.

Fail-closed is the deliberate choice over ignore-and-continue. An ignored component is an entity the
player cannot see but can still collide with, and an ignored mode-state schema is an objective the
player cannot see but is still judged by; both present a false world confidently. A closed
connection with a named cause is a worse experience and a better failure, and it keeps the rule that
a client never renders partial state (`0003-deterministic-simulation-contract.md` § "Accepted
simulation input"). The obligation is symmetric: the server rejects a command kind the running mode
does not accept rather than dropping it silently.

### Determinism obligations for framework code

Every rule below binds every mode, system, rule, and map author. They are the price of the framework
and they are checkable.

* **Ascending `EntityId` order everywhere.** Component stores are ascending by construction, systems
  iterate them directly, contact rules are evaluated over lexicographically ordered canonical pairs,
  and events are appended in production order. No loop may use a hash container, pointer order, or
  insertion order.
* **Explicit ordering of systems and rules.** Stage membership and within-stage order come from the
  mode's declared list; rule precedence comes from the table's row order. **Registration is an
  explicit list in a registry file and never a static-initializer side effect**, because
  static-initialization order across translation units is not a determinism guarantee
  (`0002-simulation-architecture.md` § "Extension points").
* **No wall clock.** `blob_simulation` names no clock type. Durations are integer tick counts
  converted once at configuration load; `TickContext` exposes `tick_sequence` and `fixed_delta` and
  nothing time-shaped.
* **Randomness only from `DeterministicRandom`, owned by `GameWorld`.** The generator is written out
  explicitly rather than taken from `<random>`, because standard engines are bit-specified but
  standard distributions are not:

```cpp
// canonical: deterministic_random -- the only randomness source inside a tick.
class DeterministicRandom final {
public:
  [[nodiscard]] static DeterministicRandom create(std::uint64_t seed) noexcept;

  [[nodiscard]] std::uint64_t next_bits() noexcept;            // SplitMix64, specified in this ADR
  [[nodiscard]] double next_unit_interval() noexcept;          // [0, 1) from the top 53 bits
  [[nodiscard]] std::uint64_t next_below(std::uint64_t bound); // rejection sampling, no modulo bias

  [[nodiscard]] std::uint64_t seed() const noexcept;
  [[nodiscard]] std::uint64_t draw_count() const noexcept;

  friend bool operator==(const DeterministicRandom&, const DeterministicRandom&) = default;
};
```

  `draw_count` is committed in every snapshot, so two runs that diverge in how many draws they took
  diverge visibly at the first differing tick instead of silently later.

* **One `EntityId` allocator.** `SimulationRuntime` owns the process's single monotonic allocator
  and places a contiguous `EntityIdReservation` in each tick's `InputBatch`. Session admission draws
  from it, and `GameWorld::create_entity()` — used by systems that spawn projectiles, flags, or
  zones — draws from that tick's reservation. Exhausting the reservation is a hard simulation
  failure, never a silent skip. Because the reservation is part of the tick's input value, a replay
  reproduces simulation-created entity ids exactly.
* **Controllers are outside the tick.** No `SimulationSystem` may call a `Controller`. Neither
  `blob_simulation` nor `blob_runtime` names the `Controller` type at all; it exists only in
  `blob_controllers`, and everything below that library sees commands rather than deciders.
* **Systems hold no mutable state.** `apply` is `const`; a system's members are immutable
  configuration. This is enforced by the interface, not by review.

**A match is reproducible from `(map, mode configuration, seed, command log)`, and that tuple is the
primary fixture format for gameplay tests.** A replay fixture is a directory: `match.ini` naming the
mode, the map path, the seed, and the mode configuration; `commands.csv` with one row per command as
`tick_sequence,entity_id,command_kind,<payload columns>`, ascending by tick then by the batch's own
canonical order; and either expected snapshot digests at named checkpoint ticks or named invariants.
Both files are read by the existing strict INI/CSV loader family. This format is what ADR 0003's
100-fresh-run bit-identity rule is asserted over once gameplay exists, and § "What-if stress tests"
records that it is also, unchanged, what a replay viewer consumes.

### Libraries, and where a new thing goes

`blob_gameplay` and `blob_controllers` are new targets in the dependency graph
(`0002-simulation-architecture.md` § "Decision"). The split between them and `blob_simulation`
follows one rule: **values live in `blob_simulation`, rules live in `blob_gameplay`.**

A component is a plain value struct with no behavior, so `Flag` and `Zone` cost `blob_simulation`
nothing and buy one `GameWorld` type, one `WorldSnapshot` type, and one oracle. What a flag *does* —
pickup, carry, capture, return — is systems and rules, and those live in `blob_gameplay`. The
alternative, templating `GameWorld` on a per-mode component list, would give every mode a different
world type and a different snapshot type, and would end the single-oracle property that the whole
architecture rests on.

`blob_controllers` owns `Controller`, `Observation`, `ControllerHost`, `ScriptedController`, and
every bot behavior. Apart from `blob_application`, which composes the host and the bot roster, it is
the only target that names the `Controller` type at all. `blob_server` does not depend on it: a
network session fills the same role through `CommandSink` and the registered `Command` kinds alone.
That is what keeps the graph one-way while still making a bot and a player indistinguishable to a
tick.

| I want to add | I create | I edit | I do not touch |
|---|---|---|---|
| A component kind | `src/simulation/components/<name>_component.hpp`, its encoder, its client renderer | `component_registry.hpp` (one type), the encoder and renderer registries (one line each) | `GameWorld`, `GameSimulation`, any system |
| A mechanic | `src/gameplay/<mode>/<name>_system.{hpp,cpp}` | the mode's declared system list (one line) | the kernel, other systems |
| An interaction | `src/gameplay/<mode>/<name>_contact_rule.{hpp,cpp}` | the mode's `contact_rules()` (one row) | `physics.hpp`, phase 3 |
| An obstacle | a row in a map's `static_bodies.csv` | nothing | any C++ file |
| A game | `src/gameplay/<mode>/` with its mode class, systems, rules, policies | `game_mode_registry.hpp` (one include and one row in `kGameModeRegistrations`), match configuration | `blob_simulation`, `blob_runtime`, `blob_server`, any other mode |
| A map | a data directory under `maps/` | match configuration | any C++ file |
| A command kind | its value-type header, its schema, its consuming system | `command_registry.hpp` (one type, one enumerator, one `CommandKindName`, one `CommandKindOf`, one application rank, one `addressed_identity_of` arm), `InputBatch::create` validation | the kernel's phase order, `kCommandKinds`, `CommandKindMask` |
| A bot | `src/controllers/<name>_controller.{hpp,cpp}` | `controller_registry.hpp` (one line), match configuration | everything else |

Five of the eight rows are "new files plus one registration line." Two — an obstacle and a map — are
data with no code at all. **A command kind is the one row that is neither**, and this is the honest
count rather than an aspiration: it edits two existing files at six sites in
`command_registry.hpp` plus its value validation in `input_batch.cpp`.
`src/simulation/README.md` § "Extension points" states the same set.

Engine review finding 5 removed one of the three files that row used to name: `recorded_entity_of`
in `game_simulation.cpp` and `addressed_identity_of` in `input_batch.cpp` were one capability with
two implementations, and they are now the single `addressed_identity_of` in `command_registry.hpp`.
Engine review finding 7 removed the `kCommandKinds` entry entirely, because the kind list is now
derived from the variant. What remains in `input_batch.cpp` is the kind's *value* rules — a thrust
direction's component range, a despawn's conflict with the tick's reservation — which is where the
one validated command value a tick may read is built. Collapsing that last site into the value-type
header through a per-kind validation trait, the way `ComponentPublication` carries a component's
publication rule, is the change that would make this row "one existing file"; it is named here and
deliberately not made in the same step that measured the count.

### Justified extension points and what-if stress

Every seam below carries an `@extension-point` tag, names its registration file, and names two
plausible distinct implementations, because a seam with one imaginable implementation is noise
rather than foresight.

| Seam | Tag | Contract | Registration | Two implementations |
|---|---|---|---|---|
| Component kind | `entity_component` | A value struct plus a `ComponentKindName` specialization | `component_registry.hpp` | `Flag` for capture the flag; `Health` for projectile damage |
| System | `simulation_system` | `apply(GameWorld&, const TickContext&) const` plus `name()` | the mode's declared staged list | `zone_shrink` for royale; `hill_scoring` for king of the hill |
| Contact rule | `contact_rule` | `(predicate, predicate, pure response)` returning bodies plus events | the mode's `contact_rules()` row order | `elastic_disc` for blob-on-blob; `flag_pickup` as a pass-through trigger |
| Game mode | `game_mode` | Seven declarations read once at construction | `game_mode_registry.hpp` and `[match] mode=` | `royale`; `capture_the_flag` |
| Map | `map_definition` | Bounds, static bodies, markers, metadata; data only | a directory under `maps/` and `[match] map=` | the 960x640 arena; an obstacle course |
| Command kind | `command_kind` | A value type in the `Command` variant with validation and a consuming system | `command_registry.hpp` | `ThrustCommand`; `FireCommand` |
| Controller | `controller` | The in-process form of the command-source role, in `blob_controllers`: `kind()`, `entity()`, non-blocking `decide(const Observation&)` | `controller_registry.hpp` and `[match] bots=` | seeded `WandererController`; an off-thread LLM-driven controller |
| Entity renderer | `entity_renderer` | A draw function keyed by component kind, client side | the client's renderer registry | the `PhysicsBody` disc renderer; the `Zone` circle renderer |

The stress tests below are the validation of that inventory. Each one is answered with "new files
plus registration" or names the missing seam honestly.

| What if | Answer |
|---|---|
| **Capture the flag** | New files in `blob_gameplay`; new `Flag` and `RespawnTimer` components; one pass-through contact row; four systems. It forced two interface changes, made now: `MatchOutcome::won_by_team` and team-tagged spawn points. Nothing else moved. |
| **An obstacle-course map** | A data directory. Static bodies are placed by `static_bodies.csv` and resolved by the built-in `reflect_static` row. No code at all, and every existing mode can play it. |
| **Projectiles with damage** | New `Health` and `Damage` components; new `FireCommand` kind; `weapon_fire` at `kPreKernel` creating an entity with `PhysicsBody` + `Lifetime` + `Damage`; a `projectile_hit` contact row emitting an event; `damage_application` at `kPostKernel`. New files plus two registry lines. `Lifetime` and `GameWorld::create_entity()` already exist for exactly this. |
| **King-of-the-hill scoring** | A new `Hill` component, a `hill_scoring` system at `kPostKernel` incrementing `Score` in ascending order, an objective returning `won_by_entity` or `won_by_team` at a threshold, and a `hill_center` marker in the map. New files only. |
| **Power-ups** | A new `PowerUpPad` component on a static entity, a `power_up_pickup` pass-through contact row, and a `power_up_application` system granting a timed effect entity carrying `Lifetime`. New files plus one registry line. |
| **A bot driven by an external LLM process** | A new `Controller` in `blob_controllers` owning a child process or HTTP client, returning the latest available decision without blocking. `blob_controllers` may use threads and Boost; `blob_simulation` is untouched. Replay still works because the command log records what the bot actually sent. New file plus one registration line. |
| **A second arena shape (non-rectangular bounds)** | **This names the missing seam.** The kernel's phase 4 is ADR 0003's rectangular fold and is not a policy socket, because that fold is what guarantees a committed center is always in bounds for arbitrarily large finite overshoot. Today the honest answer is to approximate the shape with static bodies inside a rectangular bound, which works with no new seam and inherits the discrete model's tunneling limit. A true `BoundsRule` socket is a third kernel policy point and a versioned physics change on ADR 0003's amendment path. It is named here and deliberately not built. |
| **Team modes** | `Team` is registered from day one, `MatchOutcome::won_by_team` exists, spawn points carry an optional `team_id`, and scoring systems read `Team` like any other component. New files only. |
| **A replay viewer** | The replay fixture format *is* the viewer's input. A viewer constructs `GameSimulation` from `(map, mode configuration, seed, command log)`, drives a `SnapshotPublication` at presentation cadence, and reuses the existing v2 encoder and client unchanged. New files in `blob_application` only — and the fixture format earns its keep twice. |

## Consequences

* **Positive:** Extension is the normal path. Six of the eight ways to change the game are new files
  plus one registration line, and the other two are data files. An agent that greps
  `@extension-point` finds the contract, the registration file, and two worked implementations for
  every seam without reading an existing implementation first.
* **Positive:** The kernel remains the oracle. Phases 1 through 6 keep ADR 0003's equations,
  ordering, tolerances, and tie rules exactly; the only policy a mode injects into them is a
  declared spawn choice and a declared, ordered rule table. Determinism is a property of the
  engine's shape rather than of every mode author's discipline, because `apply` is `const`,
  `TickContext` has no clock, and registration is a written list.
* **Positive:** Humans and bots are symmetric by construction. Both hold exactly the two
  capabilities `const SnapshotPublication&` and `CommandSink&`, both arrive as `Command` values in
  one batch, and the simulation never learns which is which. A recorded command log replays either,
  so a defect found in a playtest and a defect found by a bot swarm produce the same kind of
  fixture.
* **Positive:** The registries are the grep-able map of the game. `component_registry.hpp`,
  `command_registry.hpp`, `world_event_registry.hpp`, `game_mode_registry.hpp`, and
  `controller_registry.hpp` are five files that together enumerate every noun the game knows.
  Reading them is a complete inventory; nothing is discovered dynamically and nothing is registered
  by a side effect.
* **Positive:** A component kind cannot forget to participate. Because the registry is a type list,
  world equality, `destroy_entity`, and snapshot construction are generated over it, so the three
  places a hand-maintained registry rots are structurally covered.
* **Positive:** The reproducibility tuple is a product feature, not only a test format. The replay
  viewer, the fixture format, and the bug report are one artifact.
* **Negative:** There is materially more up-front structure than one game needs. `GameWorld`,
  `ComponentStore`, `SimulationSystem`, `SystemPipeline`, `ContactRuleTable`, `GameMode`,
  `SpawnPolicy`, `MatchObjective`, and `MapDefinition` all exist before the first match is played,
  and the royale rules are more code under this shape than as five named phases in `GameSimulation`.
* **Mitigation:** The cost is paid once and is bounded by this ADR's inventory: nine engine types,
  five of them pure values and four of them interfaces with one to three members each. The
  alternative is paid on every additional game, and the requirement names the second and third games
  as the reason the project exists. The three-mode table above is the evidence that the shape is
  right before any of it is written.
* **Negative:** Per-kind stores cost a join. A system needing `PhysicsBody` and `Team` merges two
  ascending spans instead of dereferencing one object, and a system needing four components merges
  four.
* **Mitigation:** The merge is a linear walk of sorted vectors, which is the access pattern the ADR
  0003 phase order already uses, and both spans are contiguous. Nothing is measured until it runs on
  native Linux under `./scripts/run-benchmarks-linux`; the existing benchmark hashes are the guard
  that a layout change did not change behavior.
* **Negative:** Closed variant registries require a one-file edit per kind. Adding a command kind
  edits `command_registry.hpp` and `InputBatch::create`; adding a component kind edits
  `component_registry.hpp`; adding an event kind edits `world_event_registry.hpp`. The
  architecture's own headline — "new files, not edits" — is not literally true for these three.
* **Mitigation:** This is the deliberate trade against Option D. The edit is one line in a file
  whose entire purpose is to be that list, and in exchange every access site stays statically typed,
  every snapshot stays compile-time complete, and no iteration order depends on a runtime registry.
  A registry file that must be edited to add a kind is also the file an agent greps to learn what
  kinds exist.
* **Negative:** `Controllable::commands_this_tick` is a per-entity vector rewritten every tick, so
  the intake phase allocates where the accepted baseline did not.
* **Mitigation:** The vector is bounded by the number of registered command kinds — three today —
  and its capacity is retained across ticks because the component is assigned, not reconstructed. If
  a native-Linux measurement shows it, the fixed bound makes a fixed-capacity replacement a drop-in
  change behind the same field name.
* **Negative:** Systems dispatch through a virtual call per system per tick, and contact rules
  through a function-pointer call per matched pair, where the baseline inlined everything.
* **Mitigation:** The per-tick system count is a small declared list, not a per-entity cost, and the
  rule call replaces a branch that would otherwise be a hardcoded `if` chain. Both are measured on
  native Linux before either is optimized, and neither can change observable results.
* **Negative:** Two identity spaces now exist — `EntityId` for a body and `ControllerId` for the
  agent that drives it — and a reader must keep them straight.
* **Mitigation:** The distinction is real and load-bearing: a controller outlives the entities it
  drives, which is what makes respawn and score attribution across deaths expressible at all. One
  allocator issues `EntityId`, `Controllable` is the only place the two meet, and the snapshot
  publishes the link.
* **Operational:** The five registry files are the map of the game and should be the first thing any
  agent or human reads. Each carries a `canonical:` marker and an `@extension-point` tag.
* **Operational:** The two central value names follow the existing `GameWorld` and `PhysicsBody`
  types and ADR 0002 § "Ownership and lifecycle" unchanged; this ADR widens what they hold and
  introduces no new name for either.
* **Operational:** `[simulation] world_width` and `world_height` retire in favor of the map's
  `ArenaBounds`, and `[gameplay]` becomes mode-scoped configuration selected by `[match] mode=`.
  Every configuration file listed in ADR 0005 § "Consequences" is affected, and a missing section
  stays a startup rejection rather than a default.
* **Operational:** ADR 0005's rules are unchanged by this ADR; only their placement changes, from
  named tick phases to the `royale` mode's declared systems. Reworking 0005 onto this framework is a
  documentation change with no rule change and no fixture regeneration.
* **Reversibility:** The exit is the built-in path. With the `royale` mode, `drag_per_second = 0`,
  an empty batch, and a map with no static bodies, the pipeline evaluates exactly ADR 0003's phase
  order and commits the same physical values, so the framework is provably additive rather than a
  reinterpretation. Collapsing back means registering one mode, deleting `blob_gameplay` and
  `blob_controllers`, and inlining that mode's systems as named phases — the same edit this ADR
  performs, run backwards, with no change to world ownership, runtime publication, protocol
  encoding, or the server boundary.

## Related

* [`0002-simulation-architecture.md`](0002-simulation-architecture.md) — ownership, the dependency
  graph including `blob_gameplay` and `blob_controllers`, the OOP policy that admits the mode,
  system, contact-rule, and controller seams, and the `@extension-point` inventory this ADR gives
  contracts to.
* [`0003-deterministic-simulation-contract.md`](0003-deterministic-simulation-contract.md) — units,
  exact `FixedDelta`, the canonical tick phases this ADR re-expresses as kernel plus stages, pair
  and wall policy, tolerances, and the 100-fresh-run bit-identity rule.
* [`0005-royale-mode.md`](0005-royale-mode.md) — the first mode's rules. Its rules are unchanged; a
  later step re-expresses them as the `royale` mode's declared systems, and repairs the ADR 0003
  cross-references that still cite this file's number for the safe zone, elimination, spawning, and
  match phases.
* [`../protocol/v1.md`](../protocol/v1.md) — the envelope, ordering, and field-dictionary discipline
  protocol v2 extends with per-component-kind schemas and the match section.
* [`../../src/simulation/README.md`](../../src/simulation/README.md) — the deterministic domain
  contract that will own `GameWorld`, the component stores, the kernel, and the stage seams.
* [`../../src/server/README.md`](../../src/server/README.md) — the read-only transport boundary that
  gains a write-only `CommandSink&` and nothing else.
* [`../../.claude/plans/2026-09-06-playable-prototype-tailnet.md`](../../.claude/plans/2026-09-06-playable-prototype-tailnet.md) — accepted execution constraints, and Steps 14 through 21, which implement this framework.

**Amended 2026-09-06:** Versioning discipline is fail-closed rather than ignore-and-continue. An
ignored component kind is an entity a player cannot see but can still collide with, and an ignored
mode-state schema is an objective a player cannot see but is still judged by. `docs/protocol/v2.md`
specifies the closing behavior and its close codes. Controller presentation values -- the controller
kind and display name published inside the wire `controllable` component -- are joined at the server
boundary from a runtime-owned controller directory keyed by `ControllerId`, not stored in the
`Controllable` component, so the deterministic core carries no proxy-supplied strings.
