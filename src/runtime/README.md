# Runtime domain

`blob_runtime` owns the wall-clock lifecycle around one deterministic simulation. Its canonical
public capabilities are `SimulationRuntime`, which is the sole mutable simulation owner, and
`SnapshotPublication`, which gives readers retained immutable snapshots without granting mutation
or lifecycle access.

The runtime constructs exactly one persistent `std::jthread`. `start`, `pause`, `resume`, and
`stop` transition that worker rather than replacing it. A successful `pause` is synchronous: once
it returns, no later tick can appear until a resume. `stop` is terminal, joins the worker, and is
idempotent. A worker exception moves the runtime to `failed`, preserves the last complete snapshot,
and can be rethrown through `rethrow_if_failed`. Publication readiness is false until the first
successful post-start tick, and it is cleared whenever the runtime becomes quiescent or terminal.

This domain depends only on `blob_simulation` and the C++ threads library. Network code must receive
only `const SnapshotPublication&`; it must never receive `SimulationRuntime&` or `GameSimulation&`.
