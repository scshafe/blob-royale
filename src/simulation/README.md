<!-- canonical: simulation_domain -- deterministic mutation and value ownership -->

# Simulation domain

`blob_simulation` is the deterministic, single-threaded game-domain core. It has no network, JSON,
logging, Boost, mutex, condition-variable, or thread dependency. This keeps the result reproducible,
cheap to test, and impossible to couple accidentally to transport timing.

## The component model

An **entity** is an `EntityId` and nothing else. Everything an entity *is* on a given tick is the set
of components keyed by its id. A **component** is a typed value struct with no behavior beyond
construction and comparison; each kind lives in its own header and declares its own wire name through
a `ComponentKindName` specialization. There is no entity base class and no entity hierarchy
(`docs/architecture/0004-gameplay-architecture.md` § "Entities, components, and stores").

`ComponentStore<C>` is the only component storage implementation. Its `entries()` are strict
ascending `EntityId` order by construction, so **every loop that walks a store is ascending-`EntityId`
for free**, `find` is a binary search, and a reader needing two kinds performs an ordered merge of two
ascending spans rather than a hash lookup.

`component_registry.hpp` is the closed, ordered list of kinds:
`ComponentList<PhysicsBody, Controllable, Lifetime, Score, Team>`. Because it is a type list, three
behaviors are **generated rather than maintained** — structural world equality, `destroy_entity`
erasing from every store, and snapshot publication of every kind — so a new kind cannot forget to
participate in any of them.

A **player** is not a type: it is an entity carrying both a `PhysicsBody` and a `Controllable`.
`PhysicsBody` is the one body value in the game and carries position, velocity, stored acceleration,
radius, mass, the collision layer and mask pair, and `is_static`; the accepted physics phases still
read the one common radius from `SimulationConfig`, so radius, mass, the masks, and `is_static` are
carried but not yet consulted. `ControllerId` is the durable identity of the deciding agent and
outlives the entities it drives; `Controllable` is the only place the two identity spaces meet.

## The command vocabulary

`command_registry.hpp` is the closed, ordered list of command kinds: the variant
`Command = SpawnCommand | DespawnCommand | ThrustCommand`, the `CommandKind` bit enumerators, each
kind's wire name, and each kind's position in phase 0's application order. A command is a value
struct in its own header under `commands/`. `SpawnCommand` names only a `ControllerId` — the engine
draws the new `EntityId` from the tick's reservation and the mode seats it — so a spawn addresses its
controller while every other kind addresses the `EntityId` it names.

`CommandKindMask` is the set of kinds a mode accepts, one integer with `create`, `none`, `all`,
`contains`, and an immutable `with`.

`InputBatch` is the one validated command value a tick may read, and `InputBatch::empty()` is the
no-input tick. `InputBatch::create` canonicalizes to phase 0's application order — despawns, then
spawns, then the remaining kinds, each group ascending by the identity it addresses — keeps the last
submitted command of a kind for an identity, and rejects an unaccepted kind, a thrust direction
component outside `[-1, 1]`, a despawn naming an id inside the batch's own reservation, and a
submitted count above the accepted limit. A thrust direction is carried verbatim: the magnitude clamp
belongs to the mode's steering system, whose written operation order is the contract
(`docs/architecture/0005-royale-mode.md` § "Steering").

`EntityIdReservation` is the contiguous half-open block of ids one tick may bring into existence.
`draw_next` advances it and throws on exhaustion; it never wraps and never reissues a drawn id,
because a reused id would graft one entity's components onto another.

Phase 0 fills `Controllable::commands_this_tick` and does not interpret it. **Command meaning is a
system's job**, so a thrust becomes stored acceleration only when a mode's `kPreKernel` steering
system reads it.

## The event vocabulary

`world_event_registry.hpp` is the closed, ordered list of in-tick event kinds: the variant
`WorldEvent = ContactEvent | SpawnEvent | DespawnEvent | EliminationEvent | ScoreEvent`, the
`WorldEventKind` enumerators, and each kind's diagnostic name. An event is a value struct in its own
header under `events/`.

Systems within one tick communicate through the bounded, ordered list `GameWorld` owns.
`GameWorld::emit` appends in production order and `GameWorld::events()` publishes it; the list is
cleared at every commit, so **events are tick-local and never appear in a snapshot**. A consequence
that must outlive the tick is written into a component instead. The list is bounded by
`kMaximumWorldEventCount` and overflow is a hard failure with
`SIMULATION.GAME_WORLD_EVENT_LIMIT_EXCEEDED`, never a silent drop, because a dropped event would
convert a failure into a differently wrong tick.

## The tick: one fixed kernel, three named stages

`GameSimulation::step(FixedDelta, const InputBatch&)` is the only mutable world operation, and a
tick with no commands is that same call with `InputBatch::empty()`. The numbered phases are kernel
mechanism that no mode may reorder, skip, replace, or add; the stages hold the mode's declared
systems:

```
  phase 0            despawns, then this tick's remaining commands recorded per entity
  ---- kPreKernel -- the mode's systems, declared order
  phase 1            stored acceleration, then drag
  phases 2-6         canonical pairs, contacts, world bounds, integration, spatial reindex
  ---- kPostKernel - the mode's systems, declared order
  ---- kLifecycle -- the mode's systems, declared order
  phase 10           validate, apply DespawnEvent removals, reindex survivors, clear events, publish
```

A `SimulationSystem` is one interface with `name()` and `apply(GameWorld&, const TickContext&)
const`. **`apply` is `const` on purpose:** a system holds immutable configuration and nothing else,
so a tick's result stays a function of the committed world and the tick's `InputBatch` alone.
`TickContext` carries the sequence this tick commits, the fixed delta, and the configuration -- and
no clock and no `InputBatch`, so no system can read a wall time or observe a half-applied intake.

`SystemPipeline` stable-partitions a mode's declared list by `SystemStage` and preserves the
declared order inside each stage, so precedence is a property of the mode's written list and never
of insertion, allocation, or static-initialization order. It rejects a null system, an empty name,
and a duplicate name.

Drag is kernel mechanism, not mode configuration: `SimulationConfig::drag_per_second` is validated
finite and non-negative and phase 1 scales the accelerated velocity by
`max(0, 1 - drag_per_second * dt)`. At the accepted `drag_per_second = 0` the factor is exactly
`1.0`, so **an empty pipeline, zero drag, and an empty batch reproduce every accepted horizon
bit-for-bit** -- asserted against a second, in-test implementation of the accepted seven-phase tick
in `tests/unit/simulation/game_simulation_tests.cpp`.

## Ownership and invariants

`GameSimulation` owns one `GameWorld`, one `SpatialGrid`, and one `SystemPipeline`. `GameWorld` owns
one ascending `entities()` roster, one `ComponentStore` per registered component kind reached
through `store<C>()` and `mutable_store<C>()`, and the tick's `WorldEvent` list. Grid cells contain
non-owning `EntityId` values and are rebuilt deterministically after a committed tick. A
`GameWorld&` exists only inside `step`, so nothing outside a tick can obtain one.

Constructors and named factories reject invalid values before they enter the world. A tick computes
against a working copy of the committed world, so if any phase or stage fails, no partial tick
becomes observable and the previous commit stands unchanged. The numbered phases read and write only
the `PhysicsBody` store and the `Controllable` command lists, so every other registered component
survives a tick unless a system writes it.

`WorldSnapshot` and `PlayerSnapshot` are immutable, copy-owned publication values. A snapshot carries
the ascending entity roster, every registered component kind through `components<C>()`, and the
protocol v1 `players()` projection of the entities carrying both a `PhysicsBody` and a `Controllable`.
Snapshot creation happens only after a complete tick and retains canonical entity ordering. Older
snapshots never change when the simulation advances.

## Extension points

`@extension-point entity_component` — `component_registry.hpp`. Adding an entity kind's vocabulary is
a new value-struct header under `components/` declaring its own `ComponentKindName`, plus one type in
the registry list. `GameWorld`, `GameSimulation`, and existing systems are untouched. Two
implementations beyond the engine set: `Zone` for the royale safe zone, `Flag` for capture the flag.

`@extension-point command_kind` — `command_registry.hpp`. Adding a command kind is a new value-struct
header under `commands/`, then one enumerator, one variant alternative, one `CommandKindName`
specialization, one `CommandKindOf` specialization, one `kCommandKinds` entry, and one application
rank in `command_registry.hpp`; its value validation and the identity it addresses in
`input_batch.cpp`; a consuming system in `blob_gameplay`; and its protocol schema, which is a protocol
minor version. Exactly one existing file in this domain declares the kind, the kernel records
commands without interpreting them, and a mode that omits the kind from its accepted set never sees
it. Two implementations beyond `SpawnCommand` and `DespawnCommand`: `ThrustCommand` for steering, and
a later `UseAbilityCommand` for a dash or a weapon. **Command meaning is a system's job**, so a new
kind adds a consuming system rather than a new kernel sub-step.

`@extension-point simulation_system` — `system_pipeline.hpp`. A mechanic is a new
`SimulationSystem` file plus one line in a mode's declared staged list; the kernel, the other
systems, and every other mode are untouched. Two implementations beyond the engine set:
`zone_shrink` for royale, `hill_scoring` for king of the hill. `SystemStage` decides what a system
may see, not when it happens to have been registered: `kPreKernel` sees start-of-tick positions and
this tick's recorded commands, `kPostKernel` sees committed positions and this tick's events, and
`kLifecycle` sees the tick's final world.

Adding an event kind is a new value-struct header under `events/` plus one type, one enumerator, one
`WorldEventKindName`, one `WorldEventKindOf`, and one `kWorldEventKinds` entry in
`world_event_registry.hpp`, and a consuming system. An event has no meaning until a stage reads it.

Pure equations in `physics.hpp` are the appropriate seam for a newly specified physical rule.
Per-entity durable state belongs in a component, never in a new field on `GameWorld`; avoid a generic
entity hierarchy or a class for each stateless equation. New tick behavior must be placed in the
explicit phase sequence documented by `docs/architecture/0003-deterministic-simulation-contract.md`
and proven deterministic before it is wired into `GameSimulation`.

Player commands are not a simulation shortcut. `InputBatch` is the deterministic domain input and
nothing more: identity, ownership, authorization, tick addressing, rate limiting, and cross-session
ordering are decided at the application and protocol boundary, before a batch exists. A command
source stamps the entity it owns, so no controller can command a foreign entity, and the simulation
never learns whether a command came from a human session or a bot.

## Verification

The canonical gate is `./scripts/verify-linux pr`. Focused tests are registered under the
`blob_simulation_unit_tests` CTest target. Linux performance measurements use
`./scripts/run-benchmarks-linux`; benchmark hashes are correctness assertions, not a second engine.
