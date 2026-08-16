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

Acceleration is stored state. At the start of every tick, each player uses only its already-stored acceleration:

`v_accelerated = v_previous + acceleration × (1/400 s)`.

Acceleration persists unchanged until a future, separately specified simulation-input contract changes it. The baseline performs no force calculation, acceleration cache read, input sampling, damping, speed cap, gravity, or implicit reset.

### Canonical tick

`GameSimulation::step(FixedDelta)` evaluates one tick in this exact order:

1. **Apply stored acceleration.** Visit players in ascending `EntityId` order and calculate accelerated velocity with semi-implicit Euler. Positions do not change.
2. **Build canonical candidate pairs.** Query the grid representing the last committed positions. Canonicalize every broad-phase pair as `(lower EntityId, higher EntityId)`, remove duplicate keys without dropping equal-distance keys, then sort lexicographically by the two IDs. Pair distance is not an ordering key.
3. **Resolve player pairs once.** Traverse the frozen canonical pair list once. The narrow phase rejects non-contacts and separating contacts. A resolved pair writes both velocities before the next pair is evaluated. Positions and stored accelerations do not change.
4. **Resolve walls.** Visit players in ascending `EntityId` order. For each player, resolve the x axis before the y axis from its current position and post-pair velocity. Produce a tick-local displacement and terminal velocity; do not publish an intermediate body state.
5. **Integrate position.** Visit players in ascending `EntityId` order and apply the displacement produced by wall resolution. This is semi-implicit Euler because the displacement derives from the accelerated, collision-resolved velocity.
6. **Rebuild the spatial grid.** Discard old membership and deterministically rebuild it from the new positions in ascending `EntityId` order.
7. **Commit.** Reject any non-finite or out-of-bounds result as a hard simulation failure. Otherwise canonicalize signed zero to positive zero, replace the committed world/grid state, increment the tick sequence once, and make that complete tick eligible for snapshot publication.

No snapshot or tick sequence is committed from an incomplete phase. The serial order above is the observable reference even if a later implementation parallelizes internal calculation (`docs/architecture/0002-simulation-architecture.md` § "Extension points").

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

The migrated fixture seeds have these accepted expectations:

* `tests/fixtures/partition-trace-test.csv` is an initial-state seed. With zero acceleration and before any wall contact, its player follows `p_N = p_0 + N × v / 400`; crossing any grid boundary must match the no-grid reference.
* `tests/fixtures/player-on-player-collision-test.csv` is an initial-state seed. Its first two players begin 40 wu apart and close at 5 wu/s. They first reach exact 20 wu contact after 1,600 ticks without an impulse on that tick, then exchange their y velocities during tick 1,601.
* `tests/fixtures/player-on-wall-collision-test.csv` is an initial-state seed. With radius 10, its first player reaches the lower x wall from `x = 15` at `1 wu/s` after 2,000 ticks; that committed tick has `x = 10` and an inward x velocity of `+1 wu/s`.

The CSV files contain initial state rather than authoritative output recordings. Executable fixture and simulation tests assert their shapes and accepted tick horizons. Malformed rows, duplicate IDs, non-finite values, invalid bounds, and unsafe dimensions are loader rejection fixtures, not simulation ticks.

### Excluded player commands

This ADR defines autonomous evolution from loaded state only. It does not define player identity, ownership, commands, input cadence, input ordering, command validation, acceleration changes, disconnect behavior, prediction, rollback, or reconciliation. `GameSimulation::step(FixedDelta)` has no command parameter or ambient input source in this version. Network frames cannot mutate the simulation, and the prototype client's acceleration-shaped message creates no compatibility obligation (`docs/PROJECT_DEEP_DIVE.md` § "React client").

`@extension-point simulation_input` from `docs/architecture/0002-simulation-architecture.md` remains documentation only. A future accepted contract may add one validated, tick-addressed input batch at the start of the canonical tick. It must define authorization and command conflicts before an implementation adds an input queue or changes stored acceleration.

### Justified extension points and what-if stress

* **What if gameplay adds unequal radii or masses?** Extend `PhysicsBody` and replace the pure pair equation with the general impulse equation. Keep canonical pair formation and outer tick phases. This becomes a versioned physics change because fixtures and snapshots can change; no strategy interface is added until a second live policy exists.
* **What if fast players must not tunnel through each other?** Replace closed-disc overlap with swept candidate bounds and a deterministic time-of-impact event order inside phase 3. Equal-time events need a new explicit tie policy. The current pure collision boundary leaves an implementation path, but continuous behavior requires an ADR amendment rather than a silent improvement.
* **What if multi-player piles need symmetric outcomes?** A deterministic simultaneous-contact solver may replace sequential impulses inside phase 3. It must state convergence and equality semantics and version changed outcomes. Stable IDs remain the final tie-breaker.
* **What if another spatial index is faster?** Replace `SpatialGrid` behind `GameSimulation` only if it emits the same canonical candidate pair set. Grid shape and traversal remain non-observable mechanism (`docs/architecture/0002-simulation-architecture.md` § "Ownership and lifecycle").
* **What if runtime cadence changes?** Presentation rate and temporary scheduler lateness do not change `FixedDelta`. A different simulation quantum requires an amended contract and regenerated fixtures; measured wall time never enters physics.
* **What if simulation phases are parallelized?** Parallel code may calculate disjoint phase-local buffers, but it must commit the same serial-reference result. Sequential shared-body pair response cannot be reordered merely for throughput (`docs/architecture/0002-simulation-architecture.md` § "Extension points").

These are seams in the pure-function and phase boundaries, not plugin registries or polymorphic strategy classes. They each have at least two plausible future policies, while the accepted baseline retains one canonical implementation.

## Consequences

* **Positive:** Physical quantities have conventional units, so changing presentation or runtime cadence cannot change gameplay speed.
* **Positive:** Exact phase and ID precedence make repeated outcomes independent of CSV row order, grid traversal, allocation address, and scheduler timing.
* **Positive:** Equal-distance contacts survive, separating overlaps receive no false impulse, oblique tangents remain correct, and finite wall overshoot always ends in bounds.
* **Positive:** The serial contract gives future grids, solvers, and parallel implementations one observable oracle.
* **Negative:** Sequential multi-contact response is ID-order-dependent and may not preserve geometric symmetry for piles.
* **Mitigation:** Accept the deterministic artifact for the current moving-disc game; require a versioned simultaneous-contact amendment when gameplay demonstrates the need.
* **Negative:** Discrete player-pair detection permits tunneling between committed positions.
* **Mitigation:** Keep fixture speeds/radii/timestep within the discrete model's needs and pin the limitation with a high-speed crossing fixture; adopt swept collision only through the documented amendment path.
* **Negative:** Fixing the quantum at 400 Hz couples physics behavior and fixture horizons to that rate.
* **Mitigation:** Keep wall-clock scheduling and presentation cadence outside simulation; supersede this ADR deliberately if measured Linux cost or gameplay requirements demand another quantum.
* **Negative:** Binary64 arithmetic does not promise cross-toolchain bit identity.
* **Mitigation:** Preserve operation order, forbid fast-math and unordered reductions, require within-toolchain bit identity, and compare cross-toolchain values and invariants with the declared tolerance.
* **Operational:** Scenario files carry explicit IDs and remain initial-state seeds; executable tests own the expected tick horizons and outcomes.
* **Reversibility:** The pure physics functions and phase-local buffers allow a versioned collision or integration policy to replace the baseline without changing world ownership, runtime publication, protocol encoding, or server boundaries.

## Implementation evidence

* `src/simulation` owns this ADR's units, exact `FixedDelta`, pure equations, stable ordering, phase buffers, and failure behavior.
* `tests/unit/simulation` and `tests/fixtures` encode every outcome in the fixture table, including the discrete high-speed limitation and migrated seed horizons.
* `config/blob-royale.cfg` uses semantic lower-case keys and records the accepted 400 Hz contract.

## Related

* [`0002-simulation-architecture.md`](0002-simulation-architecture.md) — ownership, dependency direction, pure-physics boundary, and documented future seams.
* [`../PROJECT_DEEP_DIVE.md`](../PROJECT_DEEP_DIVE.md) — evidence for prototype timing, collision, spatial-index, and fixture defects.
* [`../../.claude/plans/2026-08-04-feature-ready-foundation.md`](../../.claude/plans/2026-08-04-feature-ready-foundation.md) — accepted migration order and Steps 10–15 requirements.
