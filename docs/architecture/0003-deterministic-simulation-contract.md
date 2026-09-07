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

World bounds describe the outer rectangle `[0, width] × [0, height]`. A player is a closed disc with the configured common radius `r`; a committed center lies in `[r, width - r] × [r, height - r]`. Valid configuration requires `r > 0`, `width > 2r`, and `height > 2r`.

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
sit three named hook stages holding the mode's declared systems, and the kernel has exactly two
policy sockets inside a phase — the mode's `SpawnPolicy` in phase 0 and its `ContactRuleTable` in
phase 3 (ADR 0004 § "The tick: one fixed kernel, three named stages").

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

1. **Apply stored acceleration and drag.** Visit players in ascending `EntityId` order, calculate
   accelerated velocity with semi-implicit Euler, then scale that velocity by
   `max(0, 1 - drag_per_second × dt)`, where `dt` is the same `kFixedDeltaSeconds` the acceleration
   step already uses. `drag_per_second` is a `[simulation]` kernel parameter, not mode
   configuration: drag is mechanism this contract owns, it applies identically under every mode, and
   no system may reproduce or bypass it. It must be finite and non-negative, and the clamp at zero
   keeps the factor total when `drag_per_second × dt` exceeds one, so a large configured drag stops
   a body rather than reversing it. Positions do not change.
2. **Build canonical candidate pairs.** Query the grid, which after phase 0 indexes this tick's
   live roster at its start-of-tick positions. Canonicalize every broad-phase pair as
   `(lower EntityId, higher EntityId)`, remove duplicate keys without dropping equal-distance keys,
   then sort lexicographically by the two IDs. Pair distance is not an ordering key.
3. **Resolve contacts once.** Traverse the frozen canonical pair list once. A pair is admitted only
   when `(a.collision_mask & b.collision_layer)` and `(b.collision_mask & a.collision_layer)` are
   both nonzero, a pure integer predicate that adds no ordering
   (ADR 0004 § "Entities, components, and stores"). The narrow phase rejects non-contacts and
   separating contacts. An admitted contact takes its response from the mode's `ContactRuleTable`:
   walk that table's rows in declared order, try the canonical orientation before the swapped one,
   and take the first matching (row, orientation). A pair matching no row is unchanged, so the table
   is total without a default row (ADR 0004 § "Contact rules"). A response may write only the two
   bodies; every other consequence leaves as a `WorldEvent` for a later stage, which is what keeps
   the kernel's mutation surface exactly what this contract pins. A resolved pair writes both bodies
   before the next pair is evaluated. Positions and stored accelerations do not change.
4. **Resolve walls.** Visit players in ascending `EntityId` order. For each player, resolve the x axis before the y axis from its current position and post-pair velocity. Produce a tick-local displacement and terminal velocity; do not publish an intermediate body state.
5. **Integrate position.** Visit players in ascending `EntityId` order and apply the displacement produced by wall resolution. This is semi-implicit Euler because the displacement derives from the accelerated, collision-resolved velocity.
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

**The built-in contact rows are the accepted baseline, and this is a requirement on them rather than
a description of them.** `ContactRuleTable::built_in()` carries exactly two rows in this order:
`elastic_disc` for a dynamic-dynamic pair, then `reflect_static` for a dynamic-static pair
(ADR 0004 § "Contact rules"). `elastic_disc` must evaluate the equations of § "Player-pair policy"
and produce, for every canonical pair of equal-radius unit-mass dynamic bodies, the bit-identical
velocities the accepted narrow phase produced, including the coincident-center fallback, the
`ε_velocity` separating test, and the visibility of an earlier pair's result to a later pair.
`reflect_static` must preserve the dynamic body's speed magnitude on the contact normal, leave its
tangential component unchanged, and leave the static body untouched, which is § "Wall policy"'s
reflection applied to a body instead of an arena edge. A table whose built-in rows disagree with
either section is a defect in the table, not a new physics policy; changing an equation is the
versioned physics amendment § "Justified extension points and what-if stress" already describes. The
arena fold of phase 4 is not dispatched through the table at all, because that fold is what makes a
committed center in bounds for arbitrarily large finite overshoot.

**The accepted baseline is a special case of this pipeline, not an approximation of one.** At
`drag_per_second = 0` the product `drag_per_second × dt` is exactly `+0.0`, the drag factor is
exactly `1.0`, and multiplication by `1.0` is the identity on every finite binary64 value including
both signed zeros. An empty `InputBatch` makes phase 0 a no-op, a mode whose systems write nothing
leaves all three stages without effect, and the built-in rows reproduce the accepted narrow phase
exactly. Under those three conditions the staged kernel commits the same bodies, in the same order,
at the same tick horizons, as the accepted seven-phase baseline: every fixture horizon and expected
outcome already in this ADR remains valid unchanged, and no fixture is regenerated. Equivalence is
asserted over every value the accepted baseline committed; the match section a snapshot now also
carries has no baseline counterpart to differ from. The `sandbox` mode is the standing witness — its
one system is steering, which writes nothing when no thrust was recorded — and a mode with no
systems at all is the stricter one
(`.claude/plans/2026-09-06-playable-prototype-tailnet.md` Step 17). A seeded fixture that runs under
`royale` must additionally set `lobby_minimum_players` above its seeded roster size, which holds the
match in `lobby`, where the zone stays at full radius and elimination never evaluates
(ADR 0005 § "Mode configuration"). All fixtures and tests run at `drag_per_second = 0`; only the
deployment configuration sets a nonzero value (same plan, § "Execution constraints").

### Player-pair policy

The narrow phase models frictionless, perfectly elastic collisions between equal-radius, equal-unit-mass discs. For canonical pair `(a, b)`, where `a.id < b.id`:

* Let `d = p_b - p_a`, `distance = |d|`, and `contact_distance = 2r`.
* The pair is in contact when `distance <= contact_distance + ε_position`. A larger separation produces no change.
* When `distance > ε_position`, the contact normal is `n = d / distance`, directed from `a` to `b`.
* When the centers coincide, use `n = normalize(v_a - v_b)` if the relative velocity magnitude exceeds `ε_velocity`; otherwise use the deterministic fallback `n = (1, 0)`. This rule prevents division by zero and never consults container or thread order.
* Let `relative_normal_speed = (v_b - v_a) · n`. When it is greater than or equal to `-ε_velocity`, the pair is stationary or separating and receives no impulse, even when it overlaps.
* Otherwise exchange only the normal velocity components:

  `v_a' = v_a + ((v_b · n) - (v_a · n)) n`

  `v_b' = v_b + ((v_a · n) - (v_b · n)) n`.

Tangential components remain attached to their original players. Each applied impulse therefore preserves pair momentum and kinetic energy within the floating-point tolerance. The baseline performs no penetration correction. An overlapping separating pair keeps its velocities and integrates apart; an exactly coincident pair with equal velocities remains coincident without producing an arbitrary impulse.

Every candidate pair is evaluated once. A player may participate in multiple pairs during the same tick. Equal distance does not select a winner: if `(1, 2)` and `(1, 3)` are both contacts, both remain in the list and `(1, 2)` resolves first. The second pair observes player 1's velocity from the first response. This sequential result is deterministic but intentionally not a simultaneous constraint solution.

Player-pair detection is discrete at the positions committed at the start of the tick. A pair that first reaches contact during position integration becomes eligible on the next tick. A pair that crosses completely between two committed positions does not collide in this baseline. Scenarios must not rely on swept player-player collision until a continuous-collision amendment is accepted.

### Wall policy

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

`SpatialGrid` is a broad-phase index, not physical state. It must produce a superset of all pairs whose committed discs can touch; the narrow phase alone decides contact.

Cells are ordered row-major by `(row, column)`. Their geometric regions are half-open on internal maximum edges and closed at the world's outer maximum. An exact internal boundary belongs to the cell on its positive side for a center/home-cell calculation. For collision coverage, insert each `EntityId` into every cell intersected by the disc's closed axis-aligned bounding box. A bounding-box edge exactly on an internal cell boundary counts as intersecting both adjacent cells. IDs inside each cell are ascending; candidate pair keys are deduplicated and sorted as specified above.

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
| Drag decay | With nonzero `drag_per_second` and zero stored acceleration, velocity is multiplied by `max(0, 1 - drag_per_second / 400)` every tick and decays geometrically. Under a constant stored acceleration `a` the sequence converges to the discrete fixed point `a × (1 - drag_per_second × dt) / drag_per_second`, not to the continuous-limit `a / drag_per_second`. A `drag_per_second × dt` above one stops a body at zero and never reverses it. |
| Spawn slot order | The engine `SpawnSystem` seats unseated entities in ascending `EntityId`, each at the marker index the mode's `SpawnPolicy` returns for the world-owned rotation counter and the free-marker set. A policy that returns no index defers that entity and seats nobody in its place. Submitting the same spawns in a different order produces the same seating, because the batch is canonical and seating order is ascending `EntityId` rather than arrival order. |
| Despawn of a pending pair member | A despawn removes its entity in phase 0, before phase 2 builds the pair list, so no candidate pair in that tick names it. A partner that would otherwise have been in contact receives no impulse on that tick and integrates unchanged. An entity removed instead by a `DespawnEvent` leaves the roster at the commit of the tick that emitted it. In both cases the committed grid holds no removed `EntityId`. |
| Match transition | At most one `MatchPhase` transition commits per tick. A mode whose durations are all zero advances exactly one phase per tick and terminates instead of chaining `lobby → countdown → running → ended → lobby` inside one tick. The transition observes the tick's final world, after every `kPostKernel` and `kLifecycle` system has run. |
| Baseline preservation | With `drag_per_second = 0`, an empty `InputBatch` on every tick, and a mode whose systems write nothing, every row above this one produces the same tick horizons, the same ordered bodies, and the same bytes as the accepted seven-phase baseline. Under `royale` the same holds with a `lobby_minimum_players` above the fixture's roster size. This row is the regression that proves the framework additive. |

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

* **What if gameplay adds unequal radii or masses?** `PhysicsBody` already carries `radius` and `mass`, so this becomes a general-impulse `ContactRule` row declared ahead of `elastic_disc`, not a new seam (ADR 0004 § "Contact rules"). Canonical pair formation and the outer phases are unchanged. It stays a versioned physics change because fixtures and snapshots can change; no strategy interface is added until a second live policy exists.
* **What if fast players must not tunnel through each other?** Replace closed-disc overlap with swept candidate bounds and a deterministic time-of-impact event order inside phase 3. Equal-time events need a new explicit tie policy. The current pure collision boundary leaves an implementation path, but continuous behavior requires an ADR amendment rather than a silent improvement.
* **What if multi-player piles need symmetric outcomes?** A deterministic simultaneous-contact solver may replace sequential impulses inside phase 3. It must state convergence and equality semantics and version changed outcomes. Stable IDs remain the final tie-breaker.
* **What if another spatial index is faster?** Replace `SpatialGrid` behind `GameSimulation` only if it emits the same canonical candidate pair set. Grid shape and traversal remain non-observable mechanism (`docs/architecture/0002-simulation-architecture.md` § "Ownership and lifecycle").
* **What if runtime cadence changes?** Presentation rate and temporary scheduler lateness do not change `FixedDelta`. A different simulation quantum requires an amended contract and regenerated fixtures, and now also regenerated gameplay durations, because every mode duration is an integer tick count; measured wall time never enters physics or gameplay.
* **What if commands must be tick-addressed, predicted, or rolled back?** This contract accepts one validated batch applied at phase 0 of the tick that receives it, and nothing more. Client-side prediction, server rollback, reconciliation, and input delay each need a stated authoritative-tick addressing rule and a re-simulation contract before an implementation buffers or replays a batch. The batch value and the single mutation entry point are what such a policy extends; neither is a queue it can grow behind.
* **What if a new game rule must run inside the tick?** It becomes a system registered at one of the three hook stages of a mode's declared list, and the stage follows from what the rule must see: `kPreKernel` for body intent before anything has moved, `kPostKernel` for consequences of committed positions and this tick's contacts, `kLifecycle` for roster and match bookkeeping (ADR 0004 § "The tick: one fixed kernel, three named stages"). The kernel phases themselves are not a seam and take no registrations. A rule expressible at no stage, in no `ContactRuleTable` row, and in no `SpawnPolicy` — a fourth stage, a hook inside a phase, or a mode-supplied bounds fold — is a versioned change to this contract rather than an insertion into it.
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
* **Negative:** Discrete player-pair detection permits tunneling between committed positions.
* **Mitigation:** Keep fixture speeds/radii/timestep within the discrete model's needs and pin the limitation with a high-speed crossing fixture; adopt swept collision only through the documented amendment path.
* **Negative:** Fixing the quantum at 400 Hz couples physics behavior, fixture horizons, and now every gameplay duration to that rate: a countdown, a restart delay, and every duration a mode declares are integer tick counts converted once at load (ADR 0004 § "Determinism obligations for framework code"), so a match's felt pacing is expressed in ticks rather than seconds.
* **Mitigation:** Keep wall-clock scheduling and presentation cadence outside simulation; supersede this ADR deliberately if measured Linux cost or gameplay requirements demand another quantum. A superseding quantum regenerates every mode's tick counts alongside fixture horizons; both follow from configuration and this contract, neither from a clock.
* **Negative:** Binary64 arithmetic does not promise cross-toolchain bit identity.
* **Mitigation:** Preserve operation order, forbid fast-math and unordered reductions, require within-toolchain bit identity, and compare cross-toolchain values and invariants with the declared tolerance.
* **Operational:** Scenario files carry explicit IDs and remain initial-state seeds; executable tests own the expected tick horizons and outcomes.
* **Operational:** `drag_per_second` joins the `[simulation]` section because phase 1 owns it; it is `0` in every fixture and test configuration and nonzero only in deployment. A migrated physics fixture keeps its accepted horizon under a mode whose systems write nothing, or under `royale` with a `lobby_minimum_players` above that fixture's roster size. Fixtures that exercise gameplay set those keys deliberately.
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
