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
  controller_observation_queries.hpp/.cpp  canonical retained-snapshot player/component lookups
  controller_steering.hpp/.cpp        canonical target arithmetic and direction-component clamp
  controller_host.hpp/.cpp           one decision pass over the hosted roster
  controller_registry.hpp/.cpp       the closed map from a bot kind name to its factory
  controllers_limits.hpp             every bound this library's values enforce
  controllers_validation_error.hpp   the one `CONTROLLERS.*` exception vocabulary
  wanderer_controller.hpp/.cpp       a seeded random heading, held for a reaction delay
  chaser_controller.hpp/.cpp         thrust toward the nearest other controllable entity
  hill_seeker_controller.hpp/.cpp    thrust toward the hill's centre and hold there
  racer_controller.hpp/.cpp          seek ordered gates and recover toward the centreline
  tactical_controller.hpp/.cpp       one objective go/coast algorithm for every authored profile
  tactical_profile.hpp/.cpp          four validated active settings and bounded profile identity
  tactical_profile_catalogue.hpp/.cpp  immutable ordered configured profiles
  tactical_objective_candidates.hpp/.cpp  public objective providers and terrain screening
  tactical_seed_identity.hpp/.cpp     authored identity and domain-separated per-running seed
  creation_context.hpp               borrowed profile and authored identity during factory calls
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

`Controller::request_thrust` is the shared authoring path for each fresh steering decision by the
four diagnostic bots and tactical. It requires the publicly observed owned dynamic body, suppresses
an observed active stun, and echoes the observed optional Controllable input generation. It
preserves the submitted direction verbatim and adds no normalization, random draw, retry, or
decision timer. Never-invalidated observations therefore produce the previous commands exactly.
The helper reads simulation values only, without depending on gameplay. The authoritative
gameplay lock still admits or ignores the command at its applied tick.

`controller_observation_queries` is the canonical retained-snapshot lookup surface. A published
player is the body/Controllable join, not every body; component lookup retains the component's
own domain. Returned pointers borrow the supplied snapshot, and temporary-snapshot calls are
deleted. `controller_steering` owns written target subtraction, product/square-root order, and
the source-side component clamp. Raw offsets deliberately are not bounded `Vector2` values:
two valid positions may differ by more than that type permits. These helpers add no normalization
or decision policy to the diagnostics; tactical explicitly normalizes its own go direction.

Scripted replay deliberately does not use this helper: typed logs carry their literal token or
absence, and old CSV fixtures remain unchanged. Neither the base `decide` wrapper nor the host
rewrites returned commands. Generation survives status expiry and same-entity body return, not
entity destruction; it is not a promise of invisible body-incarnation detection.

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
new  src/controllers/<name>_controller.{hpp,cpp}         the algorithm and its validated settings
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

Five registered algorithms: the four plain diagnostic kinds `wanderer`, `chaser`, `hill_seeker`,
and `racer`, followed by profiled `tactical`. `Registration::requires_profile` is the admission
metadata; application code reads the same row returned by `ControllerRegistry::find`, not a
parallel list of tactical kinds. The single `create` path takes a sink-issued controller ID, the
unchanged legacy room seed, and optional `CreationContext`. Plain rows reject either context
field. Tactical requires both a borrowed `TacticalProfile` pointer and
`TacticalSeedIdentity{raw match seed, lobby id, authored seat index}`; it copies both values and
retains no pointer. Missing or mismatched context is a named failure, never a default profile.

The unregistered implementation `scripted_replay` is absent on purpose: a registered kind is one a roster line may name,
and a scripted controller is meaningless without the recorded log no configuration line carries, so
a row for it would make `bots=scripted_replay:1` produce a bot that silently decides nothing.
Fixtures construct it directly.

`RacerController` resolves the race block's selected `road` identity against `Observation::terrain()`;
gates stay in the race block and its next gate comes from `race_progress`. It never selects the
first corridor or assumes a fixed name. Missing bindings fail visibly. The canonical simulation
centreline projection supplies the nearest target; controllers own no private geometry loop.
It waits before progress exists, seeks the next gate while centred, and turns
toward the nearest centreline point strictly beyond its personality's `caution_fraction` of the
half-width (default `0.75`, finite in `(0, 1]`). Exact nearest-segment ties keep authored order.
It waits while bodyless, leaving checkpoint return to the mode, and releases thrust after finishing.
This geometric policy uses no random draws; it retains the factory seed and repeats exactly for
the same observation and personality. It links no gameplay rules.

## Configured tactical profiles

`TacticalProfile` and `TacticalProfileCatalogue` are controller-owned immutable values.
`simulation::BotProfileName` is only their shared bounded identity: nonempty lower snake case,
at most 64 bytes. A catalogue may be empty or contain at most 16 unique names in authored order.
`profiles()` exposes a const span; `find(name)` returns a borrowed pointer or explicit absence.
Another personality is another value, never another tactical class or registry row.

The application's strict section-family parser accepts all four required active settings:

| Setting | Accepted values | Meaning |
|---|---|---|
| `objective_seek_probability` | finite `0..1` | Probability of go at a due eligible decision |
| `reaction_delay_ticks` | integer `0..4000` | Delay from observation to the next decision |
| `aim_error` | finite `0..0.25` | Bounded perpendicular-to-forward aim perturbation |
| `target_persistence_ticks` | integer `0..4000` | Lifetime of a still-eligible objective key |

For example, append this section to a complete application configuration and select
`bots=tactical@steady:1` under `[match]`:

```ini
[bot_profile.steady]
objective_seek_probability=1
reaction_delay_ticks=80
aim_error=0.05
target_persistence_ticks=400
```

These are authored example values, not defaults. Missing, repeated, unknown, malformed, nonfinite,
or out-of-range input fails at startup; there is no clamping or inert combat setting. Plain
`kind:count` retains its meaning and must omit a profile. Profiled selection requires a configured
name. The application retains this catalogue and derives one immutable `NpcCatalogue` shared by
runtime and session admission. Only real configured choices are advertised as `npc_profiles`;
tactical is never a bare `npc_controller_kinds` choice. Lobby seats retain the full kind/profile
declaration through pending, occupied, and vacated states. The reconciler guards queued joins with
that declaration and immediately retires work for a replaced declaration.

Profiled startup rosters require the selected mode's actual `StartMatch` capability, available in
hill, race, and royale. Sandbox has no stable authored-seat identity, so it rejects those rosters
and advertises no profiled choices. Unused profile sections may still be configured there.

## Tactical objectives and observation timing

`tactical_objective_candidates` has one closed provider table keyed by public mode-state schema ID.
Hill and zone providers read published centers and radii. Race reads the exact published road
binding and next checkpoint, using canonical centreline recovery strictly beyond the existing
default racer caution fraction. Missing progress waits; finished progress coasts; invalid binding,
progress, or geometry fails visibly. No provider reads private schedules, future ticks, or gameplay
code. Candidate count is bounded at 32 before filtering, and unsupported running schemas fail.

The target point and straight center segment must pass simulation's `terrain_supports_point` and
`first_support_exit`; numerical failures propagate. This screening is not pathfinding and promises
nothing about momentum, perturbed aim, moving terrain, body-radius clearance, hazards, or combat.
Selection orders by squared distance, explicit objective-kind ordinal, then subject identity.
An unexpired lease retains the same eligible key and refreshes its public target without renewing
its acquisition time. Removal, ineligibility, or a changed gate cancels the lease immediately.

The first eligible running dynamic-body observation, body/generation change, and stun recovery
start reaction timing from the actual observed tick. The timer starts even when no objective is
available; repeated empty observations do not restart it. A lost lease restarts reaction timing,
with zero delay permitting immediate reconsideration. Reaction and persistence use checked absolute
`TickWindow` values. Due decisions schedule from the observed tick, never replay missed decisions.
Waiting retains valid current intent; cancellation emits explicit zero when public body/generation
admission permits. Bodyless Controllable waits, while no entity uses the shared spawn-request rule.
Non-running observations clear tactical work and coast where a dynamic body exists. Active stun
does no decision/RNG work, and generation change also cancels work across an entirely missed stun.

Tactical go has unit-strength direction and coast is explicit zero. Distance and seek probability
never scale acceleration. Each due, valid, non-arrived target consumes one seek draw, including at
probability zero or one; go is chosen only when `draw < objective_seek_probability`. Go consumes
one additional aim draw even for zero error. It normalizes the target offset, computes
`e = ((draw * 2) - 1) * aim_error`, then `(ux - e*uy, uy + e*ux)`, and normalizes that pair with
written square-root arithmetic and the canonical component clamp. No trigonometry is used. Missing,
invalid, or arrived targets, bodyless state, non-running phases, and active stun consume no draws.

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

Tactical owns a `DeterministicRandom` seeded by stable authored identity, not allocated controller
or body IDs. `tactical_seed_for` starts with
`h = mix_bits(0x746163746963616c XOR raw_match_seed)`, then mixes XOR lobby ID, seat index,
profile-name length, each unsigned ASCII name byte in order, and public `running_started_tick`.
It reseeds once per newly observed running identity without reseating or reallocating controllers.
There are no draws before running. Profile declaration order is not a seed input; this is
domain-separated variation, not a claim of universal 64-bit collision freedom. Diagnostic factories
retain the unchanged legacy room seed and all prior arithmetic/RNG behavior.

Only tactical rejects observations whose tick is no newer than its last successfully completed
tick. The protected default-true `Controller::accepts_observation` hook runs after controller-ID
validation but before base identity/retry mutation. Tactical's refused observations produce no
commands or state changes, and a failed observation does not consume its tick or partial tactical
state. Diagnostics and literal replay retain their existing repeated-observation behavior.

## Verification

Focused tests are registered under the `blob_controllers_unit_tests` CTest target with the
`unit.controllers.` prefix, mirroring this directory under `tests/unit/controllers/`. Run
`./scripts/verify-focused 'unit.controllers|unit.runtime'`; the canonical gate remains
`./scripts/verify-linux pr`.

The test target additionally links `blob_gameplay`, which the library itself must never link: the
end-to-end pass seats two bots in the real `sandbox` mode behind a real `SimulationRuntime` and
asserts both move, and an in-test mode written to avoid that link would be a second copy of
`SandboxMode` and of `thrust_steering`.

Helper promotion is covered against frozen complete diagnostic behavior on both pinned compiler
lanes. Tactical fixtures separately exercise profile/catalogue validation, seed derivation,
public objective/terrain selection, timing, cancellation, and duplicate-observation admission.
The implementation contract is `docs/reviews/2026-09-11-tactical-profile-contract.md`; these
test descriptions are coverage intent, not a claim that an unrun gate passed.
