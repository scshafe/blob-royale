<!-- canonical: controllers_domain -- the bots, and where a new one goes -->

# Controllers domain

`blob_controllers` is where the **decisions** live. `blob_simulation` owns values and mechanism,
`blob_gameplay` owns the rules that turn that mechanism into a game, `blob_runtime` owns the clock
and the one path from a command source into a tick, and this library owns the in-process agents that
decide what to send down that path
(`docs/architecture/0002-simulation-architecture.md` § "Decision";
`docs/architecture/0004-gameplay-architecture.md` § "Controllers").

It depends on **`blob_runtime` and `blob_simulation` and nothing else**. No gameplay, no protocol,
no server, no observability, no Boost, no logging. A bot that wanted a mode's rules has confused
"what the game does" with "what I am trying to do", and a bot that wanted a logger has forgotten
that this library counts and the composition root logs.

## Controller is a role, not a base class

Three things fill the command-source role, and only two of them implement an interface:

| Filled by | Where | Implements `Controller` |
|---|---|---|
| A networked player's session | `blob_server` | no |
| An in-process bot | `blob_controllers` | yes |
| A scripted replay | `blob_controllers` | yes |

All three hold **exactly two capabilities**: `const SnapshotPublication&` to read and `CommandSink&`
to write. Nothing else. That is why the simulation cannot tell them apart: all three arrive at the
world as indistinguishable `Command` values in one `InputBatch`, the tick stores
`Controllable::controller_id`, and it never branches on it. `blob_server` does **not** depend on this
library and does not have to, because what is shared is the sink and the command vocabulary, not the
interface.

That symmetry is asserted rather than assumed:
`tests/unit/controllers/human_bot_symmetry_tests.cpp`.

## What is here

```
src/controllers/
  controller.hpp/.cpp                the role's in-process form, and the one spawn-request rule
  observation.hpp/.cpp               everything a controller may see, and the only thing
  controller_host.hpp/.cpp           one decision pass over the hosted roster
  controller_registry.hpp/.cpp       the closed map from a bot kind name to its factory
  controllers_limits.hpp             every bound this library's values enforce
  controllers_validation_error.hpp   the one `CONTROLLERS.*` exception vocabulary
  wanderer_controller.hpp/.cpp       a seeded random heading, held for a reaction delay
  chaser_controller.hpp/.cpp         thrust toward the nearest other controllable entity
  hill_seeker_controller.hpp/.cpp    thrust toward the hill's centre and hold there
  scripted_replay_controller.hpp/.cpp  a recorded command log, one step per pass
```

## Identity: `ControllerId` decides, `EntityId` is what it is currently driving

ADR 0004 § "Controllers" was written before plan Step 22 settled the identity split, and this library
resolves the two places that matters.

* A controller holds a **`ControllerId`** for its whole life. It is issued once by
  `CommandSink::open_session`, is never reused, and survives elimination, respawn, and the lobby
  wipe.
* `Observation` is built from that durable identity and **resolves the `EntityId` itself** from the
  snapshot's published `Controllable` store, which is the only place the two identity spaces meet.
  That is how a controller finds itself across a respawn with no lookup table anywhere.
* `Observation::entity()` and `Controller::entity()` are therefore `std::optional`. Absent means
  pending or eliminated, and both are **defined states**, not precondition violations: `decide` is
  still called and may return nothing or a spawn.
* `ControllerHost` decides in **ascending `ControllerId`** order rather than the ADR's sketched
  ascending `entity()`, because `entity()` is absent before a first spawn and changes on every
  respawn, so it is neither total nor stable, while `ControllerId` is both. It is also the key
  `InputBatch` already orders spawns by.

## Asking for a body

A controller with no live entity that wants one returns a single `SpawnCommand` naming its own
`ControllerId` — the identical command a networked session sends after `welcome`. `Controller`
owns that decision for every bot (`Controller::request_body`), including *when it may ask again*:
at most once every `kSpawnRequestRetryTicks` committed ticks (100 ms at the fixed 400 Hz rate),
and immediately after a body it held is destroyed.

Both extremes are wrong. Asking once and never again strands a bot silently and permanently
whenever its spawn was refused. Asking every pass gives **one controller two bodies**, because a
request is in flight for at least one tick and a host deciding faster than the world publishes
would ask again while the engine was already seating it.

**This narrows a gap it cannot close, and the gap is not a controller's.** Nothing in the engine
refuses a spawn from a controller that already drives a body: `SpawnCommand` addresses a
`ControllerId`, `InputBatch` keeps at most one spawn per controller *per tick*, and phase 0 creates
an entity for each. A network client that sent two spawns in two ticks would get two blobs exactly
as a bot would. The rule that makes the second body impossible belongs in kernel phase 0 or in a
mode's `SpawnPolicy`.

## `ControllerHost`

Constructed from `const SnapshotPublication&` and `CommandSink&` and **nothing else**; there is no
overload, setter, or accessor through which a `GameSimulation&`, `GameWorld&`, or
`SimulationRuntime&` could arrive.

`decide_once()` acquires the latest snapshot **once**, builds one `Observation` per controller over
that same retained value, lets every controller decide in ascending `ControllerId`, and submits each
one's commands in the order it returned them before the next controller decides. One acquisition per
pass is what keeps two bots in one roster from deciding against two different worlds — a difference
no human player can observe, and therefore a break of the symmetry in the bots' favour.

**A controller that throws is isolated and counted**, exactly as a misbehaving network session is
refused rather than fatal. Its commands are discarded whole for that pass and the rest of the roster
decides normally; `failed_controller_count` rises and `last_failure()` names the controller, its
kind, its message, and the tick. **A submission the sink refuses is counted and the pass continues**
— the host never retries, because the mailbox supersedes by kind and identity so a retry is either
identical or already superseded. Nothing is swallowed: this library links no logger, so the host
counts and the composition root turns a rising count into a structured line.

## Adding a bot

```
new  src/controllers/<name>_controller.{hpp,cpp}         the behavior and its Personality struct
new  tests/unit/controllers/<name>_controller_tests.cpp  mirroring the source
edit src/controllers/controller_registry.hpp             one include and one row
edit src/controllers/CMakeLists.txt                      the new .cpp
edit match configuration                                 `[match] bots=`
do not touch                                             blob_simulation, blob_runtime,
                                                         blob_gameplay, blob_server, blob_protocol,
                                                         ControllerHost, any other bot
```

`@extension-point controller` — `controller_registry.hpp`. The table is `constexpr`, so two rows
claiming one name fail to compile rather than resolving to whichever was written first.

**Personalities are constructor configuration, not new types.** A cautious wanderer and a twitchy one
are two `WandererController::Personality` values; a timid chaser and a relentless one are two
`ChaserController::Personality` values; a shy seeker and a committed one are two
`HillSeekerController::Personality` values. Adding a class for a mood is the mistake this rule exists to
prevent, and `tests/unit/controllers/wanderer_controller_tests.cpp` and
`chaser_controller_tests.cpp` each assert that a tunable changes behavior with no new type.

Three registered kinds — `wanderer`, `chaser`, and `hill_seeker` — plus an unregistered
implementation, `scripted_replay`. It is unregistered on purpose: a registered kind is one a roster line may name,
and a scripted controller is meaningless without the recorded log no configuration line carries, so
a row for it would make `bots=scripted_replay:1` produce a bot that silently decides nothing.
Fixtures construct it directly.

## Determinism

Controllers inherit the **opposite** obligation from systems. A system runs inside the tick and is
bound by `docs/architecture/0003-deterministic-simulation-contract.md`; a controller runs outside it,
so a bot may be nondeterministic, asynchronous, or model-driven without touching replayability —
**replay replays the recorded command log, not the controller**
(`docs/architecture/0002-simulation-architecture.md` § "Consequences").

`WandererController` is nonetheless reproducible on its own, because a reproducible bug report and a
fixture that needs no scripted log are both worth having. It owns one `DeterministicRandom` — the
tree's one generator, reused rather than re-implemented — draws its two heading components into
named locals in a written order so no compiler's argument evaluation order can reorder them, and
avoids `std::cos`/`std::sin` because the standard trigonometric functions are not bit-specified
across libm implementations.

## Verification

Focused tests are registered under the `blob_controllers_unit_tests` CTest target with the
`unit.controllers.` prefix, mirroring this directory under `tests/unit/controllers/`. Run
`./scripts/verify-focused 'unit.controllers|unit.runtime'`; the canonical gate remains
`./scripts/verify-linux pr`.

The test target additionally links `blob_gameplay`, which the library itself must never link: the
end-to-end pass seats two bots in the real `sandbox` mode behind a real `SimulationRuntime` and
asserts both move, and an in-test mode written to avoid that link would be a second copy of
`SandboxMode` and of `thrust_steering`.
