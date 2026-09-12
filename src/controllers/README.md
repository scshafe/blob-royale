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
  tactical_controller.hpp/.cpp       one weighted decision pipeline for every authored profile
  tactical_profile.hpp/.cpp          seven validated active settings and bounded profile identity
  tactical_profile_catalogue.hpp/.cpp  immutable ordered configured profiles
  tactical_objective_candidates.hpp/.cpp  objective providers, screening, scoring, and selection
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

**No controller sends a shield pulse, and `Controller` gained no capability for one at Step 18.**
The base class deliberately holds exactly two — request a body, request thrust — and bot shield
policy is Step 22b's, so a `request_shield` today would be vocabulary ahead of behaviour. Step 22a
built the decision pipeline and did **not** change this: see "What the pipeline deliberately does
not decide" below for why a controller cannot yet time a shield at all. Human, bot
and replay inputs nonetheless share one admission path: a `ShieldCommand` from any source reaches
the same shared `ability` system and the same phase, body, input-lock, generation, protection and
cooldown checks. Symmetry is provable now without a capability, because `ScriptedReplayController`'s
typed log already carries whole `simulation::Command` values, so a scripted controller emits a
`ShieldCommand` with no change in this domain; the replay `commands.csv` parser accepts a `shield`
verb that uses `entity_id` alone and leaves every other payload column empty, which is the legal
never-invalidated case. The four diagnostic bots and tactical are unchanged, draw no differently,
and observe shield exactly as any other reader does: through the published component.

**Step 19 repeats that exactly: no controller sends a charge and `Controller` gained no
`request_charge`.** Bot charge timing is Step 22b's, and the plan's own words for it — "no knowingly
suicidal charge" — are why a capability landing three steps early would be worse than none: a bot
that could charge but had no policy for holes, hazards or the arena edge would be a bot that could
kill itself. Human, bot and replay inputs share one admission path here too: a `ChargeCommand` from
any source reaches the same shared `ability` system and the same phase, body, input-lock and
generation checks, plus charge's own expired-cooldown, no-active-protection, normalizable-direction
and safety-envelope gates, and the same shield-wins-the-tie rule. Symmetry is again provable without
a capability through `ScriptedReplayController`'s whole-`Command` log; the `commands.csv` parser
learned a `charge` verb that **reuses `thrust`'s two existing direction columns** rather than
declaring a pair of its own, so no existing fixture grew a column. What the two verbs mean by those
cells differs and the difference is not this domain's: `thrust` clamps the magnitude and keeps a
subunit one, `charge` normalizes, so `1,0` and `2,0` are two different thrusts and the same charge,
and `0,0` is a legal thrust release and a refused charge. A bot cannot author a stronger burst than
a human can, for the same reason a client cannot — the strength never leaves the server.

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

The application's strict section-family parser accepts **seven active settings, authored as ten
required keys**. Step 15 shipped the first four; Step 22a added the three the decision pipeline
reads. The four `objective_weight_*` keys are one setting — the per-kind weight vector — authored
one key per name rather than one positional list, so a `.cfg` a human reads names the kind it is
weighting and an omitted one is the parser's own `KEY_MISSING` naming that key.

| Setting | Accepted values | Meaning |
|---|---|---|
| `objective_seek_probability` | finite `0..1` | Probability of go at a due eligible decision |
| `reaction_delay_ticks` | integer `0..4000` | Delay from observation to the next decision |
| `aim_error` | finite `0..0.25` | Bounded perpendicular-to-forward aim perturbation |
| `target_persistence_ticks` | integer `0..4000` | Lifetime of a still-eligible objective key |
| `objective_weight_hill` | finite `0..1` | Preference for a published hill |
| `objective_weight_zone` | finite `0..1` | Preference for a published royale zone |
| `objective_weight_race_gate` | finite `0..1` | Preference for the next race checkpoint |
| `objective_weight_race_recovery` | finite `0..1` | Preference for the centreline recovery point |
| `risk_tolerance` | finite `0..1` | Fraction of a failed escape screen's penalty ignored |
| `prediction_horizon_ticks` | integer `0..400` | Ticks escape screening and intercept look ahead |

The four weight keys are declared in `TacticalObjectiveKind` ordinal order — hill, zone, race gate,
race recovery — in `kConfigFamilyFieldSpecs`, in `TacticalProfile::Section`, and in the order
`TacticalProfile::create` validates, so a section with two bad weights blames the same key at every
layer. `tactical_objective_weight_key` is the one spelling of each name: the configuration parser's
key list reads it rather than repeating the literal, so a key cannot drift between the schema and
the diagnostic that names it.

For example, append this section to a complete application configuration and select
`bots=tactical@steady:1` under `[match]`:

```ini
[bot_profile.steady]
objective_seek_probability=1
reaction_delay_ticks=80
aim_error=0.05
target_persistence_ticks=400
objective_weight_hill=1
objective_weight_zone=1
objective_weight_race_gate=1
objective_weight_race_recovery=1
risk_tolerance=0.5
prediction_horizon_ticks=80
```

These are authored example values, not defaults. Missing, repeated, unknown, malformed, nonfinite,
or out-of-range input fails at startup; there is no clamping or inert combat setting.

Four settings-level rejections are this step's, all `CONTROLLERS.TACTICAL_PROFILE_*`:
`OBJECTIVE_WEIGHT_INVALID`, `RISK_TOLERANCE_INVALID` and `PREDICTION_HORIZON_INVALID` name the key
that failed, and `OBJECTIVE_WEIGHTS_DEGENERATE` names the *section*, because no single key is at
fault. That last one is the only rejection in this library of a combination whose every value is
individually legal: a single zero weight is a real authored answer — "this profile does not care
about that objective" — but **all four at zero is not**, because it is the one weight set an
omission produces (C++ zero-fills an aggregate initializer rather than refusing to compile) and
because it makes selection inexpressive, collapsing every score onto the stable kind ordinal. A
profile with genuinely no preference authors equal *positive* weights, which keeps distance
ordering; that is what `config/blob-royale.cfg` ships.

`risk_tolerance` is not an aggression knob under another name, and the bound is written to keep the
two apart: it only ever scales a penalty *away*, so no value of it can add score to a dangerous
candidate. `prediction_horizon_ticks` is bounded at one second of committed time rather than the
ten the other tick settings allow, because a hosted bot re-decides roughly twenty times a second
and a horizon reaching past twenty of its own future decisions would be a planner's.

Plain `kind:count` retains its meaning and must omit a profile. Profiled selection requires a
configured name. The application retains this catalogue and derives one immutable `NpcCatalogue`
shared by runtime and session admission. Only real configured choices are advertised as
`npc_profiles`; tactical is never a bare `npc_controller_kinds` choice. Lobby seats retain the full
kind/profile declaration through pending, occupied, and vacated states. The reconciler guards queued
joins with that declaration and immediately retires work for a replaced declaration.

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
An unexpired lease retains the same eligible key and refreshes its public target without renewing
its acquisition time. Removal, ineligibility, or a changed gate cancels the lease immediately.

**Escape screening** is the second screen and it *ranks down* rather than deletes. For each
candidate that survived the terrain screen, the collector casts one ray from the body along the
approach direction, of length `normal_top_speed × prediction_horizon_ticks × fixed delta` clamped to
the arena diagonal, and asks the same canonical `simulation::first_support_exit` the screen beside
it already calls. **There is no second support predicate anywhere in this library**, and adding one
would break the single-geometry-owner rule `terrain_queries.hpp` states. A candidate whose approach
already left supported ground before the target was deleted by the first screen, so this ray can
only find the end of support *beyond* the target: what it answers is "if I keep going at the room's
published top speed for my horizon, do I run out of ground?". A yes sets `escape_blocked`, which the
score penalises — it does not remove the candidate, because removal would be one more way for the
collector to return nothing and the step requires a decision when every candidate screens badly,
not silence. Top speed is a
published session fact a browser client reads too, so this costs the human/bot symmetry nothing;
drag is not published and is deliberately not modelled, and omitting it overstates travel, which for
a safety screen is the conservative direction.

**Hill intercept and hold** is the one prediction in this domain that needs nothing unpublished, and
that is worth saying out loud because every other prediction this domain might want *does*.
`ComponentPublication<HillMotion>` strips the private retarget schedule and publishes the hill's
**committed velocity**; `hill_movement` integrates that velocity with no drag term; and the hill
entity owns no `PhysicsBody`, so it is never dragged, never accelerated by contact, and never capped
by a speed limit. Its future centre is therefore exactly `center + velocity × horizon × fixed
delta`, closed form, no unpublished term. Contrast the three predictions Step 22b needs: an
opponent's future position needs `drag_per_second`, which lives on `SimulationConfig` and reaches no
snapshot; a charge needs `charge_speed_fraction`; a parry needs `shield_perfect_window_seconds`.
Intercept and hold are the same computation — a bot already inside a moving hill measures itself
against the future centre and so keeps station instead of arriving where the hill used to be. A hill
with no published `HillMotion`, or a zero horizon, returns the centre unchanged, so every
stationary-hill behaviour is bit-identical to Step 15's. An extrapolated point outside the
representable `Vector2` domain returns the centre unchanged rather than throwing: a throw here would
be isolated by the host and would silently stop the bot for the pass.

The intercept is computed in the provider, so the point terrain screening and escape screening see
**is** the predicted one: a hill about to roam over a hole is screened out on where it is going, not
on where it is. The lease refresh picks up the recomputed intercept on every pass without renewing
the acquisition window, which is what makes hold work — the target moves under a held lease.

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

## Profile-weighted utility selection

**This stage is the point of Step 22a.** Step 15 selected with `min_element` over nearest squared
distance and consulted the profile at four call sites, none of them selection — so two profiles
differing only in numbers picked the *same* candidate on the same frame, and no differentiation
could be proven no matter how many objective kinds were added. ADR 0008 names the path as "published
observation → objective candidates → safety screening → **utility selection** → steering/actions";
the utility stage is what did not exist.

`tactical_candidate_score` is the one written scoring order every profile shares:

```
preference = weight(kind) * (1 - normalized_distance)
penalty    = escape_blocked ? (1 - risk_tolerance) : 0
score      = (preference - penalty) + held_bonus
```

Multiply, subtract, then add, in that order and never reassociated, so two toolchains select the
same candidate bit for bit. `normalized_distance` is the distance to the target over the **published
arena diagonal**, clamped to `[0,1]`, so one authored weight means the same thing on a 960-unit
fixture map and on a ten-kilometre one. Every term lands in the unit interval when the weights do,
which is what makes the penalty commensurate with the preference. Nothing here draws from the
generator: selection is deterministic, and the seek and aim draws stay exactly where Step 15 put
them, in the same order, so no authored profile's stream moved.

`tactical_select_candidate` takes the maximum, and an exact tie falls through to
`tactical_candidate_precedes` — kept from Step 15, no longer the selector but now the *tail* of the
chain. Its order is nearest first, then the key, whose first component is the stable kind ordinal;
keys are unique within one screened set, so this is a strict total order and **no selection can
depend on the order providers happened to push candidates in**.

**Hysteresis is the existing lease, given a bonus, not a second memory beside it.** While the
persistence window is open no selection runs at all. At the moment it ends, the candidate it was
holding carries `kTacticalHeldTargetBonus` (0.125, an eighth of the score range) into the one
comparison that can replace it, so a challenger must beat the held target by more than that bonus.
That damps oscillation between two near-equal candidates without pinning a bot to a stale one.
**Target loss is the other half**: a held key that no longer appears among the screened candidates
releases the lease immediately, cancels held input, and restarts reaction timing.

A weight is not inert today even though each running schema publishes one kind. With a single kind
the weight is a common factor and cannot reorder two candidates by itself; what it reorders is a
candidate against the *escape penalty* and against the *hysteresis bonus*, neither of which it
scales. `w * (proximity_near - proximity_far) > 1 - tolerance` is the authored decision between a
near objective a bot would overshoot into a cliff and a clear one further away, and the weight alone
settles it. `tests/unit/controllers/tactical_controller_tests.cpp` proves exactly that, and proves
it honestly: the two bots share one profile **name**, so they share a seed, a stream and a draw
count, and the only difference between them is one weight. That matters because `tactical_seed_for`
mixes the name's length and every one of its bytes, so two differently *named* profiles already draw
and steer differently before any setting is consulted, and a test comparing two names would prove
nothing.

## Reason codes and bounded work

`TacticalDecisionReason` is a closed 13-value enum recording **which branch produced the decision**,
and `TacticalTargetHold` a closed 7-value enum recording **what became of the held target** on that
same decision. They are two enums rather than one flattened branch-times-hold product, which would
have to be renamed whenever either half grew. Several distinct branches of `decide_next` return the
same observable answer — an empty command vector, or an explicit zero thrust — and before these
enums a test could only tell them apart by side effects: a draw that did not happen, a window that
did not move. A log line would not have fixed that either: free text cannot be asserted on and
drifts from the code the first time a branch moves.

The reasons are `kNotDecided`, `kAwaitingBody`, `kNoControllableBody`, `kMatchNotRunning`,
`kStunned`, `kObjectivesWaiting`, `kObjectivesFinished`, `kNoScreenedCandidate`,
`kAwaitingReaction`, `kArrived`, `kSeekDeclined`, `kPursuing` and `kPursuingUnderRisk`. The holds
are `kNone`, `kAcquired`, `kRetainedInLease`, `kRetainedByBonus`, `kSwitched`, `kLost` and
`kReleased`. Every value is reachable and the unit tests reach each one. `kPursuingUnderRisk` is
the **required fallback made visible**: a bot whose every candidate failed escape screening still
pursues the least bad one rather than throwing or standing still, and this value is how a test
tells that apart from a clean run.

`decision_reason()`, `target_hold()` and `objective_work()` are exposed exactly as the existing
window accessors are, and are rolled back with them when an observation fails validation and is not
consumed — a reason is decision state, not a log.

`TacticalObjectiveWork` counts bounded work **where it happens, rather than estimating it
afterwards**: `raw_candidate_count` (what the 32-candidate throw is measured against, before any
terrain work), `screened_candidate_count`, and `prediction_step_count`. A pass extrapolates at most
one moving-hill centre per raw candidate and casts at most one escape ray per screened candidate,
and screened candidates are a subset of raw ones, so the ceiling is
`kMaximumTacticalPredictionStepCount` = `2 × 32` = 64. It is *derived* from the candidate bound and
the two loops rather than authored, which is why it is stated beside those loops rather than in
`controllers_limits.hpp`, where the bounds an authored input must satisfy live. **There is no
search, no replanning loop and no planner**: a prediction is a fixed number of fixed-delta steps
over a bounded horizon, and a zero horizon performs — and therefore counts — none at all.

**The 32-candidate throw is left exactly as Step 15 wrote it**, and the code says so rather than
leaving it to be rediscovered. `require_candidate_count` throws
`CONTROLLERS.TACTICAL_CANDIDATE_LIMIT_EXCEEDED` on the raw per-provider count *before* terrain work,
and the host isolates that throw, so the bot silently stops acting for the pass. It is honest today
only because exactly one provider runs per running schema, which makes "raw count" and "this
provider's count" the same number. Escape screening adds no per-opponent candidate, so this step
cannot trip the bound and did not raise it. Step 22b's opponent-derived provider is the first that
can run alongside another and the first bounded by 64 seats rather than by 32 candidates; **it must
make the accounting per-provider**, or one provider will exhaust the shared budget.

## What the pipeline deliberately does not decide

This is the most useful thing to know about Step 22a, so it is written here rather than left to a
review document. **Shoving, charge timing and the trajectory-based shield decision are absent on
purpose. They are Step 22b.**

They are absent because *a controller cannot see what they need*. `Observation` carries the
published snapshot and nothing else, and this library links no gameplay:

* **The perfect opening's length is not published.** The shield component publishes
  `activation_tick`, `shield_expiry_tick`, `perfect_expiry_tick`, `cooldown_expiry_tick` and
  `parry_stun_duration_ticks` — all facts of a shield that already exists. A bot deciding *whether
  to raise one* has no window length to aim at, so a "timing error" setting would have no
  denominator without a second copy of `[abilities] shield_perfect_window_seconds` inside this
  library.
* **The charge burst has no published length.** `normal_top_speed` is published;
  `charge_speed_fraction` and `charge_safety_envelope_speed` are not. A bot cannot compute its own
  post-burst velocity, cannot compute a stopping distance, and cannot tell an available charge from
  one the safety envelope will silently refuse.
* **`drag_per_second` reaches no snapshot and no welcome.** It lives on `SimulationConfig`, and the
  three configurations in this tree author 0, 2.0 and 40. At the deployed 2.0 a linear predictor
  overstates travel by about 7% over 32 ticks and about 21% over 0.2 s, the bias is one-signed —
  it always predicts contact *earlier* than it happens — and it flips with a file the bot cannot
  read. That is precisely why hill intercept above is legitimate and an opponent predictor is not:
  the hill does not drag.
* **Shove force needs `restitution`, which exists on the in-process `PhysicsBody` but not on the
  wire.** A controller reading it would see more than a browser client can, breaking the human/bot
  symmetry `tests/unit/controllers/human_bot_symmetry_tests.cpp` asserts.

**And publishing all of them would still not buy a reliable parry.** Hosted bots decide at
presentation cadence — `snapshots_per_second=20` against a 400 Hz tick — so one decision per twenty
committed ticks, on a snapshot that may itself be a publish interval stale, with the command landing
at the next tick's phase 0. The activation tick is `observed + k` for a `k` of roughly 1 to 21 that
the bot cannot observe. **The perfect opening is 32 ticks.** The unobservable activation jitter is
comparable to the entire window and dominates any profile-authored timing error. A bot can raise a
shield in anticipation of a contact; it cannot reliably land the perfect opening, and only a change
to hosted-bot decision cadence would alter that.

**So no aggression, charge-appetite or shield-timing setting is authored here.** ADR 0008 §
"Tactical personalities without a class per mood" forbids an inert combat knob before its behaviour
exists — "No inert aggression/charge/shield settings are accepted before their behavior" — and
`tests/fuzz/corpus/application/rejected-tactical-profile-inert-combat.cfg` enforces the rule: it
authors `aggression=1` and must keep being rejected. It was deliberately **not** migrated with the
other seeds, and it still rejects for the reason its name states — the parser refuses an unknown key
at the line that carries it, before the missing-key sweep runs at the end of the document.

The four named personalities ADR 0008 sketches — Keeper, Bully, Opportunist, Cautious Racer — are
Step 22b's too, because three of the four are defined by combat behaviour. Step 22a added settings,
not profiles, which is also what keeps the tactical-profiles browser spec's exact published profile
list unchanged.

**What Step 22b will and will not be, now that the owner has decided.** ADR 0008 § "Owner decision:
authored caution for bot combat, 2026-09-12" chose **authored caution over derived physics**: none
of the four facts above becomes a v3 wire field, and controllers stay exactly where ADR 0002 puts
them, seeing what a browser sees and nothing more. Step 22b's combat numbers will therefore be
profile-authored ray lengths and anticipation windows, and its code must say so — they are authored
caution, not predictions of the kernel. The accepted consequences are that a bot will sometimes
charge into a wall or a hazard it had no way to predict, and that its shield will often be early or
late. **Neither is a defect to tune away**, and a future reader who "fixes" one by reaching for
gameplay configuration inside a controller is undoing that decision. The parry-cadence limit above
stands under the decision either way.

This also settles what a combat setting authored *here* would have meant. `risk_tolerance` and the
objective weights are read by behaviour Step 22a ships; an aggression, charge-appetite or
shield-timing key would have been read by nothing until 22b, which is precisely the inert knob the
ADR refuses.

Full findings: `docs/reviews/2026-09-12-tactical-combat-preflight.md`. The contract this step was
built to: `docs/reviews/2026-09-12-tactical-pipeline-contract.md`.

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
Step 22a adds coverage for the written scoring order and its unit terms, maximum-score selection
with the stable-ordinal tie-break, escape screening ranking down rather than deleting, hill
intercept extrapolating the published committed velocity only, hysteresis holding through a lease
and then by the bonus until a challenger beats it, a decision when every candidate screens badly,
every reason code and every hold outcome, and bounded work at its derived ceiling — plus the
weight-not-seed differentiation proof, which holds the profile **name** constant.

**A `[bot_profile]` key change breaks four gates outside this library**, which is why Step 22a's
Verify line is wider than Step 15's: `unit.application`, `verify-fuzz-regressions`, `verify-web` and
`verify-browser-e2e` all parse an authored profile section. The implementation contracts are
`docs/reviews/2026-09-11-tactical-profile-contract.md` (Step 15) and
`docs/reviews/2026-09-12-tactical-pipeline-contract.md` (Step 22a); these test descriptions are
coverage intent, not a claim that an unrun gate passed.
