<!-- canonical: simulation_architecture -- ownership and dependency contract -->

# 2. Adopt a deterministic, value-owned simulation architecture

* **Status:** Accepted
* **Date:** 2026-08-04
* **Deciders:** Project owner

## Context

At the reviewed prototype commit, simulation state, scheduling, locking, serialization, and ambient
access were mixed together. The deleted `GameEngine` singleton, entity hierarchy, cyclic partition
ownership, recursive scheduler, and direct network access are documented with exact historical
source evidence in `docs/PROJECT_DEEP_DIVE.md` and remain available in Git history.

The replacement needs one legible owner for every mutable value, one deterministic reference behavior, and object boundaries that express real state and lifecycle rather than class ceremony.

## Decision

Use six one-way domain libraries plus the production executable:

| Target | Canonical responsibility | May depend on |
|---|---|---|
| `blob_observability` | Bounded atomic JSON-lines events with an injected sink and clock | C++ standard library only |
| `blob_simulation` | Validated simulation values, world ownership, spatial indexing, pure physics, deterministic ticks, immutable snapshot values | C++ standard library only; no Boost, JSON, logging, network, mutexes, condition variables, or threads |
| `blob_runtime` | Clock, lifecycle state machine, one simulation-writer thread, immutable snapshot publication | `blob_simulation`, Threads |
| `blob_protocol` | Versioned JSON envelopes and schema-conformant encoding/decoding | `blob_simulation`, Boost.JSON |
| `blob_server` | TCP/HTTP/WebSocket sessions, routing, limits, and delivery of published snapshots | `blob_runtime`, `blob_protocol`, `blob_observability`, Boost.Asio/Beast |
| `blob_application` | Strict CLI/INI/CSV input boundaries, application composition, process signals, and ordered shutdown | all five domain targets |
| `blob-royale` | Minimal process entry point and top-level error translation | `blob_application` |

Dependencies point downward only. A lower target never calls into or includes a higher target.
`blob_application` is a separately linkable library so application tests exercise the identical
production translation units; compiling those sources again inside a test-only target is forbidden.

Small internal build targets may expose a cohesive production boundary to more than one consumer
without changing this domain graph. `blob_application_input` owns the canonical CLI/INI/CSV
translation units, `blob_server_configuration` owns validated server configuration, and
`blob_server_http_preflight` owns raw HTTP header inspection. The application, server, tests, and
fuzzers link those targets; none recompiles their `.cpp` files. These are implementation components,
not alternate applications or plugin interfaces.

```mermaid
flowchart TD
    Application[BlobRoyaleApplication] --> Runtime[SimulationRuntime]
    Application --> Server[GameServer]
    Application --> Logger[StructuredLogger]
    Server --> Logger
    Server --> Publication[const SnapshotPublication]
    Server --> Protocol[blob_protocol]
    Runtime --> Simulation[GameSimulation]
    Runtime --> Thread[one std::jthread]
    Runtime --> Publication
    Simulation --> World[GameWorld]
    Simulation --> Grid[SpatialGrid]
    Simulation --> Physics[pure physics functions]
    World --> Players[Player values in EntityId order]
    Players --> Bodies[composed PhysicsBody values]
    Grid --> Ids[non-owning EntityId values]
```

### Ownership and lifecycle

* `BlobRoyaleApplication` constructs `SimulationRuntime` before `GameServer`, so reverse-order destruction stops network access before destroying the published state or simulation.
* `SimulationRuntime` owns exactly one `GameSimulation`, one persistent `std::jthread`, and one `SnapshotPublication`. Only its runtime thread may mutate the simulation.
* `SnapshotPublication` owns an atomically replaced `shared_ptr<const WorldSnapshot>`. Readers retain immutable snapshots; they never lock or traverse the mutable world.
* `GameSimulation` owns `GameWorld` and `SpatialGrid`. `step(FixedDelta)` is its only mutation entry point and defines the complete tick order.
* `GameWorld` owns `Player` values in stable `EntityId` order. `Player` composes `PhysicsBody`; neither type owns synchronization, serialization, or spatial membership.
* `SpatialGrid` owns cells containing `EntityId` values. It never owns players and players never point back to cells.
* Stateless vector, integration, wall, and collision equations are named pure functions. A class is introduced only when a capability gains independent state or lifecycle.
* `StructuredLogger` is one concrete process-owned dependency injected into application and server
  boundaries. It never enters simulation, runtime, or protocol values and is not an ambient
  singleton.

### OOP policy

Professional OOP here means encapsulated invariants, explicit construction, deterministic
destruction, and honest relationships. Composition and value semantics are the default. Concrete
constructor injection is preferred while there is one implementation. Inheritance requires a
stable, substitutable “is-a” relationship; the deleted prototype's `Player -> GamePiece` hierarchy
did not meet that test because it represented neither independent ownership nor substitutable
behavior.

The architecture deliberately does not introduce an entity base class, generic repository, service locator, strategy per equation, or interface for every object. These would add names without adding independent capabilities.

## Extension points

* `@extension-point simulation_input`: a future validated input batch may become an argument to `GameSimulation::step`. The baseline has no player-command semantics, so no interface or queue is implemented yet.
* `@extension-point simulation_parallelism`: future bounded parallel work may occur inside tick phases only after native-Linux profiling and equivalence tests prove it useful. `GameSimulation::step` remains the canonical observable contract.
* `@extension-point snapshot_encoding`: new wire encodings may be added within `blob_protocol` without changing domain values or runtime publication. The versioned protocol selects the encoding explicitly.

These are documented seams, not plugin systems. The lightest mechanism is chosen only when a second real implementation exists.

## Considered alternatives

### Repair the dependency-graph scheduler

Rejected. The historical implementation copied notification state instead of updating it, failed to
reset external counts, evaluated predicates across incompatible synchronization domains, and used
recursive detached workers. The exact evidence is retained in `docs/PROJECT_DEEP_DIVE.md`. Repair
would preserve a general graph around six fixed phases before correctness has a serial oracle.

### Adopt an ECS or actor-per-player model

Rejected. The repository has one meaningful entity type and no demonstrated component-query or actor-isolation requirement. A value-owned world is simpler and does not foreclose a later ECS migration if real gameplay creates that cardinality.

### Create polymorphic physics strategies now

Rejected. There is one collision and integration policy. Pure functions expose the mathematical contract more directly; a strategy hierarchy would model hypothetical variation instead of current behavior.

### Keep singleton access and add locks

Rejected. More locks do not create an ownership model and would keep network, lifecycle, and simulation coupled. Constructor ownership plus immutable publication removes the shared mutable boundary.

## What-if stress tests

* **What if gameplay adds multiple entity kinds?** Add composed value types or a tagged value representation inside `GameWorld`; introduce polymorphism only if substitutable behavior actually appears. The runtime and server remain unchanged.
* **What if 100,000 players require parallel physics?** Profile the serial Linux reference, partition a proven tick phase over disjoint buffers, and compare every resulting snapshot to `GameSimulation::step` semantics. No network or entity ownership changes are required.
* **What if snapshots need binary deltas instead of JSON?** Add a versioned encoder in `blob_protocol`; `WorldSnapshot`, publication, and simulation remain unchanged.
* **What if the simulation runs without a network server?** Construct `SimulationRuntime` alone or call `GameSimulation::step` directly in a batch/test process; neither depends on Beast or Asio.

## Consequences

* Deterministic, single-threaded simulation becomes the correctness oracle and simplest debugging surface.
* Network throughput cannot corrupt or block mutable world state; snapshot copying is an accepted initial cost measured later.
* RAII makes normal shutdown and repeated construction/destruction testable.
* Stable ID ordering and explicit tick phases trade some peak throughput for reproducibility.
* The custom scheduler, singleton, global engine parameters, entity locks, cyclic shared ownership, and domain JSON methods must be deleted during the atomic production cutover.
* Future parallelism or entity polymorphism must earn complexity with a concrete requirement, Linux measurements, and preserved observable behavior.

## Related

* [`0001-linux-runtime-contract.md`](0001-linux-runtime-contract.md) — authoritative build and deployment boundary.
* [`../PROJECT_DEEP_DIVE.md`](../PROJECT_DEEP_DIVE.md) — historical evidence that motivated the replacement.
* [`../../src/simulation/README.md`](../../src/simulation/README.md) — current deterministic domain contract.
* [`../../src/runtime/README.md`](../../src/runtime/README.md) — current single-writer lifecycle and publication contract.
* [`../../src/observability/README.md`](../../src/observability/README.md) — current structured event boundary.
* [`../../.claude/plans/2026-08-04-feature-ready-foundation.md`](../../.claude/plans/2026-08-04-feature-ready-foundation.md) — accepted migration and verification sequence.
