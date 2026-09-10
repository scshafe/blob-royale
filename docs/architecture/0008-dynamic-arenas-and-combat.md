<!-- canonical: dynamic_arenas_and_combat -- proposed shared terrain, motion, abilities, tuning, and tactical personalities -->

# 8. Dynamic arenas, player abilities, and tactical personalities

* **Status:** Proposed — not approved or implemented
* **Date:** 2026-09-10
* **Baseline:** `b282a90`
* **Decider:** Project owner

## Intent

Make hill movement unpredictable, make opponents tactically interesting, and make positioning
matter through cliffs, charge, and a timing-sensitive shield. Build these from shared capabilities,
not parallel hill/race physics, a class per bot mood, or client-only rule changes.

The camera follow-up is implemented locally at the baseline. This proposal does not imply its
deployment, completion of human playtesting, or authorization to change the live hill server.
The executable sequence is
`.claude/plans/2026-09-10-dynamic-arenas-and-combat.md`.

## Owner decisions and remaining proposals

On 2026-09-10, the owner confirmed adjustable numeric acceleration/top-speed controls and live,
room-wide tuning. Other first-version choices below remain proposals, not additional requirements
attributed to the owner or approval to implement the whole design.

| Choice | Decision / recommendation | Alternative / consequence |
|---|---|---|
| Movement controls | **Confirmed:** adjustable numeric acceleration and top-speed controls | **Proposed:** normal propulsion ceiling semantics, Apply/Reset, and finite safety bounds; no on/off-only or unlimited-speed setting. |
| When tuning applies | **Confirmed:** live, room-wide, applied atomically on an authoritative server tick | Publish the same revision/effective tick to everyone in the room; do not defer running-match changes until the next match. |
| Shield interaction | Tap to activate a short timed shield | A held shield needs hold/release intent and stamina/recharge policy; leave this out of the first version unless requested. |
| Perfect-shield benefit | Zero the incoming dynamic object's momentum and apply a temporary input lock | Not a frozen/static object: other impacts may still move it. |
| Falling | A player's centre entering void causes elimination; charge does not jump | A body-radius fall threshold or jumping is a different mechanic. |
| Session protocol | A coordinated v3 session contract | v2.6 can retain derived duplicate road fields temporarily, but needs a removal migration. Do not keep two permanent road representations. |

Initial timings below are playtest hypotheses. Approval of this design permits their initial
implementation, not a claim that they are balanced.

## Existing facts that constrain the design

- The exact quantum is 1/400 second. Current contacts inspect start-of-tick overlap, then walls
  fold motion and positions integrate; high-speed tunnelling is explicitly accepted by the old
  contract (`docs/architecture/0003-deterministic-simulation-contract.md`,
  `src/simulation/game_simulation.cpp:453`, `tests/unit/simulation/physics_tests.cpp:353`).
- Acceleration is captured by `ThrustSteeringSystem`; velocity receives acceleration and drag,
  but no top-speed cap (`src/gameplay/shared/thrust_steering_system.cpp:61`,
  `src/simulation/game_simulation.cpp:394`).
- Contact responses may replace two bodies and emit events; rule rows are first-match-wins
  (`src/simulation/contact_rule.hpp:25`). A shield/lethal/charge flag-combination table would grow
  badly, and a post-kernel stun alone cannot undo an earlier incorrect collision response.
- Race already treats centre-off-road as elimination, using a union of round-ended segment
  corridors, but gate and off-road checks sample endpoints. Its current same-tick finish-before-fall
  test is intentional old behavior that this proposal changes, not a test to delete silently
  (`src/gameplay/race/race_course.cpp:79`, `src/gameplay/race/race_mode.cpp:50`,
  `tests/unit/gameplay/race/race_mode_tests.cpp:180`).
- Elimination detection and consequences are already separate. Shared respawn removes the body
  but retains the entity/controller, so new temporary states need cleanup
  (`src/gameplay/shared/respawn_system.cpp:67`).
- Hill movement is a marker tour; its non-participant hill entity survives round reset
  (`src/gameplay/king_of_the_hill/hill_geometry.cpp:23`,
  `src/gameplay/shared/match_reset_system.cpp:19`).
- Controllers read published snapshots and submit normal commands. They cannot link gameplay;
  personalities are configuration values, not subclasses. The racer currently repeats gameplay's
  polyline arithmetic because there is no shared lower-level geometry capability
  (`src/controllers/README.md`, `src/controllers/racer_controller.cpp:24`).
- Rooms already have independent runtimes and cooperative lobby controls, with no host role.
  Do not mutate process-wide `/api/v1/config` to tune one room
  (`src/simulation/game_simulation.cpp:98`, `src/application/blob_royale_application.cpp:417`,
  `src/server/game_api_router.cpp:625`).

## Architecture: one owner per capability

| Capability | Canonical owner | Consumers |
|---|---|---|
| Immutable terrain geometry and pure geometric queries | `blob_simulation`, proposed `terrain_definition` / `terrain_queries` | Kernel, gameplay, bots, snapshot publication |
| Fixed-step continuous motion and actual path records | `blob_simulation`, proposed `continuous_motion` | Contacts, support loss, race progress |
| Ground-loss consequences, steering, charge, shield, stun | `blob_gameplay/shared` | Hill, race, royale, sandbox where declared |
| Current movement tuning | Match-owned validated value | Shared steering, command admission, snapshots, UI, bots |
| Hill trajectory selection | Existing hill movement seam | Tour and random-roam policies; scoring reads the resulting `Hill` |
| Tactical action selection | `blob_controllers`, proposed `tactical_controller` | Data-driven profiles and mode-objective providers |
| Input/feedback and world drawing | Existing browser simulation domain | One command path and existing camera projection |

Preserve the dependency graph in ADR 0002. Promote read-only geometry into simulation so gameplay
and controllers can share it; never introduce a controller-to-gameplay dependency. C++ rules and
the browser do not share executable geometry code across languages: they share validated geometry
data and golden conformance fixtures. Only the server decides collisions, falling, cooldowns,
and outcomes. No generic scripting engine, arbitrary component-patch API, or entity inheritance
hierarchy is needed.

## Terrain: ground, holes, and a road over void

Use one immutable, validated map-owned `TerrainDefinition`, not an entity per hole. Its first
closed shape vocabulary is deliberately small:

- A rectangular world envelope.
- Positive ground: either the whole rectangle, or a bounded collection of named polyline corridors
  (segment capsules with round ends).
- Negative ground: bounded circular holes. Holes subtract from all positive regions; overlapping
  positive regions form a union. A later shape adds one registered geometry implementation and
  schema branch, not a separate death or rendering system.

Hill maps normally use rectangle-minus-holes. Race maps use void by default and one named ground
corridor for the road; the road has one authoring owner in terrain. Race owns ordered checkpoints,
progress, return locations, and standings, and references the named corridor instead of authoring
another track. Map syntax must be explicit and strictly parsed, extending the existing loader and
section-family machinery, not hiding shapes in free-form marker metadata. Existing maps receive an
explicit solid-ground declaration in the coordinated input migration.

Canonical queries must provide point support, swept support intervals/first exit, nearest usable
ground, and disc clearance. Implement actual union/subtraction semantics: an internal seam where
two road capsules overlap is not a cliff. Reuse these answers for falling, spawn/return placement,
hill clearance, and bot planning. Do not independently reproduce the predicate in each consumer.

**Boundary convention:** retain race's supported closed road edge and existing named position
tolerance. Exact hole rims are supported; entering the hole interior beyond that tolerance is
void. Freeze the interval endpoint rules in geometry tests, including road bends, overlaps,
tangencies, and a segment which crosses a narrow hole and lands on ground again.

Players fall when their centres leave support; this is not a wall to bounce off and not vertical
gravity. A radius-aware clearance query is separately used for safe seating and hill motion.
Shield never protects against lack of ground, and charge never teleports or jumps over it.

Ground attachment is an explicit body capability. Players are ground-bound. Existing crossing
hazards retain their current non-ground-bound behavior by default, so race does not immediately
delete hazards spawned outside the road. An explicitly ground-bound dynamic hazard despawns on
support loss through the same event mechanism; it does not get a player respawn timer. Static
obstacles remain static; map validation rejects unintended unsupported placement. This distinction
allows a floating hazard and a rolling boulder without inventing separate physics engines.

Hill/race consume falling as their normal elimination/respawn flow. Royale retains elimination
until the next round. If a sandbox map opts into cliffs, it declares shared respawn with an explicit
delay rather than leaving eliminated players permanently bodyless. Respawn/checkpoint candidates
must be supported and unoccupied using actual body radii; correct the current common-player-radius
occupancy assumption while centralizing that query (`src/simulation/spawn_seating.cpp:15`).

## Motion foundation: fixed time, continuous events

Keep the fixed 400 Hz outer tick, one writer, transactional world/RNG updates, and immutable
publication. Replace the **discrete collision policy**, not the runtime clock. This explicitly
amends ADR 0003; do not describe it as behavior-preserving or regenerate replay hashes silently.

Preferred approach: swept broad-phase coverage and analytic disc/static/wall time of impact over
piecewise-linear within-tick motion. Apply acceleration/drag once at the specified outer-tick
stage, then solve motion events within that quantum. Do not introduce charge-only substeps,
variable wall-time steps, or a second "fast body" physics path. Ordinary substeps without a proven
minimum-feature/displacement bound do not guarantee thin-hole or collision safety.

The prototype must settle:

1. Initial penetration, touching/separating contacts, zero-time repeat suppression, corners and
   multi-body ties, and re-querying candidates after changed velocities.
2. A total event order using time then explicit event priority then canonical IDs. No epsilon
   comparator that violates sorting transitivity; tolerance bucketing/root rules need tests.
3. Motion termination at first support loss or lethal contact. A fallen/eliminated body must no
   longer hit other bodies later in the same tick. A post-kernel path check alone is insufficient.
4. Bounded candidate/event/path storage and work, validated operating limits, transactional failure
   with a named error on exhaustion, and measured cost. Never silently skip contacts, shorten
   displacement, restore an old plausible frame, or switch to discrete physics when a budget ends.
5. Real resolved path segments, including bounces, for shared terrain and ordered checkpoint
   crossings. A previous-position-to-final-position chord is not the path after a wall bounce.

Gameplay remains declarative: contact responses can request a narrowly typed per-body motion
termination and emit typed events; they cannot mutate arbitrary world stores inside the solver.
Add one narrow, pure `MotionTrigger` query/response seam for non-contact events on the current
motion segment. Shared support-loss and race's ordered gate crossings are its two concrete
consumers. A trigger reads immutable world/geometry and bounded tick-local cursor state, proposes
the next event time, and returns typed events, updated cursor state, and an optional motion
termination. It cannot commit arbitrary world changes. The solver re-queries after velocity or
cursor changes and orders these triggers together with contacts/walls. A race cursor advances
through its remaining gates and requests termination at finish; post-kernel systems commit the
already-ordered progress/standing facts rather than discovering finish too late to stop motion.
Support loss is a geometry event, with player elimination versus hazard despawn decided by shared
gameplay. The kernel knows capabilities and geometry, never concrete game modes. The prototype
must prove this trigger seam, including a finish immediately before a would-be later collision.

**Chronology:** at an exactly tied time, support loss precedes body/wall contact, and elimination
precedes checkpoint/finish credit. A gate crossed earlier counts; a gate after death does not.
Cross multiple ordered gates in one tick when the actual path reaches them in order, bounded by
the authored checkpoint count. Earlier finish wins over later finish, including within one tick;
equal event times share placement. Publish an explicit within-tick finish offset alongside the
tick so standings do not assert an invisible ordering. After finishing, terminate that racer's
remaining movement for the tick and release its movement/ability intents.

The exact solver and performance ceilings are a genuine unknown. The implementation plan stops
at a prototype review gate before adopting this architecture or building dependent combat on it.
If a smaller proven approach wins that review, amend this ADR and the later steps explicitly.

## Normal movement and web tuning

Store one `MovementTuning` value in match state, seeded from authored defaults per room and read
by shared steering. It survives round reset and returns to authored defaults when the room/process
is recreated; there is no browser-local authority or automatic INI rewrite.

The UI offers labelled numeric/range controls for acceleration (`wu/s²`) and **normal movement top
speed** (`wu/s`), with Apply, Reset, authoritative values, and a pending/applied indication. Normal
speed caps propulsion, not collision impulse: do not clamp the whole velocity after every bounce.
Above the normal ceiling, controls may brake/turn but must not add speed until back within it.
Charge and external knockback may exceed it and decay under drag. A separate validated physical
safety envelope bounds all speeds and event work; it is not an "unlimited speed" UI option.

Exact vector behavior at/above the ceiling must be a tested shared locomotion function, including
sideways steering, reverse input, zero acceleration, lowered limits, and overspeed from impacts.
Do not emulate the speed setting by changing drag. A provisional initial ceiling of 600 wu/s keeps
ordinary current mode defaults below it; fixture configurations remain explicit and calibration
may revise that value before approval.

Use one absolute-value `set_movement_tuning` command updating both scalars atomically. The server
stamps room/controller identity, validates finite ranges and the safety envelope, then the tick
applies canonical ordering. Concurrent senders have an explicit deterministic winner; Apply plus
Start in one batch must start with the accepted complete values. Publish a tuning revision and
effective tick; the UI must not claim an update took effect merely because it was sent.

Live room-wide tuning is confirmed. Extend the existing cooperative room authority to live
updates, with no invented host/admin identity. The proposed phase policy permits seated
participants to update settings in lobby, countdown, running, and ended; phase-specific body and
movement rules still apply. Invalid numbers reject at the boundary; stale, unauthorized, or
rate-limited commands return an explicit outcome instead of looking successful. Bots do not use
tuning commands as a tactic. Cross-room access and actor spoofing are rejected through the
existing admission boundary, not through disabled HTML buttons.

Use the same match value and command in every admitted phase. Commit running-match updates
atomically on a server tick and publish authoritative values, revision, and effective tick to
everyone in the room. Immediately recompute stored acceleration from persistent normalized
steering intent so held input picks up the new value without a key repress; this must not bypass
stun or other movement gates. Under the proposed normal-ceiling semantics, lowering the limit
must not delete knockback momentum. Coalesce local slider edits behind Apply and bound update
rate; Reset submits authored defaults through the same atomic command. Do not add a second
live-only settings service. No setting grants one player a private acceleration advantage.

## Continuously roaming hill

Extend the existing hill motion policy with `marker_tour` and `random_roam`. Keep tour available
for authored courses and deterministic fixtures. Both write the same published `Hill` circle;
scoring and rendering do not branch on how it moved.

Random roam has positive minimum/maximum speed and bounded direction/speed retarget intervals.
Provisional values: 20–70 wu/s, a new target every 0.35–1.2 seconds. It moves every running tick,
holds a selected velocity between retargets, and reflects through shared clearance geometry at
legal boundaries. There is no dwell, teleport, per-frame browser randomness, or zero-speed stall.
The hill disc must remain over reachable ground with authored clearance; reject impossible roam
regions at startup. Test corner and narrow-region behavior, rather than repeatedly guessing new
headings until one happens to fit.

Store velocity, random stream state/identity, and next-retarget tick in committed world state.
Reuse the canonical deterministic generator and written draw order, without platform-dependent
standard distributions/trigonometric direction generation. Derive independent stable streams for
hill motion and hazard spawning so adding a hill draw does not reshuffle hazard behavior. Preserve
zero extra draws for the tour policy. Publish only current public motion, not future random
targets/RNG state that would give bots privileged predictions.

Lobby/countdown holds the initial position; running moves; ended freezes. Explicitly reset the
non-participant hill's motion state for a new round. Advance motion before scoring so points,
published geometry, and the browser agree about the current hill.

## Charge, shield, and stun

All are shared gameplay mechanics declared by modes. Humans, bots, and replays use the same
registered commands, ownership checks, cooldowns, and outcomes.

| Mode / state | Charge and shield | Fall consequence |
|---|---|---|
| Hill, running with a body | Enabled | Existing timed return; score retained |
| Race, running with a body and not finished | Enabled | Existing checkpoint/grid return |
| Royale, running with a body | Enabled | Eliminated until next round |
| Sandbox, running with a body | Enabled | Shared configured return if cliffs are authored |
| Lobby, countdown, ended, bodyless, stunned, or finished | No new activation | Existing mode lifecycle |

This table governs abilities, not a silent change to current lobby steering. Advertise command
kinds only once authoritative handlers exist; phase-specific eligibility is explicit published
state and is rechecked by the server, not inferred from the presence of a button.

### Charge

`charge(direction)` is a one-shot activation, not a held acceleration buff. Normalize/validate the
direction once at admission, reject a zero direction, and apply an instantaneous additive velocity
burst in the accepted direction. Initial gain: 0.75 times the current normal movement ceiling;
initial cooldown: 1.2 seconds. Both are authored tuning, not browser constants. The safety envelope
must cover combined current velocity and burst; refuse an inadmissible activation explicitly, not
silently convert it to a different move.

The browser supplies current steering direction or the last nonzero aim direction; before either
exists, Charge is unavailable with an explanation. Charge does not teleport, grant invulnerability,
erase existing lateral velocity, or bypass cliffs. It produces ordinary collision interactions,
which is why continuous motion precedes implementation of the move.

### Timed shield and perfect opening

Recommended first version: a tap activates 400 ms of shield; the first 80 ms is the perfect window;
activation cooldown is 900 ms. At 400 Hz these are 160, 32, and 360 ticks. Windows are half-open:
`[activation_tick, activation_tick + duration_ticks)`. Cooldown starts on activation. These numbers
are hypotheses to tune with real latency, not a reproduction of Smash's exact mechanics.

- Normal shield blocks lethal-contact elimination and targets 25% of the ordinary received
  collision impulse. Every guarded response must also leave the pair non-closing. If the nominal
  reduction conflicts with separation (notably two ordinary shields moving toward one another),
  the pair-level dissipative separation correction takes precedence over the exact percentage.
  Do not independently scale two velocity deltas and assume the pair remains valid. The prototype
  must pin the formula, mass/static behavior, energy behavior, and dual-shield golden cases.
  Shield is not immunity to cliffs or zone rules.
- Perfect shield adds a 600 ms / 240 tick stun to the incoming **dynamic** body, including a
  bounceable hazard. Static walls cannot be stunned. Require actual incoming motion; ramming a
  stationary target with a shield does not manufacture a defensive perfect-shield bonus.
- The perfect benefit applies to all qualifying impacts while its short window is open. No
  first-hit consumption is implied. Both sides are evaluated symmetrically; simultaneous eligible
  perfect shields cause mutual stun, not an EntityId-dependent winner.
- A perfect response zeroes the stunned source's velocity/acceleration immediately. It must also
  establish a non-closing contact result: a moving defender cannot keep driving into the now-stopped
  body and generate infinite zero-time impacts. Pin that separation response in the prototype.
- Charge and shield are mutually exclusive activations. Evaluate eligibility first: if both are
  eligible in one entity/tick, shield wins and charge is refused without consuming charge cooldown.
  An unavailable shield pulse does not suppress an otherwise eligible charge. Charge is unavailable
  while shield is active. This keeps the initial shield defensive and prevents stacking the moves.

Do not implement every flag combination as a contact-rule row. Use one pair-symmetric composition:
immutable pair facts → existing base physical equation → defense modifications → typed effects
and motion disposition. Reuse mass/restitution/static equations. Preserve the existing unguarded
lethal-hazard pass-through response for the surviving hazard; the eliminated player leaves the
remaining event stream. Guarded lethal contact instead takes the shared defensive response.

### Stun and input lifecycle

Stun is a reusable temporary control lock on a dynamic entity, not a bot-only flag or an `is_static`
hack. It immediately kills momentum at the triggering impact; subsequent external impulses may
still move the stunned body. It blocks steering and fresh charge/shield activations until expiry,
never restores old velocity, and merges repeated stun requests by maximum expiry, not addition.

Defense state is fixed for one outer tick; post-kernel status application clears shield and input
intent for following ticks. Thus a body parried earlier may still shield another contact in the
same 2.5 ms tick. This bounded grace is intentional; changing it would require contact-time status
mutation and a new contract, not a hidden implementation choice. Duration starts at the committed
impact tick. The no-input interval and exact expiry tick must be tested.

Current hazards have zero acceleration/drag and an assigned initial velocity, not an ongoing
driver (`src/gameplay/shared/hazard_spawn_system.cpp:83`). After stun they stay stopped until bumped;
their lifetime keeps counting down to preserve existing population bounds. Future driven objects
must consult the same lock before reapplying propulsion.

Persist normalized steering intent separately from acceleration. Stun clears that intent; ignore
commands received during stun rather than queueing an action for expiry. New activations require a
fresh press. A continuously held movement key may resume only through a newly accepted steering
update after expiry. Clear body-bound states on elimination; reset abilities to ready on respawn
for the first version, and on round restart. Test zero-delay respawn explicitly.

The mailbox coalesces same-kind inputs, so multiple same-tick activation pulses mean at most one
attempt, not queued charges. Key repeat cannot reactivate; blur/disconnect/body loss clears local
pressed/aim state. Proposed bindings are Space for charge, Shift for shield, alongside ordinary
screen buttons. Input typing and camera controls must not accidentally send abilities or thrust.
Perfect timing is judged by the server-accepted tick, not client-provided timestamps; prediction,
rewind, and latency compensation are explicitly deferred.

## Tactical personalities without a class per mood

Add one `TacticalController` with data-driven profiles. Its reusable decision path is published
observation → objective candidates → safety screening → utility selection → steering/actions.
Named objective providers cover holding/intercepting a hill, advancing race gates, and surviving
royale. They produce the same tactical candidate shape; they do not fork contact or terrain logic.

First profiles:

- **Keeper:** capture/intercept the hill, brake relative to its motion, defend a stable interior.
- **Bully:** approach the safe side of a target, line up a shove toward a cliff/hazard or out of the
  hill, charge only with an acceptable recovery path.
- **Opportunist:** take openings, prefer distracted/exposed targets, abandon low-value fights.
- **Cautious Racer:** preserve road clearance and checkpoints, use bursts when aligned, avoid
  expensive fights; not a different race physics implementation.

Profiles configure objective weights, aggression, risk tolerance, target persistence, reaction
delay, prediction horizon, aim error, charge appetite, and shield timing error. A controller kind
names an algorithm; a profile names its parameters. Extend the existing strict section-family
parser for `[bot_profile.<name>]`, then propagate the selected profile through seat declaration,
factory/reconciliation, snapshot metadata, and lobby selection. Unknown profiles fail visibly.
Keep simple wanderer/chaser/seeker/racer kinds as diagnostic options; factor shared body lookup,
geometry, and steering when touched, instead of leaving duplicate canonical helpers.

Tactics need safe approach, contact setup, predicted victim direction, and an escape corridor.
Use actual masses, velocities, terrain, current hill motion, and ability availability from public
state. Reject suicidal candidate paths before utility ranking; a weighted sum of "seek target"
and "avoid cliff" alone can cancel into unsafe behavior. Include hysteresis and target-loss handling
to avoid frame-by-frame switching. Debug output records selected tactic and bounded reason codes,
not an opaque claim that the bot is "smart."

Bots do not see future random hill choices, private commands, or intermediate ticks. Continue at
presentation cadence; measure reactions in observed ticks and make duplicate observations
idempotent. Perfect-shield anticipation uses visible trajectories plus profile reaction/error,
not a collision callback available only to bots. Derive stable distinct seeds from match/room,
seat, profile, and round identity rather than giving every bot the same seed as today
(`src/application/bot_reconciliation.cpp:110`). Replay still replays accepted commands, not the AI.

## Wire ownership and migration

Recommend a coordinated **session protocol v3** because moving the race road out of mode state
removes/reinterprets v2 fields, which is a major change under `docs/protocol/v2.md:701`.
Publish shared terrain once per complete snapshot representation, movement tuning/revision, public
ability/status state, and profile identity/catalogue where the lobby needs it. Race retains only
its objective/progress/standings information and a reference to the canonical terrain corridor.

Publish from the immutable map/match values directly, not one terrain publisher per mode. Keep
complete validated snapshots; do not introduce delta-stream state or viewport filtering here.
Apply explicit shape/point/profile/state-count budgets and prove maximum encoded frames remain
within the existing 2 MiB limit. Never truncate geometry, effects, or entities to fit a frame.

Use the v3 route and WebSocket subprotocol, generated schemas/types, and one active encoder/decoder
implementation. Public v1 configuration need not become mutable or be versioned for room tuning.
Historical v2 schemas/evidence remain labelled historical, not a second live implementation.
Old session requests fail with an explicit upgrade-required response; clients never guess or
silently fall back to another major. Development commits are not independently deployable releases
of the final v3 schema. Release server and assets together only after the whole contract is verified.

The buildable cutover checkpoint includes the foundational terrain value and strict authoring,
race binding, snapshot construction, and **all current road readers**: gameplay, racer controller,
and browser rendering. Remove old road fields only in that same checkpoint. Subsequent terrain
work extends shared queries/validation; subsequent UI work adds cliff/combat presentation, not a
belated repair of a broken road reader. New schema vocabulary may be reserved during unreleased
development, but do not advertise/accept an ability or tuning command until its authoritative
implementation and complete client/schema handling land together.

Alternative: v2.6 adds shared terrain and retains old race track fields as derived mirrors, with
equality tests and a named `_migration` adapter/removal plan. This is a valid transitional option
if preserving an incremental protocol rollout is preferred, but not the recommended permanent
architecture. No state may be independently authored or simulated in both places.

## Acceptance and limits

Automated acceptance must include:

- Same seed/input produces identical hill motion and outcomes; separate random streams; failed
  ticks roll back motion and RNG; no unreachable hill, stalls, reset leakage, or score/render drift.
- Swept player/player, player/static and hazard contact; zero-time/corner piles; bounded-work
  rejection; no collision or gate after falling; chronological multiple gates and finish ties.
- Solid floor, overlapping road capsules, holes, edge tolerances, actual bounced paths, safe
  spawning, hill/race return, and royale's distinct elimination policy.
- Charge direction/cooldown, normal-speed versus knockback, all shield timing boundaries, lethal
  and non-lethal contacts, mutual perfects, moving defenders, and dynamic hazard stun/expiry.
- Stun input rejection, no queued activation or restored momentum, blur/key-repeat/disconnect,
  bodyless/zero-delay return, repeated requests, and human/bot command symmetry.
- Two clients agree on tuning, terrain, abilities, and elimination; another room is unchanged.
  UI editing/camera movement sends no unintended gameplay command. Manual/follow/DPR stay correct.
- Profiles demonstrably choose different tactics against the same public observation; safe-side
  shove, threat avoidance, reaction limits, distinct streams, target loss, and debug explanations.
- Parser/schema mutations, command ownership/phase/rate bounds, maximal payloads, replay hashes,
  native timing/capacity benchmarks, full C++/web/browser gates, and bounded fuzz campaigns.

The native performance/release workflow is authoritative; Mac-hosted Linux/amd64 runs are advisory.
Human playtesting separately judges hill readability, whether a shove feels earned, whether parry
timing is fair over the actual connection, and whether the profiles are distinguishable and fun.
Do not substitute automated wins for those judgments.

Not included: 3D terrain/gravity, jumps, held-shield stamina, ranked matchmaking, host/account roles,
LLM/RL bots, arbitrary polygon/CSG editors, rollback networking, zoom/minimap, live deployment,
or completion of the previously pending live race playtest.

## Extensibility checks

- A stationary objective or a moving capture zone can reuse the motion policy without changing
  scoring, the kernel, or renderers.
- A new terrain primitive extends one validated geometry vocabulary; death, spawning, bots, and
  camera projection continue consuming the same contracts.
- Another stun source uses the same status/input-lock lifecycle. A different bot temperament is
  profile data; a genuinely new tactic is a candidate provider, not another physics model.
- Live tuning, reset, and future settings controls use the same match value and command;
  settings never gain a second source of truth in the browser or process-global configuration.

These checks justify the seams; they are not instructions to build the hypothetical features now.

## Planning review, 2026-09-10

Independent architecture review identified three corrected proposal defects: immediate finish
termination needed a chronological trigger seam; ordinary shields needed the same non-closing
postcondition as perfect shields; and the v3 cutover needed canonical terrain plus all existing
readers in one buildable checkpoint. The proposal now names each contract explicitly, adds the
ability mode/phase matrix, and gives priority only to an eligible simultaneous shield request.
This design review is not implementation, benchmark, or owner-approval evidence.
The reviewer rechecked the revisions and found no remaining must-fix issue in that scope; exact
solver behavior and shield correction remain unproven until the explicit prototype gate.

**Amended 2026-09-10 (owner clarification):** Numeric acceleration/top-speed controls and live
room-wide application are confirmed. Require atomic tick updates, shared revision/effective-tick
publication, and immediate held-intent recomputation without bypassing movement gates. Remaining
gameplay, authority/phase, UI interaction, and physics proposals still await review.
