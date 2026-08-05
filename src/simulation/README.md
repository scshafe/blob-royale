<!-- canonical: simulation_domain -- deterministic mutation and value ownership -->

# Simulation domain

`blob_simulation` is the deterministic, single-threaded game-domain core. It has no network, JSON,
logging, Boost, mutex, condition-variable, or thread dependency. This keeps the result reproducible,
cheap to test, and impossible to couple accidentally to transport timing.

## Ownership and invariants

`GameSimulation` owns one `GameWorld` and one `SpatialGrid`; `step(FixedDelta)` is its only mutable
world operation. `GameWorld` owns `Player` values in stable `EntityId` order. Each `Player` composes
one `PhysicsBody`. Grid cells contain non-owning `EntityId` values and are rebuilt deterministically
after a committed tick.

Constructors and named factories reject invalid values before they enter the world. A tick computes
against a working state, applies each canonical collision pair once, resolves walls, integrates,
rebuilds the grid, validates the result, and only then commits the tick. If a phase fails, no partial
tick becomes observable.

`WorldSnapshot` and `PlayerSnapshot` are immutable, copy-owned publication values. Snapshot creation
happens only after a complete tick and retains canonical entity ordering. Older snapshots never
change when the simulation advances.

## Extension points

Pure equations in `physics.hpp` are the appropriate seam for a newly specified physical rule.
World-owned gameplay state belongs in cohesive values composed by `Player` or `GameWorld`; avoid a
generic entity hierarchy or a class for each stateless equation. New tick behavior must be placed in
the explicit phase sequence documented by `docs/architecture/0003-deterministic-simulation-contract.md`
and proven deterministic before it is wired into `GameSimulation`.

Player commands are not a simulation shortcut. They require a separately specified application and
protocol boundary that validates identity, ownership, ordering, tick addressing, and authorization
before producing a deterministic domain input.

## Verification

The canonical gate is `./scripts/verify-linux pr`. Focused tests are registered under the
`blob_simulation_unit_tests` CTest target. Linux performance measurements use
`./scripts/run-benchmarks-linux`; benchmark hashes are correctness assertions, not a second engine.
