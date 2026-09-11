<!-- canonical: dynamic_arenas_and_combat -- accepted design for shared terrain, motion, abilities, tuning, and tactical personalities -->

# 8. Dynamic arenas, player abilities, and tactical personalities

* **Status:** Accepted 2026-09-10 at plan Step 1; not implemented, and the solver remains unproven until the plan's Step 5 prototype gate
* **Date:** 2026-09-10
* **Baseline:** `b282a90`
* **Decider:** Project owner

## Intent

Make hill movement unpredictable, make opponents tactically interesting, and make positioning
matter through cliffs, charge, and a timing-sensitive shield. Build these from shared capabilities,
not parallel hill/race physics, a class per bot mood, or client-only rule changes.

The camera follow-up is implemented locally at the baseline. This design does not imply its
deployment, completion of human playtesting, or authorization to change the live hill server.
The executable sequence is
`.claude/plans/2026-09-10-dynamic-arenas-and-combat.md`.

## Owner decisions

On 2026-09-10, the owner confirmed adjustable numeric acceleration/top-speed controls and live,
room-wide tuning, and later that day accepted the remaining first-version choices below and the
four design-review decisions at plan Step 1 (§ "Design review and acceptance, 2026-09-10").
Acceptance permits initial implementation through the plan's prototype gate; it is not a claim
that the timings are balanced or that the solver is proven.

| Choice | Decision / recommendation | Alternative / consequence |
|---|---|---|
| Movement controls | **Confirmed:** adjustable numeric acceleration and top-speed controls | **Accepted 2026-09-10:** normal propulsion ceiling semantics, Apply/Reset, and finite safety bounds; no on/off-only or unlimited-speed setting. |
| When tuning applies | **Confirmed:** live, room-wide, applied atomically on an authoritative server tick | Publish the same revision/effective tick to everyone in the room; do not defer running-match changes until the next match. |
| Shield interaction | **Accepted 2026-09-10:** tap to activate a short timed shield | A held shield needs hold/release intent and stamina/recharge policy; leave this out of the first version unless requested. |
| Perfect-shield benefit | **Accepted 2026-09-10:** zero the incoming dynamic object's momentum and apply a temporary input lock | Not a frozen/static object: other impacts may still move it. |
| Falling | **Accepted 2026-09-10:** a player's centre entering void causes elimination; charge does not jump | A body-radius fall threshold or jumping is a different mechanic. |
| Session protocol | **Accepted 2026-09-10:** a coordinated v3 session contract, cut over by expand and contract | v2.6 can retain derived duplicate road fields temporarily, but needs a removal migration. Do not keep two permanent road representations. |

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
section-family convention, not hiding shapes in free-form marker metadata. **Refined at Step 3,
2026-09-10:** existing non-circuit maps receive an explicit solid-ground declaration; the circuit
declares corridor-only ground. The strict map parser is extended directly because the existing
server-configuration family machinery is private, not a reusable loader API.

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
**Decided at Step 1 on 2026-09-10:** a response also reads the committed world, exactly as a
predicate already may, so defense and lethal state reach one composition without a row per flag
combination; it still writes only the two bodies (§ "Design review and acceptance, 2026-09-10").
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

Store one `MovementTuning` value in match state, seeded from the authored defaults of one shared
`[movement]` section that replaces the per-mode thrust keys (decided at Step 1 on 2026-09-10),
and read by shared steering. It survives round reset and returns to authored defaults when the
room/process is recreated; there is no browser-local authority or automatic INI rewrite.

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
updates, with no invented host/admin identity. The phase policy, accepted at Step 1, permits seated
participants to update settings in lobby, countdown, running, and ended; phase-specific body and
movement rules still apply. Invalid numbers reject at the boundary; stale, unauthorized, or
rate-limited commands return an explicit outcome instead of looking successful. Bots do not use
tuning commands as a tactic. Cross-room access and actor spoofing are rejected through the
existing admission boundary, not through disabled HTML buttons.

Use the same match value and command in every admitted phase. Commit running-match updates
atomically on a server tick and publish authoritative values, revision, and effective tick to
everyone in the room. Immediately recompute stored acceleration from persistent normalized
steering intent so held input picks up the new value without a key repress; this must not bypass
stun or other movement gates. Under the accepted normal-ceiling semantics, lowering the limit
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
remaining event stream. Guarded lethal contact instead takes the shared defensive response. The
composition is the one response for every dynamic pair, and the unguarded pass-through is its
lethal branch rather than a separate row (decided at Step 1 on 2026-09-10).

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
Publish shared terrain once per session in `welcome`, and let the snapshot value carry a shared
immutable reference to the same terrain for in-process readers that the frame encoder does not
serialize (decided at Step 1 on 2026-09-10). Publish movement tuning/revision, public
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

The cutover is **expand and contract** across buildable commits (decided at Step 1 on 2026-09-10,
refining the planning review's single checkpoint): the terrain value and its queries land with no
consumer; race binds to the named corridor while its old road fields are stamped as a derived,
equality-tested mirror; v3 transport and the browser terrain layer land while that mirror is on
the wire for exactly one step; then the mirror, the racer controller's private geometry, and the
old fields are removed together. No state is independently authored or simulated in two places at
any time, no broken road reader exists at any checkpoint, and no released v3 carries the mirror.
Subsequent terrain work extends shared queries/validation; subsequent UI work adds cliff/combat
presentation. No schema vocabulary is reserved ahead of behavior: each kind, block, member, and
command lands under v3 in the commit whose authoritative implementation and complete
client/schema handling it carries.

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
solver behavior and shield correction remain unproven until the explicit prototype gate. The
single cutover checkpoint this review asked for was refined into expand and contract at Step 1;
see § "Design review and acceptance, 2026-09-10".

**Amended 2026-09-10 (owner clarification):** Numeric acceleration/top-speed controls and live
room-wide application are confirmed. Require atomic tick updates, shared revision/effective-tick
publication, and immediate held-intent recomputation without bypassing movement gates. Remaining
gameplay, authority/phase, UI interaction, and physics proposals still await review.

## Design review and acceptance, 2026-09-10 (plan Step 1)

A second independent design review, run against the tree at `b282a90` with code quality, DRY,
and extensibility as its lenses, found the ownership model sound and the step cut unsafe. Its
findings were folded into the plan on 2026-09-10 and are summarized here so this contract and its
executable sequence agree:

- The one-commit terrain cutover could not be reviewed or bisected. It became expand and contract
  behind a pure terrain step and a shared swept-geometry root module with one event-time order,
  because contacts and terrain triggers must derive tied times from one arithmetic.
- The pair composition needed a kernel seam the design had not named: a response cannot see a
  shield, stun, or lethal component today (`src/simulation/contact_rule.hpp`), so responses gain
  committed-world read access and a per-body motion disposition, proven at the prototype and wired
  with the solver.
- Body-bound cleanup was one hand-written loop per owner and had already produced `c84e2fb`; a
  per-kind lifetime trait with one generic sweep replaces it.
- Four ability timers become one half-open tick-window value stored as absolute ticks.
- Movement tuning seeded from three per-mode thrust keys would be a second source of truth; one
  shared `[movement]` section replaces them.
- Named random streams and per-stream draw counts are world and wire changes and became a step.
- Terrain never changes during a session and travels once in `welcome`.

**Owner decisions, 2026-09-10.** The owner instructed execution of plan Step 1 with the review's
recommended answers, which records the following. The first-version proposals of § "Owner
decisions" are accepted: the timed tap shield with its half-open windows; the normal propulsion
ceiling that caps propulsion and never collision impulse, beside a separate safety envelope;
centre-based falls; the initial timings as playtest hypotheses; cooperative authority with the
proposed phase admission and no host role; Apply/Reset with authoritative pending, applied, and
rejected state; and a coordinated session v3. The four review decisions are resolved as
recommended:

| Decision | Resolution | Refines |
|---|---|---|
| (a) Contact response inputs | A response reads the committed world as a predicate may, and `ContactResponse` carries a per-body motion disposition; it still writes only the two bodies. Not defense state on `PhysicsBody`. | § "Motion foundation" and § "Charge, shield, and stun" here; ADR 0004 § "Contact rules" at plan Step 16 |
| (b) Terrain on the wire | Once per session in `welcome`, with a shared immutable reference on the snapshot value for in-process readers; not per frame. | § "Wire ownership and migration" at plan Step 7 |
| (c) Movement authoring | One shared `[movement]` section authors acceleration and normal top speed, and the per-mode thrust keys retire; not per-mode defaults. | § "Normal movement and web tuning" here; ADR 0005 § "Mode configuration" at plan Step 10 |
| (d) Cutover shape | Expand and contract with a one-step derived, equality-tested mirror; not one atomic checkpoint. | § "Wire ownership and migration" at plan Steps 6 through 8 |

**Below the contract.** The plan fixes engineering choices this ADR does not constrain, which an
execution note may revise without reopening it: one swept-geometry root module and one event-time
comparator; a tick-window value for every ability timer; a body-bound component lifetime trait
consumed generically by shared respawn; a closed list of named random streams seeded from the
match seed, with the hazard stream bit-identical to today's generator; and steering intent
persisted on `Controllable` and stripped at publication.

**Still unproven.** The swept solver, its event order under ties, the shield separation
correction, and native performance ceilings are proven only by the plan's Step 4 prototype and
accepted only at its Step 5 gate; Phase C of the plan is provisional until then. The timings are
hypotheses for human playtesting. The older contracts this design changes carry dated pointers as
of today and are amended in the same commit as the step that lands each change: ADR 0003
§ "Canonical tick" and § "State, units, and fixed time"; ADR 0004 § "The tick", § "Contact
rules", § "Determinism obligations for framework code", and § "Snapshots and protocol shape";
ADR 0005 § "Mode configuration" and § "Steering"; ADR 0007 § "The course", § "Mode state and the
wire", § "Out of bounds, and returning to a checkpoint", § "King of the hill", and § "Bots". No
source file changed at Step 1.

## Swept geometry contract, 2026-09-10 (plan Step 2)

`src/simulation/swept_geometry.{hpp,cpp}` owns analytic boundary roots for circle, capsule,
and axis-line sweeps. It returns all distinct boundary times in `[0, 1]`, so the same
arithmetic serves entry, exit, tangency, and later chronological triggers. Disc contact
expands the obstacle radius explicitly; interior containment uses an explicitly eroded radius.
Initial overlap is a separate predicate, not an invented boundary root or an impulse.

`MotionTime` in `motion_event_order.hpp` retains each computed binary64 root exactly,
normalizing only negative zero. Calculations use their written operation order under
round-to-nearest/ties-to-even and `-ffp-contract=off`; no time quantization, epsilon equality,
or endpoint snapping is permitted. Equal stored times do not assert equality of exact real
roots. A motion continuously on a boundary contributes its interval endpoints.

Circle polynomial signs use bounded floating expansions after exact power-of-two normalization.
Explicit `std::fma` computes product residuals; this is written compensated arithmetic, not
implicit compiler contraction or extended precision. Every nonzero normalized input length must
be at least `2^-200`, keeping degree-four residual bits representable. Wider scale ratios and
unrepresentable distinct root separation fail visibly instead of returning a tangent or miss.
Exact polynomial factors at zero and one retain those endpoints without a proximity snap.
`map_motion_time` owns local-root conversion to a remaining tick interval, preserving exact
endpoints and otherwise computing `begin + ((end - begin) * local)` in that written order.

The total event key is time, then explicit priority, then canonical identity. Priorities are
support loss, body contact, x wall, y wall, and checkpoint. Body pairs use ascending entity
ids; boundaries and triggers use the body id and stable authored feature index. This retains
pair-before-wall and x-before-y precedence at exact ties and puts termination before progress.
This is a pure foundation, not adoption by the live kernel: ADR 0003's accepted tick and every
accepted fixture remain unchanged. Step 4 reviews these choices; Step 5 still gates adoption.

## Terrain foundation, 2026-09-10 (plan Step 3)

`TerrainDefinition` is the one map-owned immutable value. A shared immutable storage allocation
holds both its authored fields and its derived line/arc boundary cache; equality compares authored
fields, and copying cannot separate content from cache. `ArenaBounds` is a pure module promotion
with unchanged arithmetic and diagnostics. `MapDefinition::bounds()` references terrain's envelope.

Point support is the closed envelope intersected with the positive union, subtracting open hole
interiors. The effective road radius is `half_width + kPositionTolerance`; a hole's open radius
is `max(0, radius - kPositionTolerance)`. Swept queries use the Step 2 roots and closed interval
algebra, retaining singleton ground. A first exit names the supported boundary limit when outgoing
motion becomes unsupported: starting on a rim and entering void exits at zero; ending on a rim
at one does not exit. No temporal midpoint is required between nearly equal roots.

`nearest_supported_point` is radius-zero recovery, returning the supplied point when supported
and absence for empty ground. `disc_clearance` separately measures the distance to the Boolean
terrain's exposed boundary, and `terrain_supports_disc` combines center support with that distance.
An overlap's internal seam is not a boundary. The derived cache is compiled once analytically,
without rasterization, angular probing, or a second root solver; inconsistent or unrepresentable
arrangements fail with a named terrain validation error rather than publishing partial geometry.

Boundary selection is analytic, but the returned witness is a supported binary64 coordinate.
Translation can round a circle projection into void. Only after selecting the nearest feature,
at most four coordinatewise `nextafter` corrections may move it toward the supported query point
for clearance, or toward the cached supported side for interior-curve recovery. Recovery at an
endpoint instead uses a symbolically compiled Boolean supported angular sector: one incident
curve's normal can point into another hole. Four bounded ray targets preserve and validate the
actual representable displacement's sector before testing canonical point support. This is not a
complete search of nearby binary64 points or alternative sectors; exhausting the selected ray's
budget fails visibly. The feature identity is retained, and a nearer feature is never skipped in
favor of a farther one. Rim-only geometry must itself be representable. Reported distance is to
the corrected witness; clearance correction cannot increase that computed distance. This is a
bounded representable-witness policy, not an interval-certified exact-real error bound for the
preceding root, projection, or square-root arithmetic, and does not change event-time rounding.

Initial authoring bounds are 8 corridors, 32 total segments, 40 total points, and 32 holes. The
retained boundary has at most 8,192 elements and temporary arrangement storage at most 60,000.
Temporary accounting charges deterministic logical slots, including retained pre-deduplication
slots, not standard-library-defined vector capacity or allocator overhead.
These are explicit work/storage guards, not a performance certification; Step 4 measures them and
Step 5 retains the native-evidence gate. The new production-map fuzz harness exercises these input
boundaries. Race's distance loop is duplicated only for the staged, bit-identical promotion proof;
Step 6 removes the old implementation by delegation, before any new publication owner appears.

## Continuous-motion prototype boundary, 2026-09-10 (plan Step 4)

This section records the **unwired prototype proposal**, not Step 5 acceptance or a change to
ADR 0003's live tick. The original contact detectors remain on the live path. Their impulse
arithmetic is promoted verbatim to contact-taking helpers only after independent old/new bit
comparisons pass on both lanes; the old radius-taking wrappers retain their detection semantics.
Static reflection follows the same proof-before-delegation sequence. A swept hit constructs its
contact from its geometry certificate, with written square-root arithmetic, without re-entering
legacy detection and possibly rejecting a rounded time of impact.

The circle owner also returns initial inside/on/outside membership and stationary/miss/tangent/
secant topology of the supporting infinite line, derived from its existing exact expansion facts
after the canonical written center-offset subtraction. These are distinct from clipped roots:
one retained root can be either a tangent or one end of a secant. Detailed-query/root-wrapper
equivalence must pass on both lanes before delegation. Exact tangencies are rejected as impacts
before rounded relative-velocity admission; a large-speed tangency can otherwise acquire a false
closing residual. No second discriminant solver or changed root arithmetic is introduced.
It separately exposes the initial radial direction (approaching/orthogonal/receding) from the
same exact dot-product coefficient, and exact coincidence from the canonical offset components.
Initial contact and velocity-revised retained certificates require approaching radial motion
unless centers coincide exactly. Distinct centers use their geometric normal even below the old
position tolerance; only exact coincidence uses the deterministic velocity/axis fallback. Legacy
detectors and promoted impulse equations keep their existing behavior.

One pure, typed continuous-motion driver receives already accelerated/dragged bodies, immutable
committed world/context, and borrowed immutable policy facts. It returns bodies, dispositions,
actual path segments, ordered typed consequences, and work counters, or throws without publishing
partial results. Responses can change velocity/acceleration and terminate their participating
bodies; geometry/filter changes and teleportation are invalid prototype responses. The existing
`ContactResponse`, mode declarations, component registry, event registry, and live kernel are not
adopted or extended here. Step 16 uses this same driver and disposition type; Step 18 adapts the
same guarded-pair core instead of implementing another composition.

Motion anchors change only when that body's velocity changes or its motion terminates. Pair
roots use the fixed common epoch beginning at the later of the two anchors; body/wall roots use
the body's anchor. Advancing global time filters certificates rather than reparameterizing them.
This preserves untouched endpoint and future-root bits when an unrelated event is inserted.
Swept broad-phase coverage belongs to these trajectories, never the committed-position index.

The Step 2 event key selects the minimum currently eligible event. Event times never decrease;
causally enabled events at the same time are reconsidered under that same priority order, rather
than pretending their priority could act before their cause. A current-time geometric certificate
survives a velocity-only response (notably a gate touch after a nonlethal bounce); termination
removes the body and all later consequences. Future certificates are invalidated by motion changes.
When a retained contact's generating velocity revisions differ, the same circle owner classifies
current radial motion along a full-quantum reference line, including at tick end. That directional
veto cannot revoke certified geometry using newly rounded membership, line topology, or future
roots. The original time, normal, and distance stay fixed. The selected-certificate diagnostic
trace may contain a subsequently declined contact or wall; only returned typed effects are
gameplay consequences.

Contact callbacks retain the live kernel's closing-speed predicate. Closing initial overlap is a
time-zero event, without depenetration. Body membership uses the canonical circle polynomial at
the sum of effective radii, not the legacy detector's initial proximity band. The existing gate
endpoint predicate remains the written square-root distance against authored radius plus
`kPositionTolerance`; the canonical gate helper applies that spatial expansion exactly once.
Stationary/separating overlaps and exact tangencies have no
contact, guard, or lethal consequence in this proposal. Delivering every closed touch is an
explicit alternative for Step 5, not an accidentally preserved baseline. After a pair response,
repeat suppression records both **post-response** motion revisions; its own response cannot
immediately retrigger it. Another contact changing either trajectory re-enables the pair. Time
advance and acceleration-only changes do not change current-motion revisions.

At an epoch beginning exactly at tick end, zero remaining displacement does not mean zero
relative velocity: an impact may enable another touching pair before a tied finish. That branch
passes relative velocity times one canonical fixed delta to the same detailed circle query solely
as reference-line geometry. Only initial inside/on-boundary membership, non-tangent/non-stationary
topology, and closing speed admit an event at the existing end time. Outside reference roots are
never mapped or traveled; no part of the next tick is simulated.

A folding body's center must initially lie in the envelope and its radius must fit the arena;
initial radius overlap with a wall is allowed. Outward motion at or beyond the inset wall plane
reflects immediately, while inward overlap may leave without relocation. Static geometry outside
the envelope and folding centers outside it are invalid inputs. A consumed tied wall certificate
must refresh the axis even when an intervening pair response made that old wall non-closing, so
the next opposite-wall event is not lost.

Pure motion triggers have stable identity and bounded tick-local cursor/eligibility state.
Support loss uses the canonical terrain query and bypasses guard state. Ordered gate occupancy
uses Step 2 roots and a stable eligibility boundary; overlapping next gates may advance at the
preceding gate's exact time, and finish terminates immediately. A trigger must advance its cursor,
change current motion, or terminate. No trigger receives arbitrary mutable world access.

The guarded-pair core reads committed player/lethal/phase facts and explicit frozen per-side
guard facts. The unguarded lethal branch preserves both bodies and terminates its victim(s).
Otherwise it selects the existing base equation, quarters each guarded side's received velocity
delta, applies symmetric perfect eligibility from pre-response incoming world-frame motion, and
zeros newly stunned dynamic sources' velocity and acceleration. Its separation projection uses
inverse-mass weights, with zero weight for static and newly stopped sources for that correction
only. A later external impact may still move a stopped body. Static geometry has zero motion
velocity even if a stored static body value carries nonzero velocity; its stored fields remain
unchanged by integration.

When neither side has a guard, the base equation's output is returned unchanged, including its
binary64 restitution-zero residue; no defense projection modifies an unguarded contact. The
separation correction applies only to the guarded composition that changed the base response.

The quartered response is an **external-impulse gameplay policy**, not globally energy conserving:
unit-mass collinear velocities `(10, 9)` become `(9.75, 10)` when the first side shields, increasing
the squared-speed sum from `181` to `195.0625` despite separation. Only the subsequent non-closing
projection is intended to be dissipative relative to its input. A global energy cap would require
a different tradeoff against the quarter target and remains an owner choice at Step 5.

The named prototype ceilings in `simulation_limits.hpp` bound bodies, candidate examinations,
root/trigger queries, selected events, cursor/declaration storage, paths, and returned effects.
Exhaustion is a named failure, never truncated travel, skipped contacts, discrete fallback, or a
partial committed frame. Verification, measurements, and the required specialist review are
recorded with Step 4 completion; native capacity and adoption remain unaccepted until Step 5.

Swept broad-phase endpoints are scalar geometric bounds, not prematurely constructed final body
values: an early collision/termination may make an otherwise out-of-range hypothetical endpoint
irrelevant. Terrain/gate helper inputs retain their existing stricter whole-sweep coordinate
domain, however. If `start + displacement` lies beyond the representable `Vector2` component
envelope, those helpers visibly reject even when an earlier trigger could have ended travel.
This is an explicit prototype numeric-admissibility limitation to settle before live adoption,
not a hidden discrete fallback or permission to clip the query.

## Owner clarification during Step 5 review, 2026-09-10

This section records the owner's subsequent requirements and takes precedence over conflicting
first-version proposals above. It does not retroactively change the scope or results of the
completed Step 4 prototype. The plan adds a supplemental unwired Step 4a proof; Step 5 remains
unticked, and no Phase C implementation is authorized by this clarification alone.

### Per-object effect eligibility

~~All contact, guard, and lethal consequences use the prototype's global closing-only admission~~
is not the desired gameplay contract. An individual object can require incoming impact or allow
any geometric touch, including an exact graze or stationary initial contact/overlap. Archetype
defaults may be useful authoring, but they must not prevent different effective policies on
individual objects. The exact canonical authoring/committed-value projection must be documented
in Step 4a's supplemental review and land with its authoritative behavior and wire schema at
Step 16; no field or registry arm is added in this documentation amendment.

Effect eligibility and physical response are independent. A tangent touch may deliver an
object's effect without inventing a bounce impulse. The proposed implementation extends the
existing pair path with a certified touch observation and optional closing-impact certificate;
only the latter admits the established impulse equations. One symmetric pair composition owns
the resulting body changes and effects. Shared roots, exact time order, collision filters,
anchored epochs, retained certificates, and immediate termination remain authoritative.

The bounded architecture review preferred that extension over moving-body any-touch callbacks
through `MotionTrigger`: the latter is currently unary and would need a second implementation of
pair dependencies, contact priority, and two-body responses. Step 4a must prove the extension
before the Step 5 human decision; all live admission/signature changes remain named in Step 16.
Its proposed repeat policy consumes observations at both post-response motion revisions, even
when no motion changed; only an external trajectory change can re-enable that pair within the
quantum. This is not yet a once-per-encounter promise. Different cross-tick/encounter delivery,
independent sensor filters, or center-entry effects on blocking body hazards require explicit
follow-up design rather than accidental semantics.

Cliffs/darkness use the player's center, not the first overlap of the player's outline with the
void. Keep the canonical terrain support-loss query and one ground definition; do not create
cliff collider entities or duplicate darkness geometry. The center criterion is confirmed.
The existing supported-rim equality convention is retained pending any explicit boundary-rule
change; the owner's example does not supply a replacement numerical tolerance rule.

### Perfect shield; later behavior deferred

During a very short opening immediately after activation, a qualifying incoming opponent's
current velocity and acceleration are set to zero and the opponent is stunned. "Negate momentum"
means cancel it, not reverse it through a special reflection rule. Preserve pair non-closing
composition, incoming-motion eligibility, and symmetry. A mere any-touch effect observation is
not automatically a perfect parry. Stun blocks self-propulsion; later external collisions can
still move the dynamic body, as in the existing proposed stun lifecycle.

~~Treat ordinary post-opening protection and quarter-strength knockback as settled~~ — the owner
has left behavior after the perfect opening undecided. The existing pure ordinary-shield tests
and advisory benchmark remain historical prototype evidence, not approval of that behavior.
Neither continued ordinary shielding nor an immediately inactive shield may be silently chosen.
Resolve the complete lifecycle before Step 18. The owner confirmed a short opening, not new
exact duration/cooldown/stun values; existing numerical proposals remain tuning candidates.

### Mouse direction with authoritative fixed strength

The mouse should choose movement direction, with requested acceleration magnitude fixed by the
authoritative movement setting, independent of pointer distance or pointer speed. It does not
set velocity or make the blob arrive instantly at the cursor. Existing speed-ceiling, drag,
momentum, and movement-lock semantics continue to apply. Future fast/slow zones or time periods
may vary effective movement parameters at the same shared server locomotion owner; they are not
new features or reserved schema in this amendment.

Step 11a reuses `useThrustInput`, the existing `set_thrust` path, and the current canvas projection
to produce zero/unit mouse direction. Other producers' accepted subunit analog-command
arithmetic is not globally rewritten. A stationary cursor still needs a new direction when the
blob or camera moves. Pointer release clears intent and permits ordinary coasting; it must not
delete velocity. Keyboard access, UI/typing isolation, cancellation, ownership, and fresh
activation after stun apply to the same input owner.

The owner subsequently selected **right-button hold on the arena for mouse thrust**, preserving
primary/left-drag manual camera panning (2026-09-10). Releasing the right button clears mouse
thrust intent; it does not cancel existing momentum. Context-menu suppression is scoped to the
arena steering gesture, not unrelated room controls or the whole page. Exact-center behavior
and keyboard/source priority remain proposed implementation defaults to review, not hidden
proportional acceleration. Ability aim later reuses this same direction owner. This confirms
the activation binding, not implementation completion or Step 5 acceptance.

This input clarification is distinct from the prototype's numerical-admissibility limitations.
It neither broadens accepted geometric input ranges nor approves silent precision/work fallback.

### Capacity and performance deferred

The owner does not expect 256 simultaneous physical bodies and has deferred selecting supported
capacity and optimizing performance. Native evidence is no longer a prerequisite for the
present Step 5 design decision; it remains required before performance/release certification.
Existing hard bounds remain provisional engineering guards, not a product capacity target or a
native performance claim. The new per-object behavior still requires proof and human review.

Simpler geometry and reduced clock rate or resolution are possible later tradeoffs. No such
change is made now. In particular, the 400 Hz clock, deterministic representation, and accepted
fixture values remain intact; changing those is a deliberate contract/fixture migration, not an
automatic runtime degradation. Step 24 retains the native verification requirement.
