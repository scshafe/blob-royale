<!-- canonical: deterministic_simulation_contract -- fixed-step physics semantics -->

# 3. Define the deterministic simulation contract

* **Status:** Accepted
* **Date:** 2026-08-04
* **Deciders:** Project owner

## Context and Problem Statement

The prototype treated velocity as displacement per tick, ignored acceleration and elapsed time, dropped equal-distance collision candidates, failed to reject separating overlaps, and let scheduler interleavings select observable results (`docs/PROJECT_DEEP_DIVE.md` § "Core simulation math and spatial indexing are not reliable"). The accepted ownership design makes `GameSimulation::step(FixedDelta)` the only mutable-world entry point and requires stable `EntityId` order, pure physics functions, and a deterministic serial reference (`docs/architecture/0002-simulation-architecture.md` § "Ownership and lifecycle"). The canonical `src/simulation` implementation and its tests apply this contract for units, time, phase order, collision precedence, numerical comparisons, and migrated fixtures.

This decision applies the general principles "explicit lifecycle over implicit," "separation of policy from mechanism," and "total functions over partial." Tick state and precedence are explicit; the spatial grid cannot determine physics policy; every finite, validated body state has a defined result, including coincident centers and wall overshoot.

## Considered Options

* **A. Exact fixed-step, discrete equal-mass discs with deterministic sequential response.** Use one exact simulation quantum, a fixed phase pipeline, stable pair order, frictionless elastic impulses, and reflected wall endpoints. This is the smallest policy that corrects the prototype's unit, tangential-velocity, separating-contact, equal-distance, and overshoot defects.
* **B. Variable elapsed-time integration with continuous collision detection.** Feed measured wall time into the simulation and solve time-of-impact events inside each tick. This handles tunneling, but makes scheduling jitter observable and requires a global event solver for multi-body and body-wall ties before the game has demonstrated that need.
* **C. Iterative simultaneous-contact constraint solving.** Solve all contacts as one coupled system until convergence. This gives more symmetric piles and multi-contact outcomes, but introduces convergence tolerances and iteration-order policy beyond the current moving-disc game.
* **D. Preserve the scheduler's nearest-collision and per-tick behavior.** Retain displacement-per-tick velocities and whichever equal-distance candidate survives the current map/interleaving. This preserves defects rather than a compatibility contract and conflicts with the deterministic serial architecture.

## Decision Outcome

**Chosen: Option A.**

### State, units, and fixed time

The simulation uses IEEE-754 binary64 values for every physical scalar. All accepted inputs and every phase result must be finite.

| Quantity | Unit and invariant |
|---|---|
| Position, radius, world width/height, cell dimensions | world-units (`wu`) |
| Velocity | world-units per second (`wu/s`) |
| Stored acceleration | world-units per second squared (`wu/s²`) |
| Mass | Every baseline player has the same unit mass; mass is not an input field. |
| Tick sequence | Unsigned integer, starting at zero for the loaded initial state and increasing by one only after a successful complete tick. |
| Simulation quantum | `FixedDelta = 2,500,000 ns = 1/400 s` exactly. |

`FixedDelta` is an exact duration value, not a measured floating-point elapsed time. `GameSimulation::step` accepts only this value in the accepted contract. The runtime may wake late and may execute catch-up steps as lifecycle policy, but it must never stretch, shrink, skip within, or combine simulation quanta. Simulated time after tick `N` is exactly `N / 400 s`. A different simulation rate changes observable behavior and requires this ADR to be amended or superseded.

World bounds describe the outer rectangle `[0, width] × [0, height]`. The original baseline used
the configured common radius `r` and committed centers in `[r, width-r] × [r, height-r]`.
**Step 16 amendment:** Each body's effective radius drives its wall planes, but startup/commit
admit the reviewed closed center envelope rather than requiring an inset center. Initial
radius overlap is legal and does not snap. A folded dynamic disc's diameter must fit each axis;
an exactly zero-span axis requires zero velocity. Static centers remain inside the closed map,
even with `kCross`; dynamic crossing bodies are exempt. Configuration still requires positive
common radius and dimensions larger than its diameter.

Acceleration is stored state. In phase 1 of every tick, each player uses only its already-stored acceleration:

`v_accelerated = v_previous + acceleration × (1/400 s)`.

Acceleration persists unchanged until a system writes it; the tick's validated `InputBatch` reaches
that system as recorded commands rather than as a direct kernel write (§ "Accepted simulation
input", § "Canonical tick"). Phase 1 then scales the accelerated velocity by the kernel's linear
drag factor (§ "Canonical tick"). The baseline performs no force calculation, acceleration cache
read, ambient input sampling, speed cap, gravity, or implicit reset.

"Player" in the rules below means a **dynamic body**: an entity carrying a `PhysicsBody` whose
`is_static` is false (ADR 0004 § "Entities, components, and stores"). A static body is never
accelerated, dragged, or integrated and is never folded into the arena interval; it participates
only as a subject of a contact. Every accepted rule stated for players holds unchanged for the
dynamic bodies that replace them, and this contract adds no second body type.

### Canonical tick

`GameSimulation::step(FixedDelta, const InputBatch&)` evaluates one tick in this exact order. The
numbered phases are kernel mechanism: no mode may reorder, skip, replace, or add one. Between them
sit three named hook stages holding the mode's declared systems. As amended in Step 16, the kernel
has three policy sockets: `SpawnPolicy` at intake, and `ContactRuleTable` plus
`MotionTriggerTable` inside continuous motion (ADR 0004 § "The tick: one fixed kernel, three named stages").

0. **Apply the tick's validated `InputBatch`.** Apply the batch in this order — despawns, then
   spawns, then the remaining commands — and each group in ascending `EntityId` order. A despawn
   destroys its entity, erasing it from every component store, before any pair is built. A spawn
   creates its entity from the tick's `EntityIdReservation` and leaves it unseated; the engine's
   `SpawnSystem` then walks unseated entities in ascending `EntityId` order, offers each the
   world-owned rotation counter and the set of free spawn markers, asks the mode's `SpawnPolicy`
   for an index into the map's `spawn` markers, and performs the seating write
   (ADR 0004 § "Game modes and the match lifecycle", § "Maps as data"). A policy that returns no
   index defers that entity, which is offered again in the same ascending order on a later tick;
   royale's rotating ring and the match phases it seats in are that mode's
   (ADR 0005 § "Spawning").
   The occupancy test reads the positions committed at the start of this tick together with the
   markers already seated in this phase, so two entities are never seated in contact and a map with
   no free marker defers rather than overlaps. Every remaining command is recorded into its
   entity's `Controllable::commands_this_tick`; the kernel does not interpret it, because command
   meaning is a system's job (ADR 0004 § "Commands"). The batch arrives canonical (§ "Accepted
   simulation input"): the kernel neither sorts, deduplicates, range-checks, nor reads an ambient
   queue. Velocities are unchanged, and the positions this phase leaves are the tick's
   start-of-tick positions that phase 2 and § "Player-pair policy" read.

**Hook stage `kPreKernel`.** The mode's `kPreKernel` systems run here, each once, in the order the
mode declared — never in registration, allocation, or static-initialization order
(ADR 0004 § "The tick: one fixed kernel, three named stages"). They read start-of-tick positions and
this tick's recorded commands and write body intent; nothing has moved yet. A mode's steering system
is what turns a recorded thrust into stored acceleration: it scales the command's unit-clamped
direction by its own declared maximum and writes `PhysicsBody::acceleration`, which then persists
until that entity's next thrust (ADR 0004 § "Commands"; ADR 0005 § "Mode configuration"). The kernel
neither supplies nor validates that scale, and a mode that declares no system leaves this stage
empty.

**Amended 2026-09-12 (plan Step 18):** steering is no longer the only kind of consumer here. The
shared `ability` system is declared **last** at this stage by all four gameplay modes and reads the
same recorded commands, but writes a component — `Shield` — rather than body intent. That is still
"read this tick's commands before anything has moved", and it is deliberately at this stage rather
than a fourth one: a shield activated on tick N must be visible to tick N's own contact responses,
which read the frozen post-`kPreKernel` world. Race's ordering constraint is the sharper case: its
`course_publisher` stays first so that the canonical input lock can see a published course, and the
ability system is declared after it.

**Amended 2026-09-12 (plan Step 19): a `kPreKernel` system now writes velocity, not only
acceleration and components.** The same shared `ability` system applies the one-shot charge as an
instantaneous **additive** velocity burst through `PhysicsBody::with_velocity`, before phase 1 has
run. Three consequences are worth stating rather than leaving to be discovered.

*The sentence above about steering is still exactly true of steering.* A steering system writes
`PhysicsBody::acceleration` and nothing else, and the kernel still neither supplies nor validates
its scale. What has changed is that "write body intent" was never the stage's rule — "read this
tick's commands before anything has moved" is — and a burst applied here is integrated by phase 1
in the ordinary way, with no charge-only substep, no second integrator, and no exemption from drag.

*The burst is additive, so it composes rather than replaces.* Lateral velocity survives a charge,
which is the property ADR 0008 requires of the move, and it is a property of the write rather than
of a special case: the system reads `body.velocity()` **before** the write, because the join hands
out a reference into the `PhysicsBody` store and the write mutates the referent even though
`insert_or_assign` on an id the store already holds assigns in place — the same aliasing rule the
steering system already records.

*Within-stage order produces one accepted residual.* `AbilitySystem` is declared after
`ThrustSteeringSystem`, so on an activation tick the propulsion limiter has already sized this
tick's acceleration against the **pre-burst** velocity and the committed endpoint is
`v_pre + burst + a·dt` — one tick of already-certified propulsion on top of the burst. That is
accepted deliberately; § "Amended 2026-09-12: The one-shot charge (Step 19)" below states why the
opposite order is worse. Nothing here weakens the ordering rule itself: within-stage order still
comes from the mode's declared list, and all four modes declare the same one.

1. **Apply stored acceleration and drag.** Visit players in ascending `EntityId` order, calculate
   accelerated velocity with semi-implicit Euler, then scale that velocity by
   `max(0, 1 - drag_per_second × drag_scale × dt)`, where `dt` is the same `kFixedDeltaSeconds` the
   acceleration step already uses and `drag_scale` is the body's own `PhysicsBody::drag_scale`.
   `drag_per_second` is a `[simulation]` kernel parameter, not mode configuration: drag is mechanism
   this contract owns, the kernel applies it here and applies it identically under every mode, at
   each body's own declared scale, and no system may reproduce or bypass it. The kernel reads that
   scale from the body exactly as phase 3 reads a mass; a body that declares none carries `1.0`,
   and multiplication by `1.0` is exact in binary64 for every finite value, so the accepted
   arithmetic is unchanged. `drag_per_second` and `drag_scale` must each be finite and
   non-negative, neither is bounded above, and the clamp at zero keeps the factor total when their
   product with `dt` exceeds one, so a large configured drag stops a body rather than reversing it.
   Positions do not change.
   **Amended 2026-09-12 (plan Step 19): this phase is the only thing that ever reduces a speed the
   kernel did not ask for, and in the shipped configuration it reduces nothing.**
   `config/blob-royale.cfg`
   authors `drag_per_second=0` — as does every replay fixture but `royale-drag-decay` — and at zero
   the factor is exactly `1.0`, so an externally imparted speed persists indefinitely. **Nothing
   anywhere clamps it.** The normal movement ceiling is a *propulsion* limit applied by a mode's
   steering system to the acceleration it is about to store (ADR 0008 § "Normal movement and web
   tuning": "do not clamp the whole velocity after every bounce"), the solver certifies impacts
   rather than bounding speeds, and no phase re-reads a velocity to trim it. A charge burst, a
   collision impulse, and a knockback are all the same kind of value to this contract: whatever put
   the speed there owns bounding it. For charge that owner is the authored
   `[abilities] charge_safety_envelope_speed`, checked before the burst is applied and not after —
   which is why repeated charges at zero drag converge on that envelope instead of growing without
   bound. A reader who assumes drag will eventually absorb an ability's output is reading the
   deployment configuration, not this one.
2–5. **Resolve continuous motion (amended 2026-09-11, Step 16).** Freeze the working world after
   phase 0 and `kPreKernel`; phase 1's accelerated/dragged subjects are separate values. The one
   `solve_continuous_motion` driver owns swept broad-phase candidates, certified touches/optional
   closing impacts, walls, declared unary triggers, actual path segments and terminal bodies.
   It does not consume the endpoint grid's candidate list. Both collision-mask directions must
   admit a pair. Events are ordered by exact stored `MotionTime`, then support loss, body contact,
   wall x, wall y, checkpoint, then canonical entity/feature identity. Only equal-time events use
   that priority: an earlier wall precedes a later pair. Canonical pair IDs and first-matching
   contact-table row/orientation remain deterministic. Responses read the immutable kernel-entry
   world and current subjects, may replace velocity/acceleration and motion disposition, and emit
   typed effects without world mutation. A terminated body causes no later contact or trigger in
   this quantum. Only successful solver effects enter `WorldEvent`; selected-certificate diagnostics
   are not gameplay events. Bounded work/storage/precision failure rejects the entire tick, never
   falling back to discrete motion or publishing a partial result. Production support/race trigger
   registrations follow in Step 17; Step 16 proves the declaration seam with injected policies.
6. **Rebuild the spatial grid.** Discard old membership and deterministically rebuild it from the new positions in ascending `EntityId` order.

**Hook stage `kPostKernel`.** The mode's `kPostKernel` systems run here, each once, in declared
order. They read committed positions, the index phase 6 rebuilt, and this tick's `WorldEvent`s, and
write consequences — score, damage, elimination marks, pickups, captures. Royale's safe zone and its
elimination rule are two such systems and belong to that mode, not to this contract
(ADR 0005 § "Safe zone", § "Elimination and placement").

**Hook stage `kLifecycle`.** The mode's `kLifecycle` systems run here in declared order, and then
the engine-owned lifecycle system runs last and is not removable
(ADR 0004 § "Game modes and the match lifecycle"). It evaluates the mode's objective against the
tick's final world and commits **at most one `MatchPhase` transition per tick**. That bound is what
keeps the machine total: a mode whose durations are all zero advances exactly one phase per tick and
terminates, rather than chaining transitions inside one tick. A committed transition writes
`MatchState` — the phase, its start tick, the running start tick, and the committed `MatchOutcome` —
and nothing else; a consequence a mode wants from a transition is that mode's own system, ordered
ahead of the lifecycle system by declaration (ADR 0005 § "Match lifecycle").

10. **Commit.** In this fixed order: apply the roster removals named by this tick's `DespawnEvent`s,
    destroying each from every component store; reject any non-finite or out-of-bounds result among
    the bodies that survive as a hard simulation failure; canonicalize signed zero to positive zero;
    rebuild the spatial index from the roster and positions that survive; clear the `WorldEvent`
    list; replace the committed world state; increment the tick sequence once; and make that
    complete tick eligible for snapshot publication. Removal precedes validation because an entity
    that a system both pushed out of bounds and marked for removal in the same tick is a removal,
    not a failure: a body eliminated at the arena edge is exactly that pairing, and validating first
    would stop the match on the tick a mode legitimately deletes the offending body. Validation
    therefore judges only what the tick actually commits. Because removal precedes validation,
    validation precedes the rebuild, and the rebuild precedes publication, no committed grid holds a
    non-live `EntityId` and no snapshot observes a half-applied removal. Events are tick-local and never
    appear in a snapshot (ADR 0004 § "World events"); a consequence that must outlive the tick was
    already written into a component or into `MatchState` by the system that decided it. A rejected
    tick commits nothing at all: the world, the roster, `MatchState`, and the tick sequence remain
    exactly what the previous commit left.

No snapshot or tick sequence is committed from an incomplete phase. The serial order above is the observable reference even if a later implementation parallelizes internal calculation (`docs/architecture/0002-simulation-architecture.md` § "Extension points").

Phases 1 through 6 keep their accepted content and their accepted numbers, and the accepted phase 7
is now phase 10. The numbers 7 through 9 are deliberately left unused. They are the gap where zone,
elimination, and match-transition phases would sit had gameplay landed inside the kernel, which is
the shape ADR 0004 considered and rejected; those rules are a mode's declared systems at
`kPostKernel` and `kLifecycle` instead. Keeping the gap is what lets ADR 0004 § "The tick: one fixed
kernel, three named stages" and this section name the same commit phase without either document
renumbering the other.

Every phase and every stage reads `tick_sequence` as the value this tick commits at phase 10, one
greater than the last committed sequence, and `TickContext` exposes exactly that value
(ADR 0004 § "The tick: one fixed kernel, three named stages"). A duration comparison, a shrink
fraction, or a recorded elimination tick therefore names the tick that publishes its result, so the
snapshot for tick `N` is internally consistent. `TickContext` exposes no clock and no `InputBatch`,
so no system can read a wall time or observe a half-applied batch.

Two points in a tick change the roster: phase 0, which despawns and seats, and the commit, which
applies this tick's `DespawnEvent`s. `SpatialGrid` is a derived index
(§ "Spatial-grid policy and partition boundaries"), so each of them leaves the grid equal to a phase
6 rebuild over the roster and positions in force when that point ends — phase 0 over the post-batch
roster at the start-of-tick positions, with a seated entity indexed at its marker, and the commit
over the surviving roster at the committed positions. An implementation may restore that equality by
inserting and removing the affected `EntityId` values rather than rebuilding, provided cell
membership and ascending in-cell order are identical either way. No tick therefore commits a grid
holding a non-live `EntityId`, and when the batch is empty and no system emits a `DespawnEvent`,
neither point changes the grid at all.

**Built-in equations are retained; live admission and chronology change in Step 16.** The table
declares `variable_impulse`, `elastic_disc`, then `reflect_static`. Each consumes a certified
closing impact and calls the existing pure impulse equation. Ordinary equal-unit-mass bodies
still bypass the general equation. Grazing observations cannot fabricate impulses; per-source
`any_touch` admission permits that source's gameplay effect without changing the physical mask.
Arena walls are solver-owned plane events, not contact-table rows.

**Amended 2026-09-12 (plan Step 18): the three built-in rows are no longer reached in a gameplay
mode, and the equations are still retained.** Royale, king of the hill, race, and Sandbox each now
declare one pair-symmetric `guarded_pair` row above the built-ins, and `first_match` takes the
first matching row, so a production pair reaches the composition instead of `variable_impulse`,
`elastic_disc`, or `reflect_static`. An engine-only mode that declares no row — the kernel default
and the simulation-domain test doubles — still reaches all three, which is what keeps them a live
baseline rather than dead code. The honest statement of what did and did not change is:

* **The dynamic/dynamic selection is unchanged.** `compose_guarded_pair` chooses
  `resolve_player_pair_collision` when `body_has_baseline_physics` holds for **both** bodies and
  `resolve_general_pair_collision` otherwise (`src/gameplay/shared/guarded_pair_contact.cpp`), which
  is the same partition the built-in predicates make: `variable_impulse` is declared first and its
  first predicate is `body_is_variable_dynamic` — dynamic and *not* baseline — so any pair with a
  nonbaseline side already took the general equation and every all-baseline pair fell through to
  `elastic_disc` (`src/simulation/contact_rule_table.cpp`). Same two accepted equations, same test,
  same operand order.
* **The static branch reflects the same side, with one extra guard that cannot fire on the live
  path.** `reflect_static_response` reflects whenever `observation.impact` is present; the
  composition reflects whichever side is non-static, but only under
  `!non_closing(contact.relative_normal_speed())`. On the live path that guard is a re-check of a
  test that has already passed and can change nothing, because the solver constructs the `impact`
  certificate as `impact_geometry_admitted && closing(speed)` and then stores *that same* `speed`
  in the contact it hands the row (`src/simulation/continuous_motion.cpp`); `non_closing` and
  `closing` are the same comparison against `kVelocityTolerance`, so an existing `impact` implies
  the guard is satisfied. What the guard can change is the answer for a **direct caller of the pure
  core** that synthesizes an observation whose `impact` never passed admission — the unit tests do
  exactly that — where the composition declines to reflect a separating pair that
  `reflect_static_response` would have reflected. No live wall, replay, or fixture value moves
  through it.
* **Unguarded arithmetic is byte-for-byte.** When neither side carries a guard the composition
  returns the selected base equation's output unchanged, including its restitution-zero residue;
  the quarter projection and the separation correction run only when a guard is present.
* **Diagnostics change.** A composed non-lethal contact now reports `rule_name == "guarded_pair"`
  where it previously reported `elastic_disc`, `variable_impulse`, or `reflect_static`; the lethal
  branch still reports `lethal_hazard`. `ContactEvent`s are tick-local and never enter a snapshot,
  so no accepted snapshot or replay oracle carries a rule name.

**The discrete baseline remains a frozen oracle, not a universal live equivalence claim.** The
original framework proved bit identity with the seven-phase kernel under zero drag, empty input,
and no-writing systems. Step 16 deliberately supersedes that claim: continuous chronology catches
between-endpoint crossings and can change contact order, collision positions, and subsequent
trajectory. Pure legacy wrappers and frozen equation tests remain unchanged; live tests identify
continuity cases and explain each intended divergence. Historical fixture descriptions and dated
amendments below describe their original contract unless explicitly superseded here. Arbitrarily
large finite motion is no longer promised: the reviewed representability and work budgets fail
transactionally instead of falling back to discrete motion.

### Player-pair policy

**Amended 2026-09-11 (Step 16):** Live admission is the reviewed continuous certificate path,
not the legacy proximity detector described historically below. Effective radii are summed per
pair. The canonical circle polynomial and exact-sign topology establish a closed geometric touch
and, separately, optional closing impact. Distinct centers retain their certified geometric
normal; only exact coincidence uses the deterministic relative-velocity fallback. A frozen
per-object source policy defaults to closing impact; sparse `any_touch` also admits grazing,
stationary and overlapping effects. Touch alone cannot grant an impulse. Contact-taking physics
overloads consume the certificate without re-detection; swapping negates normals and swaps source
eligibility/results while preserving certified distances and relative normal speeds.

The equal-unit-mass exchange equations below and general/static response equations remain the
canonical physical equations. The legacy detector wrappers and their frozen tests remain truthful
diagnostics, not a second live solver. The following proximity-band bullet definitions describe
those retained wrappers, not live hit membership.

The narrow phase models frictionless, perfectly elastic collisions between equal-radius, equal-unit-mass discs. This section defines the `elastic_disc` row, which is the response for a pair of ordinary bodies and is unchanged by the 2026-09-07 amendment below; a pair in which either body declares its own mass or restitution is answered by the `variable_impulse` row above it and never reaches this equation. For canonical pair `(a, b)`, where `a.id < b.id`:

* Let `d = p_b - p_a`, `distance = |d|`, and `contact_distance = 2r`.
* The pair is in contact when `distance <= contact_distance + ε_position`. A larger separation produces no change.
* When `distance > ε_position`, the contact normal is `n = d / distance`, directed from `a` to `b`.
* When the centers coincide, use `n = normalize(v_a - v_b)` if the relative velocity magnitude exceeds `ε_velocity`; otherwise use the deterministic fallback `n = (1, 0)`. This rule prevents division by zero and never consults container or thread order.
* Let `relative_normal_speed = (v_b - v_a) · n`. When it is greater than or equal to `-ε_velocity`, the pair is stationary or separating and receives no impulse, even when it overlaps.
* Otherwise exchange only the normal velocity components:

  `v_a' = v_a + ((v_b · n) - (v_a · n)) n`

  `v_b' = v_b + ((v_a · n) - (v_b · n)) n`.

Tangential components remain attached to their original players. Each applied impulse therefore preserves pair momentum and kinetic energy within the floating-point tolerance. The baseline performs no penetration correction. An overlapping separating pair keeps its velocities and integrates apart; an exactly coincident pair with equal velocities remains coincident without producing an arbitrary impulse.

Live observations are consumed at both bodies' post-response revisions, including no-op touches.
An external trajectory change can re-enable a pair within the same bounded quantum. Later events
observe prior motion responses; equal-time pairs use canonical IDs, not distance or discovery order.
This is a chronological sequential solution, not a simultaneous constraint solve.

The live solver resolves certified within-tick crossings at their event times; it no longer delays
first contact until the next tick or ignores a pair that crosses between committed positions.
Initial-overlap/revision repeat semantics and fail-visible representability limits remain those
accepted at Step 5, not an exact-real or unbounded-capacity guarantee.

### Wall policy

**Amended 2026-09-11 (Step 16):** Live walls are certified plane events in the same chronological
solver as pairs, using each body's effective radius. The retained triangular-fold helper described
below is historical/discrete diagnostic behavior, not a live alternate path. Reflection preserves
the normal-axis speed and tangential component, with x before y only at equal times. No endpoint
snapping or depenetration is added. Static centers must lie in the closed map envelope, even if
flagged crossing. Dynamic crossing bodies have no wall events. Other dynamic centers must be in
the closed envelope and their diameter must fit both axes; a zero-span axis requires zero velocity
on that axis. Initial radius overlap with a wall is legal. One promoted body-envelope guard is
shared by solver, startup, grid admission and commit; stricter spawn clearance remains separate.

The following fold description is retained for the standalone legacy helper and its exact tests:

Wall resolution is frictionless and perfectly elastic per axis. It preserves speed magnitude on the reflected axis and leaves the other component unchanged. It considers the complete proposed motion for the tick, so a committed result never remains outside the center interval and high-speed overshoot cannot tunnel through a wall.

For one axis with permitted center interval `[lower, upper]`, let `span = upper - lower` and `q = position + velocity × FixedDelta`. Fold `q` into the interval using a triangular reflection with period `2 × span`:

1. Reduce `q - lower` modulo `2 × span` into `[0, 2 × span)`.
2. Values in `[0, span]` map to `lower + value`; values in `(span, 2 × span)` map to `upper - (value - span)`.
3. Set the terminal velocity sign from the final reflected segment. An endpoint at `lower` is assigned non-negative velocity; an endpoint at `upper` is assigned non-positive velocity. Zero remains zero.
4. Snap a folded endpoint within `ε_position` of a wall to that exact wall value.

Exact endpoints take precedence over tolerance snapping. In a valid interval so small that a
non-exact folded endpoint is within tolerance of both walls, snap to the nearer wall; an exact
distance tie selects the lower wall. This keeps the result total and deterministic without moving
an exact upper-wall state to the lower wall.

The tick-local displacement is `folded_endpoint - position`; phase 5 applies it while retaining the terminal velocity from phase 4. This defines one bounce, multiple bounces, starting exactly at a wall, and arbitrarily large finite overshoot without iteration-count behavior. If x and y contacts occur at the same simulated instant, x resolves first and y second. The independent-axis result is the same, but the order governs diagnostics and any future non-axis-aligned extension. If a player-pair contact and wall contact occur in the same tick, the canonical pair response precedes the wall response.

### Spatial-grid policy and partition boundaries

As of Step 16 this remains the endpoint/stage query index. The continuous solver owns its swept
candidate generation separately; it never treats endpoint-grid candidates as the whole sweep.
Stage invalidation compares body identity, position and radius because all affect indexed coverage.

`SpatialGrid` is a broad-phase index, not physical state. It must produce a superset of all pairs whose committed discs can touch; the narrow phase alone decides contact.

Cells are ordered row-major by `(row, column)`. Their geometric regions are half-open on internal maximum edges and closed at the world's outer maximum. An exact internal boundary belongs to the cell on its positive side for a center/home-cell calculation. For collision coverage, insert each `EntityId` into every cell intersected by the disc's closed axis-aligned bounding box, sized at that body's own effective radius -- its declared radius, or the configured radius when it declares none. Sizing coverage at the configured radius instead would break the superset requirement for any body larger than it: the pair would never be offered at separations where the two discs genuinely overlap, so the contact would not be deferred to a later tick, it would never happen. A bounding-box edge exactly on an internal cell boundary counts as intersecting both adjacent cells. IDs inside each cell are ascending; candidate pair keys are deduplicated and sorted as specified above.

Consequently, moving a fixture or contact onto a row/column boundary, a corner shared by four cells, or a non-divisible cell edge must not change whether the pair is detected, how often it resolves, or its physical result. Replacing the grid with an exhaustive all-pairs broad phase must produce the same canonical pair list after narrow-phase filtering.

### Floating-point contract

The implementation must preserve the written operation order and must not enable reassociation or fast-math transformations. It must not use unordered iteration, pointer order, thread completion order, or distance as a unique key. Tolerance affects comparisons and verification; it does not quantize stored state.

Use these quantity-specific absolute tolerances and one common relative tolerance:

| Quantity | Absolute tolerance |
|---|---|
| Position/distance | `ε_position = 1e-9 wu` |
| Velocity/speed | `ε_velocity = 1e-9 wu/s` |
| Acceleration | `ε_acceleration = 1e-9 wu/s²` |
| Dimensionless/conservation checks | `ε_scalar = 1e-9` |

`approximately_equal(a, b, ε) := |a - b| <= ε + 1e-12 × max(|a|, |b|)`.

Contact, separating, wall-snap, expected-value, and conservation comparisons use the tolerance for their quantity. `EntityId`, tick sequence, pair ordering, collection size, and fixture identity use exact equality. Fixture values intended to lie strictly on one side of a decision threshold must be at least `10 ×` the applicable tolerance away from it; fixtures for an exact threshold must name that boundary and assert this ADR's inclusive rule.

The same executable, configuration, and fixture must produce bit-identical ordered snapshots across 100 fresh runs. GCC-versus-Clang and other accepted-toolchain comparisons require exact IDs/order/ticks and `approximately_equal` physical components plus the stated physical invariants; accidental cross-toolchain bit identity is not required. A NaN or infinity at input or after any phase is a hard error, never a comparison miss or silent clamp.

### Fixture contract and expected outcomes

The table and three seed horizons below retain the historical discrete contract. Step 16's live
tests supersede its one-evaluation-per-pair, next-tick-contact, high-speed-crossing, and universal
baseline-preservation rows with certified continuous chronology. The legacy wrapper/oracle still
tests those historical rules independently; changing live expectations must name the physical
reason, not rewrite that oracle. Stable input values, deterministic repeatability, conservation,
canonical identities, stage ordering, and unchanged gameplay timing remain requirements.

Simulation fixtures are specifications, not recordings of the prototype. Each fixed-tick case must declare the accepted simulation quantum, world dimensions, common radius, explicit unique `EntityId` values, initial position/velocity/stored acceleration, tick count, and either expected ordered snapshots or named invariants. Row order may not provide runtime precedence; IDs do. The migrated CSV seeds use explicit IDs assigned once in their prior row order and interpret all velocity and acceleration numbers in the units above. They do not multiply values by 400 to imitate the prototype's per-tick displacement accident.

| Case | Required expected outcome |
|---|---|
| Stored acceleration | With no contact, `v_after = v_before + acceleration / 400` and the tick displacement uses `v_after / 400`; acceleration remains stored unchanged. |
| Head-on | Two touching, approaching players exchange their normal velocities. Equal-and-opposite velocities reverse; total momentum and kinetic energy remain within tolerance. |
| Oblique | Only normal components exchange. Each player's tangential component is unchanged; pair momentum and kinetic energy remain within tolerance. |
| Separating overlap/contact | Velocities receive no impulse. The players integrate along their existing separating motion. |
| Simultaneous/equal-distance | Preserve every contact. Resolve lexicographic pair keys once each; later pairs observe earlier velocity results. Repeating with shuffled CSV rows produces the same ID-ordered snapshot. |
| Exact pair contact after integration | The contact does not affect the tick whose integration first reaches it. It resolves at the pair phase of the following tick if the players are then approaching. |
| Wall overshoot | Fold the complete endpoint into the valid center interval and set the reflected terminal direction. One- and multiple-wall overshoots finish in bounds without changing axis speed magnitude. |
| Wall/corner tie | An exact lower/upper endpoint points inward after resolution. A simultaneous corner hit reflects both components under x-then-y precedence. |
| Partition boundary | A touching pair split by an internal edge or corner appears exactly once and resolves identically to the exhaustive all-pairs reference. A non-colliding cell transition changes neither velocity nor continuous position. |
| High-speed player crossing | When two players do not overlap at either committed pair phase and cross only during integration, the baseline produces no player-pair impulse. This pins the discrete limitation instead of leaving it implementation-dependent. |
| Thrust integration | A thrust recorded in phase 0 becomes stored acceleration when the mode's `kPreKernel` steering system reads it; the kernel neither writes nor scales it. With no contact the velocity then changes by `a / 400` on every following tick and the acceleration persists until that entity's next thrust command, including across ticks whose batch is empty. Under a mode that scales a unit-clamped direction by a declared maximum, `(1, 1)` yields an acceleration of exactly that magnitude and `(0, 0)` stores zero. |
| Drag decay | With nonzero `drag_per_second`, zero stored acceleration, and a body at the default `drag_scale` of `1.0`, velocity is multiplied by `max(0, 1 - drag_per_second / 400)` every tick and decays geometrically. A body declaring another scale substitutes `drag_per_second × drag_scale`, and one declaring `0.0` keeps its velocity exactly, because the factor is then exactly `1.0`. Under a constant stored acceleration `a` the sequence converges to the discrete fixed point `a × (1 - drag_per_second × dt) / drag_per_second`, not to the continuous-limit `a / drag_per_second`. A `drag_per_second × dt` above one stops a body at zero and never reverses it. |
| Spawn slot order | The engine `SpawnSystem` seats unseated entities in ascending `EntityId`, each at the marker index the mode's `SpawnPolicy` returns for the world-owned rotation counter and the free-marker set. A policy that returns no index defers that entity and seats nobody in its place. Submitting the same spawns in a different order produces the same seating, because the batch is canonical and seating order is ascending `EntityId` rather than arrival order. |
| Despawn of a pending pair member | A despawn removes its entity in phase 0, before phase 2 builds the pair list, so no candidate pair in that tick names it. A partner that would otherwise have been in contact receives no impulse on that tick and integrates unchanged. An entity removed instead by a `DespawnEvent` leaves the roster at the commit of the tick that emitted it. In both cases the committed grid holds no removed `EntityId`. |
| Match transition | At most one `MatchPhase` transition commits per tick. A mode whose durations are all zero advances exactly one phase per tick and terminates instead of chaining `lobby → countdown → running → ended → lobby` inside one tick. The transition observes the tick's final world, after every `kPostKernel` and `kLifecycle` system has run. |
| Baseline preservation | With `drag_per_second = 0`, an empty `InputBatch` on every tick, and a mode whose systems write nothing, every row above this one produces the same tick horizons, the same ordered bodies, and the same bytes as the accepted seven-phase baseline. Under `royale` the same holds for any fixture that never requests a start. This row is the regression that proves the framework additive. |

Royale's own outcomes are not rows here. Zone shrink timing, the elimination grace count,
simultaneous elimination, placement order, and the draw are mode rules, verified by the replay suite
[`0005-royale-mode.md`](0005-royale-mode.md) declares — ADR 0005 § "Safe zone",
§ "Elimination and placement", and § "Match phases". Restating them in this contract would create a
second place for them to be wrong. What this contract owns for them is the stage each runs at, the
tick sequence it reads, the roster it observes, and the determinism obligations below.

The migrated fixture seeds have these accepted expectations:

* `tests/fixtures/partition-trace-test.csv` is an initial-state seed. With zero acceleration and before any wall contact, its player follows `p_N = p_0 + N × v / 400`; crossing any grid boundary must match the no-grid reference.
* `tests/fixtures/player-on-player-collision-test.csv` is an initial-state seed. Its first two players begin 40 wu apart and close at 5 wu/s. They first reach exact 20 wu contact after 1,600 ticks without an impulse on that tick, then exchange their y velocities during tick 1,601.
* `tests/fixtures/player-on-wall-collision-test.csv` is an initial-state seed. With radius 10, its first player reaches the lower x wall from `x = 15` at `1 wu/s` after 2,000 ticks; that committed tick has `x = 10` and an inward x velocity of `+1 wu/s`.

The CSV files contain initial state rather than authoritative output recordings. Executable fixture and simulation tests assert their shapes and accepted tick horizons. Malformed rows, duplicate IDs, non-finite values, invalid bounds, and unsafe dimensions are loader rejection fixtures, not simulation ticks.

**Determinism of gameplay state.** Every gameplay duration is an integer tick count converted once
at configuration load, and no system reads a wall clock or a measured elapsed time — `TickContext`
names no clock type, so this is structural rather than a discipline
(ADR 0004 § "Determinism obligations for framework code"). Every gameplay tie resolves by ascending
`EntityId` — command application within a batch, spawn seating, component-store iteration, event
production, and any list a system appends to — so no gameplay ordering is left to container,
allocation, registration, or thread order. The only randomness a tick may read is
`DeterministicRandom`, seeded once and owned by `GameWorld`, whose `draw_count` every snapshot
commits, so two runs that diverge in how many draws they took diverge visibly at the first differing
tick. The 100-fresh-run rule of § "Floating-point contract" therefore extends unchanged to gameplay:
a scripted match replayed from `(map, mode configuration, seed, command log)` must produce
bit-identical ordered snapshots across 100 fresh runs, including every component store, the match
phase, its start ticks, the committed outcome, and the mode's own published state. Controllers are
not part of that guarantee and do not need to be, because replay replays the recorded command log
rather than the deciders that produced it (ADR 0004 § "Controllers").

### Accepted simulation input

`GameSimulation::step(FixedDelta, const InputBatch&)` is the only mutation entry point. The
simulation reads exactly one input value per tick, the batch passed to that call, and never an
ambient queue, callback, socket, clock, or global. A tick with no commands is that same call with an
empty batch, not a different code path.

`InputBatch` is a validated value produced by `InputBatch::create` (ADR 0004 § "Commands"). Creation
canonicalizes and rejects; phase 0 repeats neither:

* Each command kind is ordered by ascending `EntityId`, and the kinds are applied in the order
  § "Canonical tick" states.
* The last command of a kind for an entity wins. Earlier ones are discarded at creation, so a batch
  carries at most one command of each kind per entity.
* An entity may not both spawn and despawn in one batch. Such a batch is rejected at creation and
  never reaches `step`.
* Every component is finite and in range. Non-finite or out-of-range values are rejected at
  creation.
* A kind absent from the mode's `accepted_command_kinds()` is **rejected** at creation. The command
  boundary already refuses an unaccepted kind at submission, so one arriving here means the boundary
  and the engine disagree about the running mode, which is an internal invariant violation and not a
  value to drop quietly.

A malformed value that fails these rules is not a simulation failure, because it never becomes an
`InputBatch`. Phase 0 applies exactly what it is handed, in the order § "Canonical tick" states.

**The batch also carries the tick's `EntityIdReservation`.** One monotonic allocator outside the
simulation issues `EntityId` values and places a contiguous reservation in each tick's batch. Every
id the tick brings into existence — a session join and any entity a system creates — is drawn from
that reservation and from nowhere else, and exhausting it is a hard simulation failure rather than a
silent skip or a reused id. Because the reservation is part of the tick's input value, replaying the
recorded command log reproduces simulation-created entity ids exactly, which is what makes the
reproducibility tuple of ADR 0004 § "Determinism obligations for framework code" complete rather
than approximate.

Identity, ownership, authorization, tick addressing, rate limiting, and cross-session ordering are
decided outside the simulation, at the application and protocol boundary, before a batch exists
(`src/simulation/README.md` § "Extension points"; protocol v2, to be documented at
`docs/protocol/v2.md`). Every command source is a controller, and every controller decides outside
the tick from immutable snapshots — a networked player's session, an in-process bot, and a scripted
replay alike. No `SimulationSystem` may call one, and the simulation never learns which kind
produced a command, so a bot acts through exactly the path a human acts through
(ADR 0004 § "Controllers").

The simulation trusts the batch's shape and nothing beyond it. A command that disagrees with
committed world state — a spawn for an entity that already exists, a despawn for an entity that does
not, a command recorded for an entity that is not live — is ignored rather than failing the tick,
because the command source is a network session and a hard failure would let one client stop the
match (ADR 0004 § "Commands"). Ignoring is a stated total result, not a fallback: the tick continues
with the world it had. Internal invariant violations remain hard failures — a non-finite or
out-of-bounds result, an exhausted `EntityIdReservation`, an overflowing `WorldEvent` list — because
those are the engine disagreeing with itself rather than with a client. Prediction, rollback,
reconciliation, and input delay stay undefined here, and the prototype client's acceleration-shaped
message still creates no compatibility obligation (`docs/PROJECT_DEEP_DIVE.md` § "React client").

`@extension-point simulation_input` from `docs/architecture/0002-simulation-architecture.md` is
realized by this contract and is no longer documentation only. What it becomes is one fixed
signature and one validated value type, not an interface, queue, dispatcher, or registry: a second
command kind is a new member of the `Command` variant, its validation inside `InputBatch::create`,
and a consuming system at a stage — not a new kernel sub-step and not a new seam
(ADR 0004 § "Commands").

### Justified extension points and what-if stress

* **What if gameplay adds unequal radii or masses?** **Answered on 2026-09-07 for mass, restitution, and contact size; see the amendment below.** The shape predicted here was right -- a general-impulse `ContactRule` row declared ahead of `elastic_disc`, not a new seam (ADR 0004 § "Contact rules"), with canonical pair formation and the outer phases unchanged. The prediction that it "stays a versioned physics change because fixtures and snapshots can change" was the conservative reading and did not hold: fixtures and snapshots change only if the accepted equation is *replaced*, and declaring the general row above the baseline and predicating it on a body that actually differs from the baseline means an ordinary pair never reaches it. No fixture horizon, accepted snapshot, or oracle value moved. What remains versioned is the arena itself: phase 4's fold and the commit-time centre interval still use the configured radius, so a body of a different size may not yet *stand* anywhere an ordinary body may not. No strategy interface was added, because there is still one live policy per row.
* **What if fast players must not tunnel through each other?** Replace closed-disc overlap with swept candidate bounds and a deterministic time-of-impact event order inside phase 3. Equal-time events need a new explicit tie policy. The current pure collision boundary leaves an implementation path, but continuous behavior requires an ADR amendment rather than a silent improvement.
* **What if multi-player piles need symmetric outcomes?** A deterministic simultaneous-contact solver may replace sequential impulses inside phase 3. It must state convergence and equality semantics and version changed outcomes. Stable IDs remain the final tie-breaker.
* **What if another spatial index is faster?** Replace `SpatialGrid` behind `GameSimulation` only if it emits the same canonical candidate pair set. Grid shape and traversal remain non-observable mechanism (`docs/architecture/0002-simulation-architecture.md` § "Ownership and lifecycle").
* **What if runtime cadence changes?** Presentation rate and temporary scheduler lateness do not change `FixedDelta`. A different simulation quantum requires an amended contract and regenerated fixtures, and now also regenerated gameplay durations, because every mode duration is an integer tick count; measured wall time never enters physics or gameplay.
* **What if commands must be tick-addressed, predicted, or rolled back?** This contract accepts one validated batch applied at phase 0 of the tick that receives it, and nothing more. Client-side prediction, server rollback, reconciliation, and input delay each need a stated authoritative-tick addressing rule and a re-simulation contract before an implementation buffers or replays a batch. The batch value and the single mutation entry point are what such a policy extends; neither is a queue it can grow behind.
* **What if a new game rule must run inside the tick?** It becomes a system registered at one of the three hook stages of a mode's declared list, and the stage follows from what the rule must see: `kPreKernel` for body intent before anything has moved, `kPostKernel` for consequences of committed positions and this tick's contacts, `kLifecycle` for roster and match bookkeeping (ADR 0004 § "The tick: one fixed kernel, three named stages"). The kernel phases themselves are not a seam and take no registrations. A rule expressible at no stage, in no `ContactRuleTable` row, and in no `SpawnPolicy` — a fourth stage, a hook inside a phase, or a mode-supplied bounds fold — is a versioned change to this contract rather than an insertion into it. **Worked on 2026-09-12 (plan Step 18):** the tap shield is the case this answer predicted. It needed a per-tick rule that reads recorded commands, writes durable state, and is visible to the same tick's contact responses, and it became one `kPreKernel` system plus one contact-rule row declared by four modes — no fourth stage, no hook inside a phase, and no fourth policy socket.
* **What if simulation phases are parallelized?** Parallel code may calculate disjoint phase-local buffers, but it must commit the same serial-reference result. Sequential shared-body pair response cannot be reordered merely for throughput (`docs/architecture/0002-simulation-architecture.md` § "Extension points").

These are seams in the pure-function and phase boundaries, not plugin registries or polymorphic strategy classes. They each have at least two plausible future policies, while the accepted baseline retains one canonical implementation.

## Consequences

* **Positive:** Physical quantities have conventional units, so changing presentation or runtime cadence cannot change gameplay speed.
* **Positive:** Exact phase and ID precedence make repeated outcomes independent of CSV row order, grid traversal, allocation address, and scheduler timing.
* **Positive:** Equal-distance contacts survive, separating overlaps receive no false impulse, oblique tangents remain correct, and finite wall overshoot always ends in bounds.
* **Positive:** The serial contract gives future grids, solvers, and parallel implementations one observable oracle.
* **Positive:** Input arrives as one validated value parameter per tick, so the simulation still owns no queue, callback, socket, or clock, and the single-writer property of `docs/architecture/0002-simulation-architecture.md` § "Ownership and lifecycle" is unchanged by gameplay.
* **Positive:** Zero drag, an empty batch, and a mode whose systems write nothing reproduce the accepted baseline bit-for-bit, so the framework landed without regenerating one physics fixture or reverifying one collision, wall, or integration outcome.
* **Positive:** Game rules reach a tick only at a declared stage, in the contact table, or in the spawn policy, so the kernel stays an oracle no mode can bend and a mode author cannot make a tick nondeterministic by writing an ordinary system.
* **Negative:** The tick now has two roster-changing points — phase 0 and the commit — where the accepted baseline had a fixed roster, so the derived spatial index can fall out of agreement with the roster in two places instead of none, and a `DespawnEvent` emitted at a stage takes effect later than the stage that emitted it.
* **Mitigation:** One stated invariant covers both points: each leaves the grid equal to a phase 6 rebuild over the roster it produced, and the commit's fixed internal order puts removal before the rebuild and the rebuild before publication. The despawn fixture row asserts that equality against the exhaustive all-pairs reference.
* **Negative:** Sequential multi-contact response is ID-order-dependent and may not preserve geometric symmetry for piles.
* **Mitigation:** Accept the deterministic artifact for the current moving-disc game; require a versioned simultaneous-contact amendment when gameplay demonstrates the need.
* **Bounded limitation (Step 16):** Continuous pair/wall detection removes discrete between-tick
  tunneling for admitted representable sweeps. Precision loss and resource exhaustion fail the
  whole tick explicitly; no exact-real or unbounded-work claim is made.
* **Mitigation:** Keep fixture speeds/radii/timestep within the discrete model's needs and pin the limitation with a high-speed crossing fixture; adopt swept collision only through the documented amendment path.
* **Negative:** Fixing the quantum at 400 Hz couples physics behavior, fixture horizons, and now every gameplay duration to that rate: a countdown, a restart delay, and every duration a mode declares are integer tick counts converted once at load (ADR 0004 § "Determinism obligations for framework code"), so a match's felt pacing is expressed in ticks rather than seconds.
* **Mitigation:** Keep wall-clock scheduling and presentation cadence outside simulation; supersede this ADR deliberately if measured Linux cost or gameplay requirements demand another quantum. A superseding quantum regenerates every mode's tick counts alongside fixture horizons; both follow from configuration and this contract, neither from a clock.
* **Negative:** Binary64 arithmetic does not promise cross-toolchain bit identity.
* **Mitigation:** Preserve operation order, forbid fast-math and unordered reductions, require within-toolchain bit identity, and compare cross-toolchain values and invariants with the declared tolerance.
* **Operational:** Scenario files carry explicit IDs and remain initial-state seeds; executable tests own the expected tick horizons and outcomes.
* **Operational:** `drag_per_second` joins the `[simulation]` section because phase 1 owns it; it is `0` in every fixture and test configuration and nonzero only in deployment. A migrated physics fixture keeps its accepted horizon under a mode whose systems write nothing, or under `royale` when nothing in its command log requests a start. Fixtures that exercise gameplay set those keys deliberately.
* **Reversibility:** The pure physics functions and phase-local buffers allow a versioned collision or integration policy to replace the baseline without changing world ownership, runtime publication, protocol encoding, or server boundaries.

## Implementation evidence

* `src/simulation` owns this ADR's units, exact `FixedDelta`, pure equations, stable ordering, phase buffers, hook stages, and failure behavior.
* `tests/unit/simulation` and `tests/fixtures` encode every outcome in the fixture table, including the discrete high-speed limitation and migrated seed horizons.
* `config/blob-royale.cfg` uses semantic lower-case keys and records the accepted 400 Hz contract and the `[simulation] drag_per_second` kernel parameter.

## Related

* [`0002-simulation-architecture.md`](0002-simulation-architecture.md) — ownership, dependency direction, pure-physics boundary, and the seam inventory this contract realizes.
* [`0004-gameplay-architecture.md`](0004-gameplay-architecture.md) — the framework this contract places: component stores, `GameWorld`, the hook stages, `InputBatch` and `EntityIdReservation`, `SpawnPolicy`, the `ContactRuleTable`, `GameMode`, and the determinism obligations every system inherits.
* [`0005-royale-mode.md`](0005-royale-mode.md) — the first mode's rules — safe zone, elimination and placement, spawning, match phases, and configuration — which this contract places and never restates.
* [`../PROJECT_DEEP_DIVE.md`](../PROJECT_DEEP_DIVE.md) — evidence for prototype timing, collision, spatial-index, and fixture defects.
* [`../../.claude/plans/2026-08-04-feature-ready-foundation.md`](../../.claude/plans/2026-08-04-feature-ready-foundation.md) — accepted migration order and Steps 10–15 requirements.
* [`../../.claude/plans/2026-09-06-playable-prototype-tailnet.md`](../../.claude/plans/2026-09-06-playable-prototype-tailnet.md) — accepted plan; Step 13 records this amendment and Steps 15 through 19 implement it.

**Amended 2026-09-06:** Phase 10 applies `DespawnEvent` roster removals before it validates, not
after. An entity that a system both pushes out of bounds and marks for removal in one tick is a
removal rather than a hard failure, and Royale's elimination at the arena edge is exactly that
pairing. Validation now judges only the bodies the tick commits.

**Amended 2026-09-06:** The gameplay framework of
[`0004-gameplay-architecture.md`](0004-gameplay-architecture.md) is accepted, so this contract now
places it. The canonical tick becomes one fixed kernel with three declared hook stages. Phase 0
applies the tick's validated `InputBatch` — despawns, then spawns seated by the engine `SpawnSystem`
through the mode's `SpawnPolicy` and the map's markers, then commands recorded into
`Controllable::commands_this_tick`. Phase 1 gains drag from the `[simulation]` kernel parameter
`drag_per_second`. Phase 3 takes each admitted contact's response from the mode's `ContactRuleTable`
by first match, whose built-in rows are required to reproduce the accepted pair and reflection
numbers exactly. `kPreKernel`, `kPostKernel`, and `kLifecycle` hold the mode's declared systems and,
last and not removable, the engine's lifecycle system with its one transition per tick. The accepted
phase 7 becomes phase 10, which now also applies `DespawnEvent` roster removals, rebuilds the
derived index from the surviving roster, and clears the tick's `WorldEvent`s; the numbers 7 through
9 are left unused. § "Excluded player commands" is replaced by § "Accepted simulation input", which
realizes `@extension-point simulation_input` as
`GameSimulation::step(FixedDelta, const InputBatch&)` and adds the `EntityIdReservation` rule. The
fixture table gains framework-generic rows and defers every Royale outcome to
[`0005-royale-mode.md`](0005-royale-mode.md). No game rule lives here: this contract fixes only
where a rule runs, what it may read, and what makes it reproducible. At `drag_per_second = 0`, with
an empty batch and a mode whose systems write nothing, the accepted baseline is reproduced
bit-for-bit, so no fixture horizon or expected outcome above is regenerated, and the decision and
its `Accepted` status are unchanged.

**Amended 2026-09-07:** A body may declare its own mass, restitution, contact size, and share of
the kernel's drag, and may cross the arena bounds instead of folding off them. None of it changes
the accepted collision or the accepted drag.

`PhysicsBody` gains `restitution`, defaulting to the perfectly elastic `1.0`, beside the `mass` no
phase previously read. A `variable_impulse` row is declared **above** `elastic_disc` in
`ContactRuleTable::built_in()` and matches only when one of the two bodies differs from the
baseline, so a world of ordinary blobs never reaches it and `elastic_disc` still delegates to
`resolve_player_pair_collision` unmodified. That ordering is the whole of the argument: `first_match`
takes the first matching row in declared order, so the same row declared *below* the baseline would
be unreachable instead, because `elastic_disc` matches every dynamic pair. The general equation is
`j = -(1 + e) * (v_rel . n) / (1/m_a + 1/m_b)` with the operation order fixed in the implementation
and `e` the pair's minimum restitution -- minimum because one perfectly inelastic body should damp
every contact it takes part in, and because it returns exactly `1.0` for a baseline pair. It reduces
to § "Player-pair policy" algebraically at unit masses and restitution `1`, but **not bit-for-bit**:
it forms `(v_b - v_a) . n` where the baseline forms `(v_b . n) - (v_a . n)`, plus one product and
three divisions the baseline never performs. That is precisely why a baseline pair must never reach
it, and why the predicate is a requirement rather than an optimization.

Contact size follows the same argument. The impulse, phase 3's narrow-phase gate, and the grid's
coverage box now measure a pair at `r_a + r_b` from each body's effective radius rather than at
`2 * player_radius`. This changes no committed value, because every body that exists today has an
effective radius equal to the configured one and `x + x` is exactly `2 * x` in binary64 at every
finite magnitude. The grid was the load-bearing one: coverage at the configured radius is not the
superset § "Spatial-grid policy and partition boundaries" requires, and the falsification test
builds the exhaustive all-pairs reference that section names and fails when the coverage is reverted.

Contact is a question about a pair's own geometry, so it takes the pair's radii. The arena is a
question about configuration, so `[r, extent - r]` and phase 4's fold keep `player_radius`. Moving
that interval would change where every existing body may stand and would regenerate every accepted
wall fixture; it stays the versioned growing-blob change.

A body may also declare that it crosses the bounds rather than folding off them, which is what an
object travelling across the arena needs. The commit-time bounds check skips such a body; it stays
bounded by `Vector2`'s component limit, so it leaves the arena but never the representable world.
The grid clamps it into the edge cells rather than dropping it from the index, because a body one
step outside a wall already overlaps a body just inside it and absence would make that a missed
contact rather than a deferred one. The cost is that a body far outside is offered against
everything in the cells it clamps to, all of which the narrow phase rejects on distance.

A static body may now carry zero mass. Nothing divides by it -- neither built-in row reads a mass,
and the general impulse requires a dynamic body on both sides -- and the accepted protocol golden
already publishes a wall that way against a schema typing mass as non-negative. Requiring otherwise
would have regenerated an accepted artifact to state an invariant nothing needs.

**Drag becomes per-body by the same argument, and § "Canonical tick" phase 1 is reworded rather
than deleted.** `PhysicsBody` gains `drag_scale`, defaulting to `1.0`, validated finite and
non-negative through the same single validating factory `mass` and `restitution` route through.
Phase 1 forms `drag_per_second × drag_scale` and hands that to the existing equation; the `× dt`,
the `1.0 -`, the `max(0, …)` clamp, and the componentwise scale are all untouched, and so is the
order they are evaluated in. Every body that exists carries the default, and multiplication by
`1.0` is exact in binary64 for every finite value, so `drag_per_second × 1.0` **is**
`drag_per_second` bit for bit and every committed velocity is the value it always was -- proved the
way the contact-size change was proved, by an empty diff over `tests/fixtures/replays/`, `maps/`,
and the published schemas, and by the `AcceptedBaselineTick` oracle passing unmodified.

Phase 1's own sentence used to say `drag_per_second` "applies identically under every mode, and no
system may reproduce or bypass it". The part that matters stays true and is still written there:
the kernel owns drag, the kernel applies it in phase 1, and no *system* reproduces or bypasses it.
What changes is one clause -- it now applies identically under every mode **at each body's own
declared scale** -- and the mechanism is one the contract already uses, because the kernel reads a
coefficient off the body exactly as phase 3 reads a mass. Leaving the old clause standing beside
the new equation would have left a reader holding two sentences that cannot both be true.

`drag_scale` is deliberately **not** part of `body_has_baseline_physics`. That predicate decides
which *collision* equation phase 3 hands a pair to, and no collision equation reads drag. Including
it would route a pair to the general impulse because one body coasts, and the general impulse is
not bit-identical to the accepted exchange -- so a unit-mass, perfectly elastic hazard that declares
`drag_scale = 0` would silently change the arithmetic of every contact it took part in for a reason
with nothing to do with contact. The rule the predicate now states for the next per-body property
is that it belongs there only if a contact rule reads it.

**Clarified 2026-09-12 (plan Step 18).** That rule governs which properties belong in
`body_has_baseline_physics`, not where a contact-relevant fact must be stored. Shield state is read
by a contact response and is deliberately **not** a `PhysicsBody` field: ADR 0008 decision (a)
resolved that a response reads the committed world exactly as a predicate may, so defense state
stays a separate body-bound `Shield` component and the equation-selecting predicate stays about
mass and restitution. A property earns a place in `body_has_baseline_physics` only if a collision
*equation* reads it; a property a response merely consults belongs in its own component.

**Raising hazard speed instead was rejected, and it is worth saying why, because it is the obvious
alternative.** The drag factor is geometric, so a body launched at `v` and never thrusting again
covers exactly `v / drag_per_second` world units in total. At the deployed `drag_per_second = 2.0`
on a 960×640 arena, a 260 wu/s object travels 130 wu of the 1,154 wu diagonal and stops -- verified
in an offline replay rather than reasoned about. Restoring the crossing through speed alone needs
upward of 2,500 wu/s, which crosses the whole arena in 0.38 s: an object nobody can see, let alone
dodge, which is not the mechanic. A crossing object is defined by keeping its speed, so it says so.

`drag_scale` is not published. Like `restitution` and the bounds behaviour it joins, no client
reads it to render a frame, so a protocol minor for a number nothing draws would be churn; the wire
encoder and `physics-body-component.schema.json` are untouched.

No fixture horizon, accepted snapshot, map, or oracle value changed, and the decision and its
`Accepted` status are unchanged.

**Amended 2026-09-10 (ADR 0008, plan Step 1):** ADR 0008 is accepted and commits this contract to
two changes that land with their implementing steps rather than now. First, § "Canonical tick"
phases 2 through 5, § "Player-pair policy", § "Wall policy", and the tunnelling consequence above
are replaced by swept, chronological within-tick motion under a total event order, with a narrow
motion-trigger socket beside the contact table: exactly the path the "fast players must not
tunnel" row names. That amendment is written when plan Step 16 wires the reviewed solver, after
the plan's Step 5 prototype gate, and until then every accepted fixture, oracle, and horizon
stands. Second, the acceleration persistence of § "State, units, and fixed time" becomes
persistence of normalized steering intent, from which shared steering recomputes acceleration each
tick under a match-owned tuning value; that lands with plan Step 10 and changes no committed value
below the normal-speed ceiling, which fixture configurations author explicitly. The decision and
its `Accepted` status are unchanged today.

## Amendment: persistent steering intent, 2026-09-11 (plan Step 10)

This supersedes § "State, units, and fixed time" and the steering paragraph of § "Canonical tick"
only for declared shared locomotion. `Controllable` retains optional private normalized thrust
intent. Absence is distinct from explicit zero: an authored body acceleration remains untouched
until a valid thrust reaches that body. Normalize once with the original written square-root,
reciprocal, and component multiplication; do not normalize the stored result again. Before old
readers delegate, independent frozen arithmetic and real-system sequences must pass both lanes.

Each `kPreKernel` steering pass computes requested acceleration from stored intent and the current
match tuning. Its exact uncapped branch preserves `(component * scale) * acceleration` bits.
The finite-step cap projects the requested Euler endpoint into the disc whose radius is the
greater of normal top speed and current speed, then materializes acceleration. Recheck through
the canonical integration equation so rounding cannot silently violate speed or amplify requested
propulsion; bounded representability failure remains explicit. This is not a clamp on current
velocity, does not delete external overspeed, and does not change phase-1 integration or drag.
A crossing tick is constrained even if it began below the normal ceiling. Existing phase
admission remains; no Running-only thrust gate is introduced.

These postconditions compare computed binary64 squared norms, not exact-real norms. Compute
the requested endpoint with the canonical integrator, retaining its existing domain failures.
An unconstrained endpoint returns the requested acceleration verbatim. Otherwise use the initial
radial factor `sqrt(max(V², v·v)) / sqrt(w·w)`. If this first computed projection equals `v`
componentwise, zero is the defined quantized-projection identity, not proof of pure-outward input:
binary64 rounding can erase a tiny turn. For a nonidentity first projection, try that radial
factor plus at most eight `nextafter` corrections toward zero, always scaling the original `w`.
Each radial attempt permits the initial non-amplifying acceleration scale plus at most eight
corrections. Recheck canonical integrated squared speed and original-request non-amplification.
Any later zero acceleration or integrated endpoint equal to `v` fails immediately, without
shrinking through identity into braking. Exhaustion raises `GAMEPLAY.LOCOMOTION_PRECISION_LOST`.
This is a bounded witness policy, not correctly rounded exact projection or exhaustive search.

Shared body seating clears old-body intent while retaining commands received for this tick;
snapshot publication strips intent. The same owner will apply the Step 14 movement lock, with no
status source introduced now. Phase 0's existing controller-addressed handler gains atomic tuning
and bounded successful-commit decisions as explicitly named by the amended plan/ADR 0002. No new
kernel policy socket, clock change, live continuous-motion adoption, or fixture recalibration is
authorized. The plan's full baseline gates remain mandatory.

## Amended 2026-09-11: Guarded NPC declaration joins (Step 15)

The existing closed indexed-join handler accepts optional `expected_npc`, a complete kind/profile
declaration, only together with a seat index. A mismatch with current phase-0 seat state, including
preceding commands in the batch, is a normal no-op. Absence preserves existing literal indexed-join
behavior; the guard is supplied
by reconciliation, never inferred from a caller's kind or current state. NPC profile identity
survives declaration, join, leave, and publication. The selection catalogue is a bounded value,
not a policy callback; runtime checks SeatNpc membership before mailbox insertion. No command
kind, existing relative application rank, kernel phase, or policy socket changes. All accepted
unprofiled physics/replay values and command sequences remain subject to the unchanged fixture gates.

## Amended 2026-09-11: Live continuous motion (Step 16)

The owner accepted Step 5 and approved preserving oversized benchmark layouts as explicit
grid-only diagnostics. Canonical tick phases 2–5, player-pair policy, wall policy, body legality,
and the tunneling consequence above now adopt the reviewed solver. Historical discrete
equivalence claims in earlier dated amendments are historical, not a second live path.
One body-envelope validator governs solver, factory, endpoint grid, and final survivors;
grid invalidation includes effective radius. At most 256 physical bodies (including statics)
enter or survive the live kernel. Lower-only test limits and all solver work/storage ceilings
fail visibly and roll back the whole tick; standalone spatial-grid diagnostics keep their
existing larger capacity. No performance certification follows from these correctness limits.

The exact integration boundary and pure-move prerequisites are recorded in
[`2026-09-11-live-motion-integration-contract.md`](../reviews/2026-09-11-live-motion-integration-contract.md)
and [`2026-09-11-live-motion-prerequisite-review.md`](../reviews/2026-09-11-live-motion-prerequisite-review.md).
Production support/race triggers are supplied by Step 17; ~~guarded pair composition remains
Step 18~~ — it landed on 2026-09-12; see § "Amended 2026-09-12: Guarded pair composition and the
shared ability stage (Step 18)" below.

## Amended 2026-09-11: Ground attachment and safe seating (Step 17)

PhysicsBody's validated GroundAttachment is independent of bounds behavior, collision material,
and radius. Generic typed bodies default floating; production player seating/scenario construction
explicitly binds players to ground. Existing crossing hazards remain floating. Shared gameplay
registers the same canonical center-support query for bound dynamic bodies, with termination and
typed player elimination/nonplayer despawn consequences. No kernel mode branch or second solver.
Safe seating separately requires a supported full player disc and clearance against actual effective
body radii, using written square-root arithmetic and canonical tolerances. Every successful seat
refreshes subsequent marker admission against the updated live store. Work exhaustion still rolls
back the entire quantum; map shape admission does not guarantee every trajectory fits the event cap.

## Amended 2026-09-12: Guarded pair composition and the shared ability stage (Step 18)

Every dynamic pair in the four gameplay modes now resolves through one declared `guarded_pair` row
over the existing pure composition core. § "Canonical tick" carries the honest statement of what
this does and does not change to the accepted baseline: the dynamic/dynamic branch selects the same
two accepted equations by the same `body_has_baseline_physics` test; the static branch reflects the
same non-static side as `reflect_static` under an additional closing-speed guard that the solver's
own impact admission has already satisfied on every live path; unguarded arithmetic is returned
unchanged; and a composed non-lethal contact reports the diagnostic rule name `guarded_pair`
instead of `elastic_disc`, `variable_impulse`, or `reflect_static`. The three built-in rows remain
reachable, and remain the engine baseline, for a mode that declares no row of its own.

`kPreKernel` gains its second kind of consumer. The shared `ability` system reads this tick's
recorded commands and writes body-bound `Shield` state so the same tick's contact responses can see
it in the frozen post-`kPreKernel` world. No fourth hook stage, fourth policy socket, ninth mode
declaration, second solver, or new event root is introduced, and `TickContext` gains nothing.
Ability durations are integer tick counts converted once at configuration load, under the existing
no-wall-clock obligation.

This records the contract change, not verification, native capacity, or release certification.
The implementation contract is
[`2026-09-12-shield-composition-contract.md`](../reviews/2026-09-12-shield-composition-contract.md).

## Amended 2026-09-12: The one-shot charge (Step 19)

**`kPreKernel` now writes velocity.** The shared `ability` system already wrote a component here;
with the one-shot charge it also writes `PhysicsBody::velocity`, through `with_velocity`, as an
instantaneous additive burst applied before phase 1 integrates. § "Canonical tick" carries the
in-place statement of what that does and does not change. In summary: the write is additive so
lateral motion survives; the pre-write velocity is read before the store is touched, because the
join hands out a reference the write invalidates in place; phase 1 then integrates and damps the
result in the ordinary way, with no charge-only substep, no second integrator, and no exemption
from drag. The stage rule was never "kPreKernel writes only intent" — it is "read this tick's
commands before anything has moved" — and that is unchanged.

**Nothing clamps an externally imparted speed, and this contract says so on purpose.** The normal
movement ceiling limits *propulsion*, applied by a mode's steering system to the acceleration it is
about to store; no phase re-reads a velocity to trim it, and the solver certifies impacts rather
than bounding speeds. At the shipped `drag_per_second=0` the phase 1 factor is exactly `1.0`, so a
burst, a collision impulse, and a knockback all persist until something else changes them. The
consequence for charge is stated where the arithmetic is: repeated activations are bounded by the
authored `[abilities] charge_safety_envelope_speed`, checked in raw doubles **before** the burst is
applied, and not by drag. Checking before rather than after is not a style preference — a `Vector2`
component past `1e12` throws, that throw would escape `AbilitySystem::apply` and
`GameSimulation::step`,
and the runtime worker would then stop the simulation thread permanently. A rule that made one
client's command able to end a match would be a worse failure than any speed.

**One accepted ordering residual, recorded rather than removed.** All four modes declare
`ability` after `thrust_steering` at this stage, so on an activation tick the propulsion limiter has
already sized the tick's acceleration against the pre-burst velocity and the committed endpoint is
`v_pre + burst + a·dt`. Reordering would be worse: with the ability system first, the limiter's
`max(ceiling², v·v)` bound would be computed against the post-burst velocity and thrust could
sustain a charged speed indefinitely, which is the larger violation of the propulsion rule above.
From the following tick that same `max` term is what lets a charged body steer without amplifying
or braking.

No fourth hook stage, policy socket, mode declaration, second solver, or new event root is
introduced, and `TickContext` gains nothing. The accepted discrete baseline is a frozen oracle over
zero drag, empty input, and no-writing systems, so a mechanic that only ever runs on a recorded
command does not reach it. This records the contract change,
not verification, native capacity, or release certification. The implementation contract is
[`2026-09-12-charge-contract.md`](../reviews/2026-09-12-charge-contract.md).
