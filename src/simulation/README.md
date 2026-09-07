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

## Ownership and invariants

`GameSimulation` owns one `GameWorld` and one `SpatialGrid`; `step(FixedDelta)` is its only mutable
world operation. `GameWorld` owns one ascending `entities()` roster plus one `ComponentStore` per
registered component kind, reached through `store<C>()` and `mutable_store<C>()`. Grid cells contain
non-owning `EntityId` values and are rebuilt deterministically after a committed tick.

Constructors and named factories reject invalid values before they enter the world. A tick computes
against a working state, applies each canonical collision pair once, resolves walls, integrates,
rebuilds the grid, validates the result, and only then commits the tick. If a phase fails, no partial
tick becomes observable. The tick reads and writes only the `PhysicsBody` store, so every other
registered component survives a tick unchanged.

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

Pure equations in `physics.hpp` are the appropriate seam for a newly specified physical rule.
Per-entity durable state belongs in a component, never in a new field on `GameWorld`; avoid a generic
entity hierarchy or a class for each stateless equation. New tick behavior must be placed in the
explicit phase sequence documented by `docs/architecture/0003-deterministic-simulation-contract.md`
and proven deterministic before it is wired into `GameSimulation`.

Player commands are not a simulation shortcut. They require a separately specified application and
protocol boundary that validates identity, ownership, ordering, tick addressing, and authorization
before producing a deterministic domain input.

## Verification

The canonical gate is `./scripts/verify-linux pr`. Focused tests are registered under the
`blob_simulation_unit_tests` CTest target. Linux performance measurements use
`./scripts/run-benchmarks-linux`; benchmark hashes are correctness assertions, not a second engine.
