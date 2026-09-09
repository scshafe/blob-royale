# Runtime domain

`blob_runtime` owns the wall-clock lifecycle around one deterministic simulation, and it owns the
whole path from an untrusted command source into a tick. Its canonical public capabilities are
`SimulationRuntime`, the sole mutable simulation owner; `SnapshotPublication`, which gives readers
retained immutable snapshots without granting mutation or lifecycle access; `CommandSink`, the
write-only capability every command source holds; and `ControllerDirectory`, the presentation values
a wire encoder joins by `ControllerId`.

The runtime constructs exactly one persistent `std::jthread`. `start`, `pause`, `resume`, and
`stop` transition that worker rather than replacing it. A successful `pause` is synchronous: once
it returns, no later tick can appear until a resume. `stop` is terminal, joins the worker, and is
idempotent. A worker exception moves the runtime to `failed`, preserves the last complete snapshot,
and can be rethrown through `rethrow_if_failed`. Publication readiness is false until the first
successful post-start tick, and it is cleared whenever the runtime becomes quiescent or terminal.

## The clock

The worker waits for each tick's deadline and advances it by exactly one fixed quantum, so the
cadence is 400 Hz and not "as fast as the last tick allowed". A late worker -- a slow step, or a
thread descheduled under the container's CPU quota -- ticks back-to-back until it has caught up,
but only for `kMaximumCatchUpTicks` quanta (10 ms); further behind than that, the deadline is
re-based to now and the room runs in slow motion for a moment instead of sprinting, because the
sprint is exactly the burst a CFS quota throttles next (`tick_deadline.hpp`, a pure function of
literal time points). **Tick numbers never skip**: a re-base moves the wall-clock deadline and
nothing else, and determinism is per tick sequence. `TickStatistics` counts committed ticks,
overruns, re-bases, and the worst step and lateness seen; the composition root logs
`runtime.tick_overrun` and `runtime.clock_rebased` when they rise, because this library links no
logger.

## The command path

Every tick is built the same way. The worker drains `CommandMailbox` exactly once, asks
`EntityIdAllocator` for a contiguous block of `spawn_count + 1` entity ids, builds one `InputBatch`
under the running mode's accepted command kinds, and calls `GameSimulation::step`. The block is
never empty and the runtime never hands a tick `InputBatch::empty()`, because a mode may create an
entity on its first running tick — royale's shrinking zone is exactly that — and an empty block
there is a hard simulation failure rather than a quieter tick.

The entity-id cursor opens above everything the simulation has already committed *and* above the
whole block its map reserves for static bodies, which `GameWorld::create(configuration, map, seed)`
numbers from `kMinimumEntityId`. That is computed from the committed roster and the map rather than
assumed, so a scenario-seeded id and a map's walls are both covered and a spawn can never be handed
an id an existing entity already holds.

`CommandMailbox` is bounded and mutex-protected. It supersedes rather than appends when a command of
the same kind for the same addressed identity is already pending, which is lossless — `InputBatch`
keeps the last command of a kind per identity — and makes the bound one on distinct commanded
identities rather than on submission rate, so one client cannot evict another's input. On overflow
it evicts the oldest non-lifecycle command to make room for a spawn or a despawn and never the
reverse. No drop is silent: every refusal is returned to the caller and counted in
`CommandMailbox::Statistics`, which `SimulationRuntime::command_mailbox_statistics()` exposes.

`CommandSink` has exactly three operations — `open_session`, `submit`, `close_session`.
`open_session` returns a **`ControllerId`**, not an `EntityId`: the engine chooses entity ids inside
`step` from the tick's reservation, and a controller outlives the entities it drives, so the durable
identity is the only one a session can hold across an elimination. `close_session` enqueues the
controller's `leave` before retiring it, so the tick, not the session, destroys whatever the
controller drove and vacates its seat; a session needs to know nothing about the world to leave it.
The seat itself is taken with a server-issued `join`, which a session submits for itself whenever it
observes that it holds none, exactly as it submits a `spawn` whenever it has no body
(`commands/join_command.hpp`).

## Dependencies

This domain depends only on `blob_simulation` and the C++ threads library. `StructuredLogger` is
deliberately not among them (ADR 0002 § "Ownership and lifecycle"), which is why a dropped command is
observed as a runtime counter and turned into a log line by the composition root.

Network code must receive only `const SnapshotPublication&` and `CommandSink&`; it must never
receive `SimulationRuntime&` or `GameSimulation&`.
