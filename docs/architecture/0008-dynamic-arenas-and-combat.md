<!-- canonical: dynamic_arenas_and_combat -- accepted design for shared terrain, motion, abilities, tuning, and tactical personalities -->

# 8. Dynamic arenas, player abilities, and tactical personalities

* **Status:** Design accepted 2026-09-10 at plan Step 1; revised prototype boundary accepted by the owner at Step 5 on 2026-09-11. Phase B is implemented; Phase C is authorized but its live-kernel and combat steps still require implementation and verification. Native performance/release certification remains deferred.
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
and bot planning. ~~Hill motion also requires ground clearance~~ — superseded by the owner's
2026-09-11 terrain-independent hill clarification below. Do not independently reproduce the
predicate in each consumer.

**Boundary convention:** retain race's supported closed road edge and existing named position
tolerance. Exact hole rims are supported; entering the hole interior beyond that tolerance is
void. Freeze the interval endpoint rules in geometry tests, including road bends, overlaps,
tangencies, and a segment which crosses a narrow hole and lands on ground again.

Players fall when their centres leave support; this is not a wall to bounce off and not vertical
gravity. A radius-aware clearance query is separately used for safe seating, not to constrain
the roaming hill's route (owner clarification, 2026-09-11).
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
Charge and external knockback may exceed it ~~and decay under drag~~. A separate validated physical
safety envelope bounds all speeds and event work; it is not an "unlimited speed" UI option.

**Promise closed and one sentence corrected, 2026-09-12 (plan Step 19).** The separate validated
safety envelope is now a real authored key rather than a promise: required
`[abilities] charge_safety_envelope_speed`, 20,000 wu/s, validated finite, strictly positive, and
at most `simulation::kMaximumPhysicalComponentMagnitude`, and checked against *combined* current
velocity and burst before a charge commits. It is an **engineering guard, not a balance option**,
and it is deliberately not a UI control: it bounds representability and per-tick motion-event work,
so exposing it beside acceleration and normal top speed would offer a player a lever over how much
solver work one activation may cost. The number itself is initial tuning that the owner has never
selected — what the owner accepted at Step 1 is *that such an envelope exists* (§ "Owner decisions,
2026-09-10"), and 20,000 changes by editing the section rather than by amending this ADR.

**"and decay under drag" is struck because it is false in the shipped configuration.**
`drag_per_second` is a `[simulation]` kernel parameter and `config/blob-royale.cfg` authors it as
`0`, as does every replay fixture under `tests/fixtures/replays/` except `royale-drag-decay` —
seventeen of the eighteen checked in at this step, `royale-charge-burst` among them. At zero drag
ADR 0003's phase 1 damping factor is exactly `1.0` and multiplication by `1.0` is the identity, so
a charge burst is **permanent**, not decaying. The claim holds only where drag is authored nonzero:
`deploy/ubuntu-pc/blob-royale.cfg` (2.0) and the `royale-drag-decay` replay (2). What bounds
repeated charges is therefore the envelope, not drag, and that is the whole reason the envelope is
a required key rather than an incidental representability check — see § "Charge" below for the
arithmetic at the authored ceiling. "Do not emulate the speed setting by changing drag" below is
unchanged and now cuts both ways: drag is also not the mechanism that limits an ability.

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

Random roam has positive minimum/maximum sampled scalar speed and bounded direction/speed
retarget intervals. Initial values: 20–70 wu/s, a new target every 0.35–1.2 seconds.
~~It moves every running tick and holds a selected velocity between retargets.~~ Boundary
cancellation may reduce or zero actual velocity; otherwise the selected velocity persists.
~~It reflects through shared clearance geometry at
legal boundaries, and its disc must remain over reachable ground with authored clearance.~~
**Superseded by the owner, 2026-09-11:** the hill itself does not bounce. Its route may cross
restricted/dangerous cliff areas, holes, and other unsupported terrain; it is a moving capture
zone, not a ground-bound physical body. The owner also chose no spawn-reachability restriction:
locally safe regions need not connect to a player's spawn. This does not restore the removed
requirement that the whole hill remain over safe terrain during movement.

The hill does not create ground or protect a player from the terrain underneath it. Existing
player support/fall rules and scoring ownership remain separate. There is no authored dwell,
teleport, or per-frame browser randomness. ~~There is no zero-speed stall.~~ Boundary rests are
intentional. Swept disc containment, terrain reflection, and spawn-connectivity validation are
not prerequisites for hill motion.

**Outer-map decision, 2026-09-11:** cancel outward displacement and velocity, without forced
inward steering or bouncing. Bounds constrain the center to the exact closed rectangle; retain
ADR 0007's permission for the circle to overhang. Compute an unbounded displacement and proposed
endpoint through the existing canonical motion functions. Per axis, a strict overshoot retains
the starting coordinate and zeros that velocity component; exact boundary arrival commits the
coordinate and zeros outward velocity. Preserve the other axis without renormalization. No
epsilon, radius inset, reflection, early retarget, or extra random draw is used. Canceling a
whole axis step may stop the center short of the edge. Normal scheduled retargets continue even
at rest, and repeated outward selections may be canceled; no bounded escape time is guaranteed.

Store velocity, random stream state/identity, and next-retarget tick in committed world state.
Reuse the canonical deterministic generator and written draw order, without platform-dependent
standard distributions/trigonometric direction generation. Derive independent stable streams for
hill motion and hazard spawning so adding a hill draw does not reshuffle hazard behavior. Preserve
zero extra draws for the tour policy. Publish only current public motion, not future random
targets/RNG state that would give bots privileged predictions.

Step 12's concrete sampler uses a rational quarter-circle parameter, quadrant rotation, and
written square-root normalization. Draw order is parameter, quadrant, scalar speed, retarget
interval; equal ranges still draw, and bounded-integer rejection stays visible in draw counts.
There is no uniform-angle claim. Scalar speed intake is `[2^-10, 1,000,000]` wu/s (the lower
bound keeps a dominant-axis step representable at the maximum map extent); realized vector
norms are binary64-rounded. Retarget ranges convert to positive ordered ticks, at most one
hour. These engineering limits are not a native capacity claim. Existing authored configurations
explicitly retain `marker_tour`, which has no `HillMotion` component or extra random draws.

Lobby/countdown holds the initial position; running moves; ended freezes. Explicitly reset the
non-participant hill's motion state for a new round. Advance motion before scoring so points,
published geometry, and the browser agree about the current hill.

These phase names describe the phase observed by the movement system. The engine's lifecycle
transition runs afterward: a countdown-to-running transition snapshot still holds the marker,
and the next tick first samples and advances. Likewise an ended-to-lobby transition snapshot
remains frozen until the next movement pass observes lobby and resets. No lifecycle order changes.

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

**Amended 2026-09-12 (plan Step 18).** The shield half of this table is now enforced rather than
proposed: the shared `ability` system admits a pulse only in `running`, with a `Controllable` and a
non-static body, outside the canonical input lock, at a matching input generation, with no active
protection and an expired cooldown. All four gameplay modes declare the system and advertise
`shield`, including Sandbox, so the last row's "Enabled" is now true of a real command. ~~The charge
half remains a proposal until Step 19.~~ See § "Amendment: tap shield and the live guarded
composition, 2026-09-12 (plan Step 18)" below.

**Amended 2026-09-12 (plan Step 19): the charge half is enforced too, by the same system and the
same four gates.** `AbilitySystem` evaluates running phase, non-zero tick, non-static body, and the
canonical input lock **once** against the world at entry, then decides both abilities against that
one evaluation. The column heading "Charge and shield" is now literal rather than aspirational:
there is one admission matrix because there is one system, and the last row's "No new activation"
covers a charge pulse identically. On top of those four, each ability applies exact optional
generation equality and refuses while protection is active; the two then differ in their own last
gate. Shield asks for an expired shield cooldown. Charge asks for an expired **charge** cooldown —
the two cooldowns are separate keys on separate components and neither gates the other ability —
plus an obtainable unit direction whose resulting speed the safety envelope admits. Then the
conflict rule below.

**The conflict rule, as landed.** Eligibility for both abilities is computed **before either
write**, and `protection_active` is read into a local before anything mutates a store. Shield is
applied when it is eligible; charge is applied when it is admissible **and shield is not eligible**.
The gate is spelled `!shield_eligible` rather than "no `Shield` component present", which is what
makes the plan's "an ineligible shield pulse does not suppress an eligible charge" provable rather
than incidental. Losing the tie consumes no charge cooldown, queues nothing, and leaves no trace:
the charge branch simply does not run. Computing eligibility first is not a stylistic preference —
under a naive "apply shield, then decide charge" ordering the tie would resolve by re-reading a
store the shield branch had just written, so "a shield is active" and "a shield was activated on
this tick" would be indistinguishable, and the rule would happen to work only because a positive
shield duration is enforced in a different file.

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

**Implemented 2026-09-12 (plan Step 19).** Every sentence above is now shipped behaviour, with four
things this section did not say and a reader would otherwise get wrong.

*The gain is authoritative and the direction is normalized, not clamped.* `charge_speed_fraction`
is `0.75` — a dimensionless multiple of the **current** normal movement ceiling read from match
state, not an authored speed, because that ceiling is live-tunable and "0.75 times the current
normal movement ceiling" above means the ceiling a room is actually running. Charge therefore may
**not** use `gameplay::normalized_thrust_intent`, which is this tree's single magnitude *clamp*: its
scale is `1.0` whenever the magnitude is at most one, and its own header says "Subunit analog intent
keeps its magnitude". That is correct for an analog throttle and wrong here. Routed through it, a
client sending `{"x":0.5,"y":0}` — legal under the per-component unit bound `set_thrust` already
publishes — would receive half the burst, making pointer distance into strength: the exact inverse
of § "Mouse direction with authoritative fixed strength", which fixes requested magnitude by the
authoritative setting "independent of pointer distance or pointer speed". A one-shot activation with
a stated fixed gain must scale by nothing the client controls, so a new canonical helper,
`gameplay::unit_direction`, sits beside `normalized_thrust_intent` in
`src/gameplay/shared/locomotion.{hpp,cpp}` and always returns a unit vector or nothing. It computes
`sqrt(x*x + y*y)` written out, divides rather than multiplying by a reciprocal, and returns
`std::nullopt` — never throws — when the magnitude is not finite, is zero, or when the divided
components are not finite or leave the component domain. The refusal band is wider than exact zero:
`{"x":1e-200,"y":0}` passes the decoder and `InputBatch`, its squared magnitude underflows to zero,
and a reciprocal would be infinite. **Charge normalizes where thrust clamps**, and the two helpers
sit side by side so that the difference is a decision a reader can see rather than infer.

*Refusal is silent, and that is not a contradiction of "refuse explicitly, not silently convert".*
This section's sentence contrasts refusal with **conversion** — with quietly delivering a different,
weaker move — not with silence. Every inadmissible charge is a no-op on the path every other ability
refusal already takes: no cooldown consumed, no queued activation, no event, no error, and no
receipt. A thrown validation error would be strictly worse than silence, because `AbilitySystem`
runs inside the tick: a throw escapes `GameSimulation::step`, the runtime worker records a worker
failure and returns, and the room is dead. One client's charge must not be able to end a match, so
the envelope is checked in raw doubles **before** any `Vector2` is constructed, where
`Vector2::operator+` would otherwise throw on a component past `1e12`.

*The cooldown is validated strictly positive, unlike the shield's.* `charge_cooldown_seconds` is
`1.2` — 480 ticks at 400 Hz — and a value rounding to zero ticks is refused at startup with
`GAMEPLAY.ABILITY_DURATION_NOT_POSITIVE`. The shield's deliberate zero-cooldown exemption does not
transfer: shield admission has a second gate, because a new pulse also requires the prior protection
to have ended, and charge is one-shot with no protection window at all, so a cooldown of zero ticks
would admit a burst on every tick — four hundred a second.

*One cross-key rule bounds the fraction, and nothing else needs to.*
`charge_speed_fraction * simulation::kMaximumNormalTopSpeed <= charge_safety_envelope_speed`, or
`GAMEPLAY.ABILITY_CHARGE_BURST_EXCEEDS_SAFETY_ENVELOPE` at startup. In words: a charge from rest
must stay admissible whatever a room tunes its ceiling to. At the shipped `0.75` and the
`10,000` wu/s tuning maximum that is `7,500 <= 20,000`. Without it an unbounded fraction is a hole
rather than a knob — `charge_speed_fraction = 1e6` against a 10,000 wu/s ceiling is a `1e10` wu/s
burst whose components still sit inside `Vector2`'s domain and which then exhausts the motion-event
work cap on its first tick, and an exhausted cap is fatal to the room. The rule closes that without
inventing an arbitrary ceiling on the fraction itself.

### Timed shield and perfect opening

Recommended first version: a tap activates 400 ms of shield; the first 80 ms is the perfect window;
activation cooldown is 900 ms. At 400 Hz these are 160, 32, and 360 ticks. Windows are half-open:
`[activation_tick, activation_tick + duration_ticks)`. Cooldown starts on activation. These numbers
are hypotheses to tune with real latency, not a reproduction of Smash's exact mechanics.

**Amended 2026-09-12 (plan Step 18):** ~~Recommended first version~~ — these four values are now the
authored required `[abilities]` section (`shield_duration_seconds`, `shield_perfect_window_seconds`,
`shield_cooldown_seconds`, `parry_stun_duration_seconds`), validated by
`gameplay::AbilityConfiguration` and converted once through the shared `duration_ticks` rule.
Landing them as configuration does **not** promote them from hypothesis to balance:
0.4/0.08/0.9/0.6 seconds — 160/32/360/240 ticks — remain the ADR's initial tuning assumptions, not
owner-selected values, and they change by editing the section rather than by amending this ADR.
The half-open window and activation-start cooldown sentences above are now the shipped
`TickWindow` semantics verbatim.

- Normal shield blocks lethal-contact elimination and targets 25% of the ordinary received
  collision impulse. Every guarded response must also leave the pair non-closing. If the nominal
  reduction conflicts with separation (notably two ordinary shields moving toward one another),
  the pair-level dissipative separation correction takes precedence over the exact percentage.
  Do not independently scale two velocity deltas and assume the pair remains valid. ~~The prototype
  must pin the formula, mass/static behavior, energy behavior, and dual-shield golden cases.~~
  Satisfied on 2026-09-12 (plan Step 18): the shipped pure core is the pinned formula, and Step 18
  calls it rather than restating it. The quarter target remains **initial tuning** and the
  pair-level non-closing correction still takes precedence over the exact fraction; this bullet,
  § "The guarded-pair core", the `(10, 9) -> (9.75, 10)` worked example, and § "Owner decision:
  ordinary protection after the perfect opening, 2026-09-10" are the four statements of it, and
  Step 18 deliberately adds no fifth.
  Shield is not immunity to cliffs or zone rules; support loss and zone exposure never consult a
  guard, which is live behaviour as of Step 17 and Step 18 respectively.
- Perfect shield adds a 600 ms / 240 tick stun to the incoming **dynamic** body, including a
  bounceable hazard. Static walls cannot be stunned. Require actual incoming motion; ramming a
  stationary target with a shield does not manufacture a defensive perfect-shield bonus.
  Implemented 2026-09-12 (plan Step 18) as authored `[abilities] parry_stun_duration_seconds`,
  captured on the defender's `Shield` at activation and read back off the **defending** body when
  the composition's stun fact is translated, so the frozen defender supplies the effect it owns.
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
  Implemented 2026-09-12 (plan Step 19) exactly as written, in the one `AbilitySystem` that owns
  both: `protection_active` is read into a local before any write, both eligibility booleans are
  computed before either write, and the charge branch's conflict gate is spelled `!shield_eligible`.
  § "Charge, shield, and stun" above records why each of those three is load-bearing rather than
  incidental.

Do not implement every flag combination as a contact-rule row. Use one pair-symmetric composition:
immutable pair facts → existing base physical equation → defense modifications → typed effects
and motion disposition. Reuse mass/restitution/static equations. Preserve the existing unguarded
lethal-hazard pass-through response for the surviving hazard; the eliminated player leaves the
remaining event stream. Guarded lethal contact instead takes the shared defensive response. The
composition is the one response for every dynamic pair, and the unguarded pass-through is its
lethal branch rather than a separate row (decided at Step 1 on 2026-09-10).

**Implemented 2026-09-12 (plan Step 18).** This prediction is now the shipped arrangement. The
standalone `lethal_hazard` row and `src/gameplay/shared/lethal_hazard_contact_rule.{hpp,cpp}` are
deleted; its running-phase and player-presence predicates and its diagnostic name move unchanged
into `guarded_pair_contact.{hpp,cpp}`, and its response is the composition's lethal branch. The one
declared row is `guarded_pair`, declared above the built-ins by all four gameplay modes. That one
row carries **two** diagnostic names: a lethal contact still reports `rule_name == "lethal_hazard"`,
so the retained pass-through proofs read exactly as before, while every composed non-lethal contact
now reports `guarded_pair` where it previously reported `elastic_disc`, `variable_impulse`, or
`reflect_static`. This is one row with two names, not a rename.

### Stun and input lifecycle

Stun is a reusable temporary control lock on a dynamic entity, not a bot-only flag or an `is_static`
hack. It immediately kills momentum at the triggering impact; subsequent external impulses may
still move the stunned body. It blocks steering and fresh charge/shield activations until expiry,
never restores old velocity, and merges repeated stun requests by maximum expiry, not addition.

**Charge needed no `StatusSystem` change at all, 2026-09-12 (plan Step 19), and that is this
paragraph's own doing.** A burst already in flight keeps flying when its owner is stunned, which is
precisely "never restores old velocity" and "subsequent external impulses may still move the stunned
body": the status pass zeroes intent and acceleration and never touches velocity. A *fresh* charge
during a stun is already refused with no new code, by the canonical input lock plus the generation
bump that invalidates a pulse stamped before the stun. A live charge cooldown survives a stun for
the same reason a shield's does — the status pass is not the cooldown's owner. Nothing about charge
is written into the status system, and nothing should be.

Defense state is fixed for one outer tick; post-kernel status application clears shield and input
intent for following ticks. Thus a body parried earlier may still shield another contact in the
same 2.5 ms tick. This bounded grace is intentional; changing it would require contact-time status
mutation and a new contract, not a hidden implementation choice. Duration starts at the committed
impact tick. The no-input interval and exact expiry tick must be tested.

**Implemented 2026-09-12 (plan Step 18), and the grace is preserved, not retracted.** `StatusSystem`
cancels still-active protection at `kPostKernel` by shortening the shield window, and the perfect
opening it contains, to the cancellation tick; the original activation, any already-elapsed perfect
history, the cooldown window, and the captured parry-stun duration all survive, so a cancelled
shield still refuses a new pulse for the rest of its cooldown. Because a response reads the frozen post-`kPreKernel` world and
`StatusSystem` runs after the solve, that cancellation first takes effect on tick N+1 — which is
exactly the bounded grace this paragraph describes, now realized rather than merely permitted.
Cancelling at the activation tick legitimately yields empty protection, cancelling twice is an
exact no-op, and cancelling before activation is a named chronology error. Only the `ability`
system removes a fully expired `Shield`; `StatusSystem` never erases one.

Current hazards have zero acceleration/drag and an assigned initial velocity, not an ongoing
driver (`src/gameplay/shared/hazard_spawn_system.cpp:83`). After stun they stay stopped until bumped;
their lifetime keeps counting down to preserve existing population bounds. Future driven objects
must consult the same lock before reapplying propulsion.

Persist normalized steering intent separately from acceleration. Stun clears that intent; ignore
commands received during stun rather than queueing an action for expiry. New activations require a
fresh press. ~~A continuously held movement key may resume through a newly accepted update after
expiry.~~ The owner-approved Step 14 generation contract below invalidates that activation even
when every active-stun snapshot is missed; only a fresh press captures the new generation.
Clear body-bound states on elimination; reset abilities to ready on respawn
for the first version, and on round restart. Test zero-delay respawn explicitly. Live as of
2026-09-12 (plan Step 18) for shield: `Shield` declares the Step 13 body-bound lifetime trait and
nothing else, so body loss and round reset clear it through the one registry sweep, and a returning
body is ready because it carries no `Shield` at all — no per-owner cleanup and no reset field.
`Charge` does exactly the same on 2026-09-12 (plan Step 19), which is the point of stating the rule
once: the second ability cost this paragraph no second mechanism.

The mailbox coalesces same-kind inputs, so multiple same-tick activation pulses mean at most one
attempt, not queued charges. Key repeat cannot reactivate; blur/disconnect/body loss clears local
pressed/aim state. Proposed bindings are Space for charge, Shift for shield, alongside ordinary
screen buttons. Input typing and camera controls must not accidentally send abilities or thrust.
Perfect timing is judged by the server-accepted tick, not client-provided timestamps; prediction,
rewind, and latency compensation are explicitly deferred.

## Tactical personalities without a class per mood

**Step 15 implementation clarification, 2026-09-11:** The owner approved working objective-seeking,
reaction timing, aim error, and target persistence first. The precise contract is
`docs/reviews/2026-09-11-tactical-profile-contract.md`. Objective seeking chooses unit-strength
go versus coast probabilistically; it does not vary the acceleration of go input. Profiles are
strict authored values with a bounded names-only selection catalogue, not mood classes. Preserve
legacy diagnostic seeds, arithmetic, and repeated-observation behavior; identity-based seeds and
duplicate/stale-observation rejection apply only to tactical. A guarded bot join preserves full
profile declaration identity while existing unguarded human/CSV joins retain their semantics.
Sandbox tactical startup is explicitly unsupported because it lacks the required seat identity.
Step 15 uses only current public objectives and straight-center terrain screening; predictive
combat/escape planning and the named combat personalities below remain later work, not claims of
this foundation. No inert aggression/charge/shield settings are accepted before their behavior.

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
  ticks roll back motion and RNG; no reset leakage or score/render drift. ~~No stalls.~~
  Boundary cancellation permits rest while ordinary scheduled retargeting continues. Prove permitted
  hill travel across dangerous/disconnected terrain without changing player support or creating
  safe ground. The former prohibition on unreachable hills is superseded on 2026-09-11.
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

## Terrain construction provenance repair, 2026-09-10 (plan Step 7a)

The owner approved a focused provenance-preserving repair after unchanged sloped/fractional
fixtures exposed a manufactured angular sliver. A separate clipped-envelope case lost the identity
of an existing side/cap endpoint while recomputing its line intersection. Captured hexadecimal
evidence lives in `docs/reviews/2026-09-10-racer-projection-admission-blocker-review.md`; that report
and the Step 7a verification record distinguish diagnosed causes from verified completion.

Known construction relationships are retained through boundary compilation: a capsule side keeps
its authored orientation, an arc endpoint keeps its authored radial direction, and a certified
intersection at an existing endpoint retains that endpoint's identity. Endpoint containment
certificates use literal axis coordinates/ranges and matching retained axis orientation; a side
whose tiny authored slope merely rounded onto an axis does not qualify. An axis line through a
circle's center reuses the arc's already-stored included cardinal constructions, which exhaust
that diameter's intersections. General line/circle root arithmetic is unchanged. This is not proximity
merging and does not make independently constructed nearby points equivalent. Unrelated curves,
including equal-tangent branches with distinct curvature, retain their own incidences and support
constraints. Existing missing/conflicting-incidence and precision/work guards remain fail-visible.

The compiler classifies adjacent open angular sectors using their ordered boundary rays and
incident halfspace signs before constructing an interior vector. Only the first canonically
supported sector needs a representable recovery direction; an unsupported sector does not need a
numeric witness. A selected supported sector with no representable direction still fails rather
than choosing a different sector or feature. The four-target, selected-feature recovery contract
above remains unchanged. Absence of a strict first-order angular cone does not prove absence of
supported curved regions, rims, or isolated points, and must not delete them from the boundary.

This refines the construction/provenance contract, not authored tolerance, point support, event
roots/order, runtime clock, wire geometry, or gameplay policy. The pure Step 8 arithmetic promotion
still requires both pre-delegation lanes before its old readers switch. No exact-geometry library
replacement, blanket precision increase, coordinate snapping, native certification, or Step 5
acceptance follows from this repair.

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
same guarded-pair core instead of implementing another composition — done on 2026-09-12 by
`src/gameplay/shared/guarded_pair_contact_rule.{hpp,cpp}`, which projects committed `Shield`
windows into the core's frozen guard facts and translates its typed consequences, calling
`compose_guarded_pair` unchanged.

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

### Perfect shield; later behavior initially deferred

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
~~Resolve the complete lifecycle before Step 18.~~ The owner confirmed a short opening, not new
exact duration/cooldown/stun values; existing numerical proposals remain tuning candidates.
This historical deferral is superseded by the later owner decision below, and that decision was
implemented on 2026-09-12 at plan Step 18.

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

## Supplemental contact proof completed, 2026-09-10 (plan Step 4a)

The permanent pure pair path now carries one source-oriented touch observation, optional
closing-impact certificate, and per-object effect eligibility. Omitted frozen policies explicitly
mean closing impact; invalid, duplicate, absent-entity, and oversized policy declarations fail.
Only the optional impact admits physical response or perfect-parry arithmetic. No-op observations
consume both post-response revisions; external trajectory changes can re-enable the pair under
the existing budgets. This implements the clarification without a second pair engine, root
owner, or live-kernel change.

The supplemental `rigorous-architect` source review closed with no remaining blocker. Its
concrete Step 16 projection is an optional body-bound `ContactEffectAdmission` component holding
a validated nondefault policy. Absence means closing impact. Resolve explicit instance input
before an archetype default and canonical omission, so an explicit closing override can disable
an archetype's any-touch default. Static authored body rows co-own geometry and instance policy;
dynamic creation accepts typed instance input. The full authoring/registration/lifetime/v3
publication migration remains Step 16, not part of this proof.

Both advisory Mac/Docker lanes passed 761 simulation/gameplay cases and 42 accepted fixtures.
All eight benchmark cases and delivery passed; the three default-policy prototype workloads
kept identical correctness hashes and work counts. See
`docs/reviews/2026-09-10-per-object-contact-prototype-review.md` and its sibling baseline JSON
for scope, numerical limitations, and measurements. This is not human Step 5 acceptance, native
certification, capacity selection, or a decision about post-perfect-window shielding.

## Owner decision: ordinary protection after the perfect opening, 2026-09-10

The owner selected **ordinary protection with reduced knockback for the remaining shield
duration**. This supersedes the earlier post-opening deferral and clears that hold for Step 18.
The short perfect opening still cancels a qualifying incoming opponent's current velocity and
acceleration and applies stun. After that opening, contact protection and reduced received
impulse remain, but the special momentum cancellation and parry stun do not. Cliffs continue
to bypass all shield phases.

Use the already tested quarter-impulse target as the initial tuning assumption communicated
during execution, with the pair-level non-closing correction taking precedence. The owner
confirmed reduced knockback, not that exact fraction; exact shield/perfect/stun/cooldown
durations likewise remain tuning candidates. Reuse the permanent pure composition and the
shared tick-window lifecycle rather than introducing a second shield response.

This is a gameplay decision, not approval of live continuous-physics adoption at Step 5,
native capacity/performance certification, or ~~evidence that the live shield is implemented~~ —
the live shield landed on 2026-09-12 at plan Step 18 and is recorded in its own dated section
below; this decision remains the gameplay authority for what it does, not the evidence that it
works.

## Session terrain publication contract, 2026-09-10 (plan Step 7)

The selected map is shared immutable ownership, and snapshot/observation terrain aliases that
map's actual member with authored-value equality and lifetime retention. Welcome requires its
explicit complete terrain; frames omit it. Wire terrain carries bounds, positive ground kind,
ordered named corridors, and ordered named circular holes under existing shape/point/name
limits. Clients validate the whole shape, aggregate/containment/segment constraints, and exact
agreement with v1 configuration bounds before publication or rendering. No geometry repair or
fallback rectangle is permitted. One world layer draws positive ground minus the union of
holes through the existing camera projection; race draws objectives only. **Refined 2026-09-12
(plan Step 20):** that one layer now also draws the cliff rim bounding the supported set and the
void underneath it, from the same geometry and the same projection; see § "Amendment: cliff and
combat rendering" below.

Canonical routes are `/api/v3/lobbies` and `/api/v3/lobbies/<lobby_id>/session`, subprotocol
`blob-royale.session.v3`; no new room-one alias. Recognized old v2 routes fail explicitly before
upgrade/session/controller admission but after global request security checks. The one v3
error envelope also handles global errors on both parsed session-version prefixes; v1 remains
closed. The full fixed retirement response and precedence are normative in `docs/protocol/v3.md`.
Historical v2 schemas/examples remain validated, without an active v2 encoder.

Shared JSON geometry goldens cover positive corridor unions, overlapping-hole subtraction,
round caps, and envelope clipping. C++ cases use the canonical factories and support query;
browser tests use the same data for validation/rendering. Raster antialiasing is not a second
gameplay support predicate. Phase C adoption and release/native verification remain separate
gates; this entry specifies the Step 7 contract and does not claim those gates passed.

## Race terrain-reader contraction, 2026-09-10 (plan Step 8)

Race's temporary `track`/`track_half_width` mirrors are removed from mode state and session v3.
Required `road` is selected identity, not geometry: a non-default validated bounded name in the
authoritative race block, exactly resolved against terrain at race initialization and consumer
boundaries. The client retains welcome terrain to reject missing bindings and a checkpoint radius
larger than the selected half-width before publishing a snapshot. There is no arbitrary first-road
selection, fixed-name fallback, or new generic simulation create/commit validation hook.

The existing race helper initializes from a bound course. Both recorder and publisher already own
that course; their order and member ownership remain unchanged, including an initially Running
first-tick finisher. Production tick-zero state remains `NoModeState`. Direct controller/protocol
fixtures explicitly supply matching authored terrain instead of installing race state on a bare
solid arena.

One canonical centreline projection arithmetic core replaces the racer's private loop, preserving
first-authored ties and the exact written operation order. The old distance-only API does not gain
point-construction errors on its broader standalone-corridor domain. Bounded-name storage is
consolidated at its documented third-name threshold, retaining distinct policies and old default
sentinels while disallowing an empty default for road identity. Both extractions require proof on
the two lanes before old readers/types delegate. The plan owns completion evidence; this contract
does not approve live solver adoption. The preceding Step 7a separately resolved the diagnosed
clipped-corner admission issue recorded in
`../reviews/2026-09-10-terrain-clipped-corner-admissibility-review.md`; bounded numerical limitations
and the Step 5 human gate remain.

## Amendment: committed movement-tuning feedback, 2026-09-11 (plan Step 10)

The owner approved request-correlated, server-confirmed applied/rejected results, with an
interrupted request shown as unknown until current room values are checked. This resolves the
execution review checkpoint, not the separate Step 5 physics gate. The contract below refines
§ "Normal movement and web tuning" and the plan's named kernel-seam allowance.

The simulation-owned validated pair has acceleration in **0..10,000 wu/s²** and normal top speed
in **1..10,000 wu/s**. Initial authored defaults remain **400/600**. These finite parameter bounds
are engineering defaults, not a native capacity certificate or a claim that all external speeds
are capped. Fixture acceleration stays unchanged and its explicit ceiling is 10,000, with
bit-identical replay gates required. One required `[movement]` section uses full unit-bearing
keys; `GameModeConfiguration` carries it into match startup. Match state owns current/default
pairs, revision, and effective tick. Defaults reset only on room recreation; Apply/Reset changes
current values through the same command. No INI rewrite or process-global live setting is added.

Optional normalized intent distinguishes no submitted input from explicit coast. Preserve the
old written arithmetic in the unconstrained branch, including subunit analog semantics. A
finite-step constraint projects requested Euler velocity into radius `max(normal speed, current
speed)` and returns acceleration, never velocity. Canonical integration must confirm the speed
and non-amplification postconditions; bounded rounding failure remains visible. Body seating and
publication own intent cleanup/privacy. Phase 1 still integrates and drags once. The existing
phase admission stays intact; future movement modifiers and the Step 14 lock share this owner.

The numerical contract uses computed binary64 squared norms. Canonical requested integration
retains existing domain errors. Return original acceleration verbatim on the uncapped branch.
Accept identity of the first computed radial projection as zero, explicitly allowing a tiny turn
to be lost by rounding; this is not a pure-outward geometric certificate. Otherwise try the
initial radial factor plus eight corrections toward zero, always from the original endpoint;
each radial attempt has its initial non-amplifying acceleration scale plus eight corrections.
Recheck both postconditions through the canonical integration operation order. A later zero
acceleration or endpoint equal to current velocity fails immediately rather than continuing into
artificial braking. Exhaustion is `GAMEPLAY.LOCOMOTION_PRECISION_LOST`. This bounded witness policy
does not certify exact-real inequalities or guarantee finding every representable solution.

The command adds both scalars, `expected_revision`, and a correlation-only `tuning_request_id`.
For tick N, freeze entry revision R. The existing phase-0 visitor checks current seated membership
at canonical position in lobby/countdown/running/ended. The last eligible contender against R
wins; matching-R losers are `superseded`, mismatches `stale_revision`, unseated senders `not_seated`,
and exhaustion `revision_exhausted`. One winner commits once to R+1/effective N, including equal
values; no winner leaves state unchanged. Tuning ranks after thrust and before Start while old
relative ranks remain. Bounded decisions are allocated before commit and returned only after it.

One mailbox-owned exchange per open controller uses the existing mailbox lock for admission,
eviction, commit completion, claim, and close. Boundary outcomes are `rate_limited`, `mailbox_full`,
and `mailbox_evicted`; they carry no fabricated decision tick or revision. Tuning is non-lifecycle
traffic. Initial interval is **500 ms**; the global pre-parse limiter and malformed-frame behavior
remain. IDs strictly increase per controller, with gaps allowed; they are never contender order.
A second unresolved request closes 1008 `tuning_request_in_flight`, and reused IDs close 1008
`tuning_request_id_reused`, instead of allocating a queue or overwriting a result.

Session v3 adds required `match.movement` (current/default pairs, limits, revision, effective tick),
the welcome minimum interval, and nullable `data.tuning_result` outside match. A result carries
request ID, status, nullable decision tick/revision, and retry-after milliseconds only for a rate
refusal. Claim a terminal result only into a covering snapshot, after egress admission and before
encoding/write. Claim reopens the runtime slot; the write callback releases only its session-owned
record. Thus a legitimate next request after receiving A is safe even before A's server callback,
and that callback cannot erase B. Failure discards the claimed local result and never reinserts
it over a newer exchange. Unobserved completion is unknown, never automatic retry or inferred
success from the current shared revision. Reconnect has a new controller and no acknowledgment
recovery. Protocol owns a validated wire value; a server adapter avoids a protocol→runtime dependency.

The browser keeps one `sendCommand` path, reserves pending state before sending, and resolves it
only on a matching validated result before accepting the frame. UI controls remain Step 11.
Security review retains existing room-bound capabilities and cooperative seated authority, not
new roles. Alternatives considered were inferring success from shared revision (cannot establish
whose command committed) and a generic receipt/event queue (adds ownership and backpressure not
needed here). What if two people race? Canonical revision rules decide once. What if a write
callback is late? Claim-before-write preserves the next request. What if movement gains modifiers?
The single intent/locomotion owner can compose them without another authoring surface. The plan
records implementation and exact verification; this amendment does not claim those gates passed.

## Owner UI and input clarification, 2026-09-11 (Steps 11 and 11a)

The owner selected **keep draft; require review** when another participant changes room movement
tuning during unsaved local edits. Preserve both edited values and their base revision, display
the newer authoritative pair, and require an explicit review action before applying the draft
against that revision. Review rebases the comparison revision, not the draft values. A subsequent
peer update requires review again. Reset explicitly submits current authored defaults through the
same command path. An interrupted request remains unknown even after current values are reviewed;
shared state cannot establish whose request committed. There is no automatic resubmission.

The owner superseded **right-button propulsion and directional-key priority** with **cursor-only
aim and held Space propulsion**. Mouse position determines direction from the owned blob; Space
requests the authoritative acceleration magnitude, and release clears thrust without deleting
momentum. Pointer distance and speed never scale acceleration. An exact-center pointer supplies
no direction and therefore zero thrust. WASD/arrows no longer steer after Step 11a; those keys are
available for later charge/shield bindings, without assigning or implementing abilities here.
The earlier proposed Space-for-charge binding is superseded. Left-drag camera panning remains.
**Assigned 2026-09-12 (plan Step 21):** two of that reserved pool are now spent -- `KeyS` activates
shield and `KeyD` activates charge -- and the four arrow codes stay reserved and stay asserted
inert. See § "Amendment: accessible ability controls" below, which also records why the
Shift-for-shield proposal in § "Stun and input lifecycle" is superseded rather than adopted.

One canonical input owner retains direction, send throttling, and cancellation, observing the
Canvas's existing shared projection rather than owning a second projection.
Typing, tuning controls, camera interaction, blur, disconnect, replacement bodies, and later stun
must not create or resume a held propulsion request; renewed propulsion requires fresh activation
after cancellation. The cursor can change aim without propulsion when Space is released. This
separates aim from go without adding another sender, wire command, or private movement strength.
Step 11 preserves current steering outside editing until Step 11a deliberately migrates source,
tests, help text, and real-browser scenarios together. Neither clarification approves Step 5.

The input implementation can observe welcome identity, owned entity identity, and body presence.
Fresh snapshot/body objects are not new body incarnations. A same-entity body removed and recreated
entirely between delivered snapshots cannot be distinguished with the current wire, which has no
incarnation token. Cancellation covers observed lifecycle transitions; no position-jump heuristic
or guarantee for an invisible transition is claimed. This step does not expand the protocol.

Implementation clarification (2026-09-11, Step 11a): Feature retains the sole input/sender hook;
Canvas publishes CSS-space pointer/body observations through a stable callback, and Viewer retains
camera policy. ADR 0004's dated cursor/Space amendment records these ownership boundaries. The
execution plan records exact web/browser evidence and the headless tab-blur verification limit.

## Amendment: body-bound lifetime ownership, 2026-09-11 (plan Step 13)

`ComponentLifetime<T>::bound_to_body` is a default-false simulation trait declared beside each
body-bound component. `GameWorld::erase_body_bound_components_without_body()` consumes the one
component registry, walks each selected store in ascending entity order without allocation, and
erases entries lacking a body. Shared respawn calls it once after all current body removals,
cleaning newly and already-bodyless entities even when they are not participants. The invariant
holds after that sweep, not after every intermediate mutable-store operation.

`HillPresence` and `ZoneExposure` declare the trait. Both scorer body-loss passes retire; normal
leaving-hill and point-rollover erasure remain scoring policy. Score, race progress, controller
identity, respawn timers, and the non-body hill/motion remain persistent. Royale still destroys
whole entities and therefore already clears every kind. ADR 0007's shared-respawn and scoring
paragraphs are amended in lockstep. Zero-delay return, tick order, accepted replay values, and
public component values do not change.

## Amendment: missed-stun input invalidation, 2026-09-11 (plan Step 14)

The owner requires held Space to stay canceled even when the client misses the entire stun.
Use optional public `Controllable.input_generation`, echoed exactly by `ThrustCommand` and
`set_thrust`. Absence means never invalidated, never a wildcard or compatibility fallback. Each
applicable positive request assigns the positive committing tick; this remains present through
expiry and same-entity respawn. Publication retains it while stripping private intent/commands.
Commands including zero releases are admitted only if unlocked and their optional token matches.
Mismatches do not replace current valid intent. Existing coalescing remains last-submission-wins.

The browser captures the observed token on a fresh non-repeat Space press. Every update/release
keeps that token; generation changes retire held and queued work without retagging it. Snapshot
ticks determine observed status expiry, never wall time. Shared bot steering authoring copies
the token from each fresh public decision and suppresses observed stun. Scripted replay remains
literal. This is an entity-lifetime guarantee, not detection of an invisible destruction/reseat;
the existing Step 11a replacement limitation still applies.

`TickWindow` is a simulation value with validated absolute activation/expiry, `[t,t+d)` semantics,
and checked addition through the existing safe-integer TickSequence limit. Stun publishes both
endpoints to all observers. A zero request is a no-op; missing/bodyless/static targets are
ignored. Validate all applicable requests before mutation, reject a positive request at tick
zero, merge active windows by maximum expiry, and replace expired windows without joining gaps.
Every applicable request invalidates input even if it does not extend a longer active interval.
One-tick duration expires at the next steering evaluation because application is PostKernel.

The first shared gameplay lock predicate is introduced here; Step 10 supplied intent but no
lock. `StatusSystem` runs last in each mode's PostKernel list before unchanged lifecycle stages.
It clears self-propulsion and explicit-zero intent, not velocity. ~~Actual impact momentum kill
remains Step 18~~ — it landed on 2026-09-12 in the guarded composition's perfect branch, which
zeroes the incoming body's velocity and acceleration inside the response rather than in status;
subsequent bumps and hazard lifetime continue. Body loss uses the Step 13 trait.
~~The narrow StunRequest test-foundation/Step 18 production exception is recorded in ADR 0004.~~
That exception closed on 2026-09-12 when the production producer landed; ADR 0004 records its
terminus in place.
The detailed implementation/test contract is
`docs/reviews/2026-09-11-stun-input-generation-review.md`. No unstunned fixture or existing bot
random behavior changes, no new kernel policy socket, and no Phase C authority are implied.

## Human acceptance of the physics prototype boundary, 2026-09-11 (plan Step 5)

After the Step 15 completion handoff, the owner explicitly answered "yes" to approval of Step 5
and commencement of Phase C. The handoff linked the revised Step 4a review and advisory baseline
and surfaced the remaining numerical, repeat-contact, and shield-energy limitations. This accepts
the reviewed Step 4/4a motion model together with Step 7a's repaired construction provenance;
the original sloped/fractional and clipped-corner inputs are not accepted rejection cases.

The accepted boundary retains bounded binary64 geometry and whole-sweep endpoint admission,
initial overlap without depenetration, revision-based within-tick repeat suppression rather than
once-per-encounter delivery, limited recovery search, and visible precision/work exhaustion.
The ordinary shield's quarter-impulse proposal remains initial tuning with its documented
external-energy behavior, not a new globally energy-conserving contract. Per-object touch versus
closing-impact eligibility, player-center support loss, and ordinary post-perfect protection
remain the already-resolved gameplay decisions.

Phase C may now proceed in plan order without a motion-model redesign. Step 16 owns live solver
and per-object-policy adoption; Step 17 owns production falling/race chronology; Step 18 owns the
tap shield and the live guarded composition (landed 2026-09-12); Step 19 owns the one-shot charge
(landed 2026-09-12); the remaining steps own visuals and controls. Approval does not itself
implement or verify those
steps, change the 400 Hz clock or representation, certify native capacity/performance, or authorize
push/deployment. Native evidence remains required for Step 24 performance/release claims.

## Step 17 implementation refinement, 2026-09-11

Ground attachment is explicitly selected by normal player construction; generic bodies and existing
crossing hazards default floating. Competitive support triggers bind only while running; Sandbox's
bind in every phase, preserving its automatic lobby/countdown/running progression. Sandbox declares
the existing shared respawn lifecycle with required
[sandbox] respawn_delay_seconds (initial two-second tuning matching hill/race, not a newly confirmed
owner value). Static bodies never fall. The canonical full-disc seating query is separate from
center-based falling and terrain-independent hill motion.

Unsafe checkpoint return discs fail at application startup with a named cross-value error. Occupied
supported points defer through the one existing lifecycle. Course publication moves before steering
to establish first-tick finish locks; narrow checkpoint facts preserve certified offsets through the
solver adapter. RaceStanding publishes normalized finished_tick_offset in [0,1], with no rounded
tie bucketing or tick-plus-fraction summation. Very large coincident courses can exceed the unchanged
motion-event work cap; that is explicit transactional refusal, never a partial finish or fallback.
See the [Step 17 contract](../reviews/2026-09-11-falling-race-return-contract.md).

## Amendment: tap shield and the live guarded composition, 2026-09-12 (plan Step 18)

The timed tap shield and the live guarded contact composition are implemented. This section
records what landed; the sections above carry the in-place dated corrections it makes true.

One required `[abilities]` section is authored for every application configuration, validated by
`gameplay::AbilityConfiguration` and carried into each mode through `GameModeConfiguration`:
`shield_duration_seconds=0.4`, `shield_perfect_window_seconds=0.08`, `shield_cooldown_seconds=0.9`,
`parry_stun_duration_seconds=0.6`. Converted once by the shared `duration_ticks` rule those are
160, 32, 360, and 240 ticks, matching § "Timed shield and perfect opening" exactly. **These are
this ADR's initial tuning assumptions, not owner-selected balance values.** Rounded shield, perfect,
and parry-stun durations must be positive and the perfect window may not exceed the shield;
cooldown may legally be zero or shorter than the shield, because admission requires both that prior
protection has ended and that the cooldown has expired, so a short cooldown cannot resurrect an
active shield. Convenience mode factories keep `AbilityConfiguration::defaults()`; the registry
path consumes the authored value.

Body-bound `simulation::Shield` holds three `TickWindow`s sharing one positive activation tick —
protection, perfect, cooldown — plus the parry-stun duration captured at activation, so the frozen
defender supplies the effect it actually owns and no contact-time reader needs the configuration.
Cancellation shortens only still-active protection: the shield window, and the perfect opening it
contains, both end at the cancellation tick, because an opening that outlived a cancelled shield
would still answer "perfect" to a contact response. An opening that has already elapsed is
untouched, and the original activation, the cooldown, and the captured duration never move. It
declares only the Step 13 body-bound lifetime trait.

The shared `ability` system runs **last at `kPreKernel`** in all four gameplay modes, mirroring
`StatusSystem`'s "last at `kPostKernel`" and satisfying race's constraint that `course_publisher`
publish the course before ability admission reads the canonical input lock. It erases a `Shield`
only when protection *and* cooldown have both expired, then admits at most one pulse per entity in
ascending `EntityId` order under the matrix in § "Charge, shield, and stun". A refusal changes
nothing at all: no cooldown consumed, no queued activation, no error, and no event. Queue
acceptance and a local send are not activation confirmation; the published `Shield` windows are
the positive proof of a committed activation, and this step adds no per-request negative receipt.

The perfect opening cancels a qualifying incoming opponent's velocity **and** acceleration and
stuns it for the **defender's** captured duration, taken from the defending body's committed
`Shield` rather than from a global. Ordinary protection with reduced knockback continues for the
rest of the shield window, without momentum cancellation or parry stun. The quarter-impulse target
stays initial tuning with the pair-level non-closing correction taking precedence, and this
amendment states no fifth version of that formula. Cliffs and zone rules bypass shield in every
phase.

One pair-symmetric row, `guarded_pair`, is declared above the built-ins by royale, king of the
hill, race, and Sandbox. Its adapter projects `GuardState` from the committed world — never from
the working subject bodies — forcing `kNone` for a static subject, and then calls the existing pure
`compose_guarded_pair` core with no second equation and no second timer arithmetic. Static/static
pairs never reach a row, so the symmetric predicate pair is safe for walls. The standalone
`lethal_hazard` row is deleted and its predicates, rationale, and diagnostic name are rehomed into
the composition. Engine-only modes that declare no row still reach the three built-ins; in the four
gameplay modes they are now unreachable.

`StatusSystem` cancels still-active protection on stun while preserving cooldown, the original
activation, and elapsed perfect history. Because responses read the frozen post-`kPreKernel` world,
that cancellation first bites on the next tick, which is the same-tick defensive grace
§ "Stun and input lifecycle" already documents — preserved here, not retracted.

Publication is complete in this commit: the `shield` component kind with exactly `activation_tick`,
`shield_expiry_tick`, `perfect_expiry_tick`, `cooldown_expiry_tick`, and `parry_stun_duration_ticks`;
the client-sendable `shield` command carrying only a required `input_generation` that is null or a
positive exact tick; schema, encoder, generated types, strict client validation, examples, and the
`docs/protocol/v3.md` row. The session major stays `3.0` and no close code is added.

**Not claimed here.** No charge field, key binding, or visual treatment landed: ~~charge is Step
19~~ — charge landed the same day, in the dated section below — visuals are Step 20, and controls
are Step 21. No new kernel policy socket, ninth mode declaration,
event root, second pair equation, command-receipt channel, or `Controller` capability was added.
This records implementation, not verification, native capacity, or release certification.
See the [Step 18 contract](../reviews/2026-09-12-shield-composition-contract.md).

## Amendment: the one-shot charge, 2026-09-12 (plan Step 19)

The one-shot charge is implemented. This section records what landed and the three judgements a
careless reader would get wrong; the sections above carry the in-place dated corrections it makes
true.

**Three keys join the same required `[abilities]` owner**, validated by the same
`gameplay::AbilityConfiguration` and reaching every mode through the same `GameModeConfiguration`:
`charge_cooldown_seconds=1.2` (480 ticks through the shared `duration_ticks`, and **validated
strictly positive**, unlike `shield_cooldown_seconds` — § "Charge" states why the shield's exemption
does not transfer), `charge_speed_fraction=0.75` (finite and strictly positive; a dimensionless
multiple of the *current* normal ceiling read from match state, never an authored speed, because the
ceiling is live-tunable), and `charge_safety_envelope_speed=20000` wu/s (finite, strictly positive,
at most `simulation::kMaximumPhysicalComponentMagnitude`). One cross-key rule joins them:
`charge_speed_fraction * kMaximumNormalTopSpeed <= charge_safety_envelope_speed`, so a charge from
rest stays admissible whatever a room tunes its ceiling to. Two of the three are doubles, which is
the first time this owner has stored anything but a tick count, and `AbilityConfiguration`'s own
canonical header was amended in the same commit rather than left to contradict itself.

**These three numbers are this ADR's initial tuning and an engineering guard, not owner-selected
balance values**, exactly as the four shield values are. The distinction matters most for the
envelope: what the owner accepted at Step 1 is *that a separate validated safety envelope exists*
(§ "Owner decisions, 2026-09-10"), and 20,000 wu/s is a first number nobody has chosen. It is also
not a balance option and not a UI control — § "Normal movement and web tuning" now says so in place,
where the promise was made.

**Body-bound `simulation::Charge` holds exactly one `TickWindow`, the cooldown.** Charge is
one-shot, so there is no active window and no captured effect parameter; a burst is applied and
gone. It publishes exactly `activation_tick` and `cooldown_expiry_tick` — both absolute endpoints,
never a countdown, because an absolute tick stays true in a frame a client buffered or received
late, which is the rule the shield encoder already states. The activation is published as well as
the expiry because a cooldown arc needs its denominator and `charge_cooldown_seconds` is
deliberately server-side. Because the cooldown is strictly positive, the schema and the encoder
require `cooldown_expiry_tick > activation_tick` **strictly**, where shield's ordering is `<=`. It
declares the Step 13 body-bound lifetime trait and nothing else, and only `AbilitySystem` removes
it, once the cooldown has expired.

**`simulation::ChargeCommand` is entity-addressed and carries a direction.**
`CommandKind::kCharge` is `1u << 11` with application rank 11, appended after shield's 10 so every
existing bit and rank is preserved. Its wire payload is `{x, y, input_generation?}` with the
generation **optional**, matching `set_thrust` rather than `shield`. Nothing behavioural turns on
that choice — the C++ value and the exact-optional-equality admission are identical either way — so
it follows the discriminator this tree has already written down three times: shield's member is
required-and-nullable because it has no other member and an optional one would make `{}` the whole
message, and each of those three statements contrasts it with `set_thrust`, which "carries x and y
whatever happens". Charge carries required `x` and `y`, so it is `set_thrust`'s shape. `x` and `y`
reuse `set_thrust`'s per-component `unit_interval_scalar` bound.

**The effect is an instantaneous additive velocity burst** of
`charge_speed_fraction × match.movement.current.normal_top_speed()` along the unit direction,
written through `PhysicsBody::with_velocity`. Additive, so lateral velocity survives, which is this
ADR's "never erases existing lateral velocity" made literal. It touches no acceleration, position,
radius, mass, or collision capability: there is no teleport, no invulnerability, no cliff bypass,
and a charging body meets every other body through the unchanged Step 18 composition and Step 16
motion. The envelope is checked in raw doubles — both prospective components finite, and
`sqrt(next_x² + next_y²)` at most `charge_safety_envelope_speed` — **before any `Vector2` is
constructed**, because a magnitude bound at or below the component domain keeps both components
representable and because a `Vector2` throw inside a `kPreKernel` system would stop the runtime
worker permanently.

**The accepted `kPreKernel` ordering residual.** `AbilitySystem` runs **last** at `kPreKernel`,
after `ThrustSteeringSystem`, in all four modes. On an activation tick the thrust limiter has
therefore
already sized this tick's acceleration against the *pre-burst* velocity, so the committed endpoint
is `v_pre + burst + a·dt`: one tick of already-certified propulsion stacked on top of the burst, at
most 1 wu/s at the authored 400 wu/s² and the 1/400 s tick. **This is accepted in writing rather
than reordered.** Running the ability system first would be worse, not better: the limiter's
`max(ceiling², v·v)` bound would then be computed against the *post*-burst velocity, and thrust
could sustain a charged speed indefinitely — a far larger violation of § "Normal movement and web
tuning"'s "above the normal ceiling, controls may brake/turn but must not add speed until back
within it" than one certified tick of ordinary acceleration. From the next tick on, that same `max`
term is what lets a charged body steer without amplifying or braking, which is the intended feel.

**What bounds repeated charges is the envelope, not drag.** At the shipped `drag_per_second=0` the
burst never decays, so a body that charges again and again in one direction accumulates
`0.75 × 600 = 450` wu/s per activation at the authored ceiling. From rest and with no other
propulsion, the forty-fourth activation reaches 19,800 wu/s and the forty-fifth is refused, because
20,250 wu/s would pass the 20,000 wu/s envelope. Repeated charges therefore converge on a bound
instead of growing without limit, and they do so long before the solver's per-tick motion-event work
cap is threatened. § "Normal movement and web tuning" carries the correction to the ADR's older
"decay under drag" sentence.

**Every refusal is a silent no-op**, on exactly the path every other ability refusal already takes:
no cooldown consumed, no queued activation, no event, no error, no receipt. This section's own
"refuse an inadmissible activation explicitly, not silently convert it" contrasts refusal with
*conversion*, and Step 18 already committed that a refused pulse produces no per-request receipt and
that the contract promises none.

**Nothing else moved.** No second system — the conflict between two abilities cannot be decided
without one place holding the priority, which is why the system has always been named `ability`
rather than `shield`. No new kernel socket, hook stage, event root, pair equation, policy socket,
mode declaration, or control binding; no `Controller` capability; no `StatusSystem` change; no
protocol version bump and no new close code. Visual treatment is Step 20 and key or button bindings
are Step 21, so no sender exists yet. This records implementation, not verification, native
capacity, or release certification.
See the [Step 19 contract](../reviews/2026-09-12-charge-contract.md).

## Amendment: cliff and combat rendering, 2026-09-12 (plan Step 20)

The client draws what Steps 18 and 19 published and deferred. Nothing on the wire changes: no
schema, encoder, component, field or server behaviour moves, and the whole step is verified by the
web gate alone.

**Cliffs are the boundary of the supported set, which is not the arena edge.** A cliff is the
boundary of *(envelope intersect the positive-ground union) minus the union of holes* -- the same
set `terrain_supports_point` computes. Two consequences the obvious reading misses. Corridor edges
are the only real cliffs on any runnable map: no shipped map or browser fixture authors a hole, so a
rim that covered only holes would leave both race maps' entire drop unmarked. And the envelope is
not a cliff at all: a hole edge kills, while the map edge folds a blob back under this ADR's own
outer-map decision, so the border stroke `SimulationCanvas` already draws from `configuration.world`
keeps its existing neutral treatment. Restyling it would both duplicate a geometry source and tell
the player the wrong thing about what happens there.

Both rims come from geometry the terrain layer already holds. Hole rims are stroked while the
accumulated complement clips are still live, so only the ground-side half survives and arcs interior
to an overlapping neighbour vanish with the clip -- the union outline, correct by construction.
Corridor rims are an underprint: the same polyline stroked at road width plus twice the rim, then
the road surface over it, with every rim pass preceding every surface pass. Rim width is authored in
world units and projected, so it scales with the camera rather than describing it.

**Void feedback is painted on the region, never on a body.** The plan's phrase "void feedback
beneath entities" reads naturally as a mark under a body whose centre is unsupported, and the client
must not draw that, for two independent reasons. Deciding it would require a second implementation
of the canonical support predicate including its asymmetric tolerances, which this ADR and that
function's own note both forbid. And the state never reaches a snapshot: a ground-bound body
terminates at its last supported point and loses its body on the same tick, so the only bodies ever
over void are `floating` hazards, which never fall -- a mark on one would be a confident lie.
Painting the unsupported region itself satisfies "beneath every entity layer" in the z-order sense
the layer stack already means, and needs no per-body verdict.

**Ability marks read the tick, not component presence.** `EntityRenderFrame` gains the snapshot tick
-- the step's one client-contract change -- because every ability window on the wire is a pair of
absolute ticks and a renderer that cannot see the tick can only draw presence. That reading is
explicitly forbidden by the published shield schema: a cancelled shield keeps publishing a component
whose protection has already ended. The shield mark therefore answers three states, not two, and the
third is the most common by duration: the perfect opening, ordinary protection, and no protection at
all while the cooldown still runs. Stun draws on the same rule. Charge stays deliberately undrawn in
world space -- its burst is already on screen as the velocity the body carries -- and its cooldown is
screen-space feedback.

**Two limits this step cannot exceed, recorded rather than papered over.** A perfect mark is a
*state* mark and never an event treatment with a designed duration: the opening is 32 ticks at
400 Hz against a 50 ms snapshot interval at the shipped cadence, so it is truthful whenever drawn
but can be missed entirely at a lower configured cadence. And the screen-space readout states
remaining time and never availability: charge readiness depends on the safety envelope, which is not
on the wire, and only the shield protection length that actually happened is published, never the
authored one.

This records the client contract, not a judgement that the result reads well. The web gate asserts
call sequences and geometry arguments; it does not look at pixels, and no shipped map authors a
hole, so the hole-rim path is exercised only by synthetic fixtures. Whether a cliff reads as a cliff
is an owner judgement on rendered images.

## Amendment: accessible ability controls, 2026-09-12 (plan Step 21)

A person can now press shield and charge. Nothing on the wire changes: the command kinds, their
admission and their published state all landed at Steps 18 and 19, and this step is verified by the
web gate alone.

**Shift is rejected as a shield binding, and the reason is mechanical rather than aesthetic.** The
canonical input owner's go-key guard filters `altKey`, `ctrlKey` and `metaKey` and deliberately does
*not* filter `shiftKey`, a choice an existing test pins, because Shift+Space must still thrust. A
bare-Shift shield would therefore fire on the leading half of every Shift+Tab a keyboard user makes;
five presses of it is the Windows Sticky Keys gesture; it is two codes against a
one-constant-per-action model; and modifier keys do not auto-repeat, so the key-repeat rule this ADR
requires could not be demonstrated against it at all. The bindings instead come from the pool this
ADR already reserved when Step 11a retired directional steering, which is the assignment that
paragraph anticipated.

**A pulse is not a held control, and reusing the held control's send path would break it.** The
ability activation shares the input owner's guards -- modifiers, composition, typing, camera gesture,
ownership, the input lock -- and shares its `sendCommand`, but not its change-only flush: that gate
would swallow a second identical pulse, its coalescing timer would delay one against an 80 ms perfect
opening, and its refusal latch is shared with held thrust. Each ability keeps its own one-shot latch,
cleared on keyup and on blur, because a blur swallows the keyup and the key would otherwise wedge.

**The client now rate-limits its own activations, because the boundary's answer to excess is a
disconnect rather than a refusal.** The per-session command bucket is a 30-token burst refilled at
20 per second, the token is charged before parsing, and an empty bucket closes the connection. Held
thrust with a moving cursor already runs at the full refill rate. So an activation is suppressed
entirely while the client can see a live published cooldown for that ability -- legitimate published
state, and the primary mitigation -- with a per-ability minimum send interval as the backstop for the
window between a press and the next snapshot. Nothing is lost by it: the mailbox coalesces same-kind
inputs, so extra same-tick pulses were never more than one attempt.

**Remembered aim survives a stun.** The last nonzero steering direction is retained across a
same-body generation or lock change; the discard set stays what this ADR contracts -- body loss, body
replacement, a new welcome, and disconnect. Before this step a stun silently wiped it, which would
have made charge unavailable for the frames after every parry.

**The client still never claims readiness**, on the rule Step 20 recorded: charge readiness depends
on the safety envelope, which is deliberately server-side and not on the wire. What a control may
state is what the client can see -- an unadvertised kind, no owned body, a phase that is not running,
an active stun, a live published cooldown, active shield protection blocking a charge, and for charge
the absence of any aim.

**One accessibility limitation is recorded rather than resolved.** Charge takes its direction from
the cursor aim or the last nonzero steering direction, and both exist only for a mouse pointer inside
the canvas. A touch, pen or keyboard-only player therefore has no aim, and charge stays unavailable
with its explanation showing. Shield is fully usable without a pointer. Closing that gap means
inventing a keyboard aim, which would partly reverse Step 11a's removal of directional steering and
is a decision for the owner, not an implementation detail of this step.
