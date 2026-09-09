<!-- canonical: royale_mode -- the royale game mode: steering, zone, elimination, and placement -->

# 5. Define the Royale game mode

* **Status:** Accepted
* **Date:** 2026-09-06
* **Deciders:** Project owner

## Context and Problem Statement

The gameplay framework is accepted and has no game in it.
`docs/architecture/0004-gameplay-architecture.md` defines entities as component sets in `GameWorld`,
a fixed physics kernel with three named system stages, a declared `ContactRuleTable`, and a
`GameMode` that declares seven things once at construction. It names `royale` in every worked
example it uses to prove those interfaces and defines none of its rules. `GameSimulation::step` now
takes a validated `InputBatch` and `docs/architecture/0003-deterministic-simulation-contract.md`
§ "Canonical tick" applies it at phase 0 and applies drag at phase 1, but nothing writes a thrust
into an acceleration, nothing shrinks a circle, and nothing decides a match.
`.claude/plans/2026-09-06-playable-prototype-tailnet.md` § "Execution constraints" accepts a thrust,
drag, and shrinking-zone core loop on plan approval and requires this ADR as Step 12.

Two questions are open. The first is what the smallest playable game is on the accepted physics:
equal-radius, equal-unit-mass discs, frictionless perfectly elastic pair collisions, reflecting
walls, and an exact 400 Hz quantum, with every quantity expressed in `wu`, `wu/s`, and `wu/s²`
(`docs/architecture/0003-deterministic-simulation-contract.md` § "State, units, and fixed time").
Smallest is a constraint, not a preference. Any rule that changes a collision equation, a wall
equation, or the simulation quantum invalidates the accepted fixture horizons and forces a physics
re-verification before a single player has ever pressed a key.

The second is where each of those rules lives now that a framework exists. Every rule below is one
of five things: a system at a declared stage, a value in a component, a key of the mode's own
validated configuration, a predicate of the mode's `MatchObjective`, or a marker in the map. None of
them is a phase inside `GameSimulation` and none of them is a field on the world.

This decision applies "separation of policy from mechanism" — game rules are declared systems
layered over an unchanged collision, wall, and integration kernel; "total functions over partial" —
every accepted configuration and every roster state has a defined result, including zero durations
and a full spawn ring; and "explicit lifecycle over implicit" — match phase and phase start tick are
committed world state, never derived from wall time or from process uptime.

## Considered Options

* **A. Thrust-plus-drag steering inside a linearly shrinking circular safe zone.** A command sets
  stored acceleration; linear drag bounds speed; a deterministic zone function eliminates players
  who linger outside it. This adds no new physical equation: it writes the acceleration field ADR
  0003 already stores, relies on a kernel scalar multiply, and reads positions. Every existing
  fixture stays valid at zero drag, and every rule lands as a system, a component, or map data on
  interfaces ADR 0004 already accepted.
* **B. Growth by consumption (agar-style).** Blobs absorb smaller blobs and gain radius and mass.
  Rejected: unequal radii and unequal masses replace the accepted equal-mass pair equation with the
  general impulse equation, which ADR 0003 § "Justified extension points and what-if stress" already
  classifies as a versioned physics change that regenerates fixtures. Under ADR 0004 it is a new
  contact row rather than a new seam, which lowers the structural cost and changes nothing about the
  verification cost. It also requires a growth-and-balance curve to be designed before any input
  path exists at all.
* **C. Wall-less sumo knock-out arena.** Remove the outer walls and eliminate a blob by pushing it
  past the arena edge. Rejected: it contradicts the accepted wall policy, which guarantees that a
  committed center always lies in `[r, width - r] × [r, height - r]` and that arbitrarily large
  finite overshoot still terminates in bounds
  (`docs/architecture/0003-deterministic-simulation-contract.md` § "Wall policy"). The bounds fold
  is kernel mechanism and not a policy socket, so a mode cannot remove it without a versioned
  physics change (ADR 0004 § "Justified extension points and what-if stress"). Deleting the walls
  deletes that totality guarantee and every wall fixture with it.
* **D. Direct velocity-set steering without drag.** Overwrite velocity from the command each tick.
  Rejected on two counts: it erases collision response, because a bounced player overwrites its
  post-impulse velocity on the following tick and contacts become cosmetic; and collision-injected
  speed has nothing to remove it, so speed accumulates across repeated exchanges unless a speed cap
  is added. A cap applied after the contact phase discards normal-component energy and breaks the
  momentum and kinetic-energy invariants asserted by the head-on and oblique cases in
  `docs/architecture/0003-deterministic-simulation-contract.md` § "Fixture contract and expected
  outcomes".
* **E. Variable-speed or wall-clock match timing.** Drive countdown, shrink, and grace from measured
  elapsed time so the match feels the same under a changed presentation rate. Rejected: it makes
  scheduler jitter observable inside the simulation, which ADR 0003 § "State, units, and fixed time"
  forbids outright, and `TickContext` exposes no clock by construction (ADR 0004 § "Determinism
  obligations for framework code"). The same recorded command stream would replay to a different
  result, destroying the 100-fresh-run bit-identity requirement.

## Decision Outcome

**Chosen: Option A, declared as the `royale` `GameMode`.**

### The mode declaration

`RoyaleMode` is a `GameMode` in `blob_gameplay` under `src/gameplay/royale/`, registered by one line
in `game_mode_registry.hpp` and selected by `[match] mode=royale`. It is constructed with its own
validated configuration and hands that configuration to the systems and policies it builds. The
engine calls each declaration exactly once, at construction, and nothing calls into the mode during
a tick (ADR 0004 § "Game modes and the match lifecycle").

| Declaration | What `RoyaleMode` returns | Interface |
|---|---|---|
| `name()` | `royale` | ADR 0004 § "Game modes and the match lifecycle" |
| `systems()` | `thrust_steering` at `kPreKernel`; `zone_shrink` then `zone_elimination` at `kPostKernel`; `placement_recorder`, `lifetime_expiry`, `hazard_spawn` then `elimination_grace_publisher` at `kLifecycle` | ADR 0004 § "The tick: one fixed kernel, three named stages" |
| `contact_rules()` | `lethal_hazard`, then `ContactRuleTable::built_in()`'s own rows | ADR 0004 § "Contact rules" |
| `accepted_command_kinds()` | spawn, despawn, thrust | ADR 0004 § "Commands" |
| `spawn_policy()` | `RotatingRingSpawnPolicy` over the map's spawn markers (§ "Spawning") | ADR 0004 § "Game modes and the match lifecycle" |
| `objective()` | `RoyaleObjective` (§ "Match lifecycle") | ADR 0004 § "Game modes and the match lifecycle" |
| `validate_map()` | rejects a map with fewer than `lobby_seat_count` markers of kind `spawn` | ADR 0004 § "Maps as data" |

The names in the `systems()` row are each system's `name()`, the stable snake_case identity the
pipeline, diagnostics, and fixtures use and the one ADR 0004 § "Game modes and the match lifecycle"
already lists for `royale`. The engine appends its own `MatchLifecycleSystem` last at `kLifecycle`
and it is not removable, so `placement_recorder` always runs before this tick's phase transition is
evaluated.

Royale uses the engine components `PhysicsBody` and `Controllable` unchanged and adds two of its
own, `Zone` and `ZoneExposure` (§ "Where zone and elimination state live"). It adds no command kind,
no event kind, and no contact rule.

**Why `contact_rules()` is the built-in table verbatim.** Royale changes no collision equation. Blob
meets blob is the accepted equal-mass exchange and blob meets wall or static body is the accepted
reflection, so the mode declares `ContactRuleTable::built_in()` and adds nothing to it (ADR 0004
§ "Contact rules"). That one line is why every accepted pair and wall fixture in ADR 0003 § "Fixture
contract and expected outcomes" stays valid without regeneration: the mode is structurally incapable
of reaching those equations.

### Scope, vocabulary, and evaluation order

Four terms are load-bearing and are used with these exact meanings throughout:

* **live** — the entity owns a `PhysicsBody`. It collides, integrates, and is published with that
  component in the snapshot.
* **pending** — the entity owns a `Controllable` and owns no `PhysicsBody`. Its spawn was accepted
  and this mode's `SpawnPolicy` has not yet seated it.
* **alive count** — the number of entities that own both a `PhysicsBody` and a `Controllable`. It is
  the only population the objective reads. The zone entity owns neither and a map's static bodies
  own no `Controllable`, so neither is ever counted.
* **eliminated** — the entity was destroyed by this tick's `placement_recorder`. The deciding
  controller outlives it and rejoins as a new `EntityId` under the same `ControllerId`, so nothing
  in this mode has to model a disembodied player (ADR 0004 § "Controllers").

Within one tick the royale rules are evaluated in exactly this order, and every rule below that
depends on relative order names the dependency rather than assuming it:

1. Kernel phase 0 applies this tick's despawns, seats pending entities through the engine
   `SpawnSystem` driven by this mode's `SpawnPolicy`, and records each remaining command into
   `Controllable::commands_this_tick`.
2. `thrust_steering` at `kPreKernel` turns this tick's thrust commands into
   `PhysicsBody::acceleration`.
3. Kernel phases 1 through 6 apply stored acceleration and drag, resolve contacts through the
   built-in table, fold walls, integrate, and rebuild the spatial index.
4. `zone_shrink` then `zone_elimination` at `kPostKernel`, in that declared order: elimination reads
   the radius this tick's `zone_shrink` wrote.
5. `placement_recorder`, `lifetime_expiry`, `hazard_spawn` then `elimination_grace_publisher` at
   `kLifecycle`, then the engine's `MatchLifecycleSystem`: the transition observes the alive count
   after this tick's eliminations have left the roster, and the mode-state block a snapshot
   publishes is what the last of the four left.

Items 4 and 5 are the accepted phases 7, 8, and 9 of ADR 0003 § "Canonical tick", re-expressed as
declared systems in the same relative order and committing the same values (ADR 0004 § "The tick:
one fixed kernel, three named stages").

**Tie policy.** Ties of any kind resolve by ascending `EntityId`. This covers command application
order within a batch, seating order, elimination evaluation order, placement-list append order
within one tick, and any future rule this ADR does not anticipate. Component stores are ascending by
construction, so every royale system gets that order for free rather than sorting for it (ADR 0004
§ "Entities, components, and stores"), and `EntityId` is a total order over `std::uint64_t`
(`src/simulation/entity_id.hpp`), so no tie is ever unresolved.

### Mode configuration

One strict configuration section, `[royale]`, carries every balance number this mode owns. The
application parses it with the existing strict INI loader and hands the validated value to the mode
factory named in `game_mode_registry.hpp` (plan Step 25). The mode holds it and hands it to the
systems it builds, which is the only way configuration reaches a tick: a system's members are
immutable configuration and `TickContext` is mode-agnostic (ADR 0004 § "The tick: one fixed kernel,
three named stages"). Every key is required, no key has a silent default, an unknown key is a
rejection, and every value must be finite.

| Key | Unit | Validation | Proposed value |
|---|---|---|---|
| `thrust_max_world_units_per_second_squared` | `wu/s²` | finite, `>= 0` | `400` |
| `zone_minimum_radius_world_units` | `wu` | finite, `>= 0`; `validate_map` additionally rejects a map whose `R_full` is not strictly greater | `60` |
| `zone_shrink_seconds` | `s` | finite, `>= 0` | `90` |
| `elimination_grace_seconds` | `s` | finite, `>= 0` | `3` |
| `lobby_seat_count` | count | integer, `>= 1` and within the engine's lobby bound; `validate_map` additionally rejects a map with fewer `spawn` markers | `4` |
| `countdown_seconds` | `s` | finite, `>= 0` | `5` |
| `restart_delay_seconds` | `s` | finite, `>= 0` | `8` |

Units appear in the key names because the value alone cannot carry them and a misread unit is a
silent balance bug rather than a parse error. The `R_min` bound is checked in `validate_map` rather
than at parse time because `R_full` is a property of the map's bounds, not of this section, and a
mode that cannot play a map must say so at startup naming the cause (ADR 0004 § "Maps as data").

**Durations are converted once, at load, into integer tick counts.** For a duration `x` in seconds,
`ticks(x) = nearest integer to (x × 400), ties away from zero`. The loader rejects a result that
does not fit the tick counter. The mode stores only the tick counts; no system sees a value in
seconds and none multiplies by 400 at runtime. No wall-clock time enters the simulation at any
point (ADR 0004 § "Determinism obligations for framework code").

The proposed values yield `countdown_ticks = 2,000`, `zone_shrink_ticks = 36,000`,
`elimination_grace_ticks = 1,200`, and `restart_delay_ticks = 3,200`.

**Thrust maximum is a mode key and drag is not.**
`thrust_max_world_units_per_second_squared` is the scale `thrust_steering` applies when it turns a
thrust command this mode accepted into `PhysicsBody::acceleration`. It is meaningless to a mode that
does not accept thrust, and two modes that both accept thrust may reasonably disagree about it, so
it belongs to the mode. Drag is applied by the kernel to every dynamic body in every mode and
belongs to the kernel (§ "Drag is a kernel parameter, not a mode parameter").

**Which match is played is not this mode's configuration.** The application selects it:

```
[match]
mode=royale
map=arena-960x640
bots=wanderer:2, chaser:1
```

`mode` resolves through `game_mode_registry.hpp`, `map` names a directory under `maps/`, and `bots`
names controller kinds and counts resolved through `controller_registry.hpp` (plan Steps 25 and 31;
ADR 0004 § "Controllers"). None of the three is readable from inside a tick, and no royale rule
knows whether a command came from a browser or a bot.

Balance values are configuration. Changing a number in the table above requires no amendment to this
ADR. Changing a *rule* — the drag law, the zone shape, the elimination predicate, the objective, or
the spawn policy — does.

### Drag is a kernel parameter, not a mode parameter

Drag lives in `[simulation] drag_per_second` and is applied by kernel phase 1, immediately after
stored acceleration, for every dynamic body in ascending `EntityId` order (ADR 0003 § "Canonical
tick"):

```
v_accelerated = v_previous + acceleration × dt
damping       = max(0, 1 - drag_per_second × dt)
v             = (v_accelerated.x × damping, v_accelerated.y × damping)
```

`dt` is the single canonical binary64 seconds value of `FixedDelta`, `kFixedDeltaSeconds` in
`src/simulation/simulation_limits.hpp`, the same constant the acceleration step already uses.

It is not a `[royale]` key for three reasons. It is a property of the medium rather than of the
game: every dynamic body in the world moves through the same damped integrator whichever mode is
loaded, and two modes sharing an arena should not disagree about how a coasting body decelerates. It
is inside the kernel, which is mechanism that no mode may reorder, skip, or replace (ADR 0004 § "The
tick: one fixed kernel, three named stages"), so a mode-scoped drag would be a mode editing a kernel
equation. And the zero-drag identity that keeps every accepted fixture horizon valid is a property
of the kernel, asserted once for every mode rather than once per mode.

**Every fixture and test configuration sets `drag_per_second=0`; only the deployment configuration
sets `2.0`** (plan § "Execution constraints"). At zero, `drag_per_second × dt` is exactly `+0.0`,
`damping` is exactly `1.0`, and `x × 1.0 == x` for every finite binary64 `x` including both signed
zeros, so the step is the identity on velocity and no horizon moves.

Drag is still a balance input for this mode, so the two numbers a designer needs are recorded here.
Under constant full thrust the continuous-limit terminal speed is
`thrust_max_world_units_per_second_squared / drag_per_second`, which is `200 wu/s` at the proposed
values and the figure to reason with when choosing balance numbers. The exact fixed point of the
discrete recurrence above is `thrust_max × (1 - drag_per_second × dt) / drag_per_second`, which is
`199 wu/s` — a `0.5 %` shortfall that scales with `drag_per_second × dt`, and the value a fixture
asserts. A `drag_per_second` at or above `400` produces a non-positive raw damping factor, is
clamped to zero, and brings every body to a full stop each tick; that is a degenerate configuration,
not an error, and it never reverses velocity.

### Where zone and elimination state live

Both are components, not mode match state.

* **`Zone { center, radius }`** is carried by one entity. `zone_shrink` creates that entity from the
  tick's `EntityIdReservation` on the first tick it observes no entity carrying `Zone`, and assigns
  the component on every tick after that. The zone entity owns no `PhysicsBody` and no
  `Controllable`, so it never enters a contact pair, never integrates, and is never counted alive.
  It persists across matches.
* **`ZoneExposure { outside_ticks }`** is the per-entity grace counter, written by
  `zone_elimination`. An absent `ZoneExposure` reads as zero, so nothing has to seed it at spawn.

Both are registered in `component_registry.hpp` as `blob_simulation` value structs with no behavior,
and everything they *do* is `blob_gameplay` systems. That is the "values live in `blob_simulation`,
rules live in `blob_gameplay`" rule of ADR 0004 § "Libraries, and where a new thing goes" applied
literally, and it costs `blob_simulation` two headers and one registry line while keeping one
`GameWorld` type, one `WorldSnapshot` type, and one oracle.

Registration there buys three generated behaviors that mode-owned match state would not get:
structural `operator==` over the whole world, erasure by `destroy_entity`, and snapshot
participation (ADR 0004 § "Entities, components, and stores"). The third is why this is not a matter
of taste. A component is snapshot-visible by construction, so the radius a client renders is exactly
the radius `zone_elimination` tested against on the tick being rendered, and a per-entity grace
counter is comparable between two runs at the first differing tick instead of being invisible until
someone is eliminated. The second is why "a spawned entity starts with a zero counter" needs no
rule at all: an eliminated entity is destroyed, and its controller rejoins under a new `EntityId`
carrying no `ZoneExposure` (ADR 0004 § "Controllers").

The per-entity counter could not have been match state in any case: state that is per entity and
durable across ticks is a component by the rule ADR 0004 § "The tick: one fixed kernel, three named
stages" states, and `MatchState` is for match-wide state. The zone is match-wide and is still a
component, because ADR 0004 § "Snapshots and protocol shape" asks that mode state be components
wherever it can be and names the zone as the case that proves the model carries its weight.

Royale's mode state in `MatchState` therefore carries what is not entity-shaped: the ordered
placement list (§ "Elimination and placement") and `previous_phase`, the lifecycle phase this mode
observed on the previous tick (§ "Match lifecycle"). The engine's own `MatchState` fields — phase,
phase start tick, running start tick, and the committed `MatchOutcome` — are engine-owned and this
mode adds nothing to them and restates none of them (ADR 0004 § "Game modes and the match
lifecycle").

It carries one thing more, and it is a different kind of thing: `elimination_grace_ticks`, which is
`G` from § "Elimination and placement" copied out of the validated `[royale]` configuration. It is
not something royale observed; it is something royale was told. It is here because the block is the
mode's contribution to every published snapshot, and a client already receives every entity's
`ZoneExposure` counter but has no way to learn the bound that counter is tested against — so the
elimination rule reads to a player as arbitrary, which is exactly what the 2026-09-07 playtest
reported. `elimination_grace_publisher` is the one system that writes it, declared last at
`kLifecycle` so it is the final writer of the block within a tick; **no royale rule reads it back**,
because `zone_elimination` holds its own copy of the configuration and always will.

### Steering

`thrust_steering` is a `kPreKernel` system. It reads each live entity's
`Controllable::commands_this_tick` in ascending `EntityId` order and writes
`PhysicsBody::acceleration`. Command meaning is a system's job; the kernel records commands and does
not interpret them (ADR 0004 § "Commands").

A thrust command carries a direction vector `(x, y)` with each component in `[-1, 1]`. Components
outside that range or non-finite are rejected in `InputBatch::create` and never reach the world
(ADR 0004 § "Commands"; plan Step 16). The system clamps the vector's magnitude to at most one and
scales it by the configured maximum:

```
m = sqrt(x × x + y × y)
s = 1        when m <= 1
s = 1 / m    when m > 1
acceleration = ((x × s) × thrust_max, (y × s) × thrust_max)
```

The written operation order above is the contract; ADR 0003 § "Floating-point contract" already
forbids reassociating it. A command inside the unit disc keeps its magnitude, so an analog stick
produces proportional thrust. `(1, 1)` normalizes to `(0.7071…, 0.7071…)` and yields an acceleration
of magnitude exactly `thrust_max`, so diagonal movement carries no advantage. `(0, 0)` stores zero
acceleration, which is the coast command.

Thrust writes the stored-acceleration field that ADR 0003 § "State, units, and fixed time" already
defines, and it persists unchanged until the next thrust command for that entity, exactly as the
baseline specifies. There is no per-tick decay, reset, or implicit zeroing. A thrust naming an
entity that owns no `PhysicsBody` has nothing to write and is skipped.

`kPreKernel` is the correct stage by the rule ADR 0004 § "The tick: one fixed kernel, three named
stages" gives for it: a system there reads start-of-tick positions and this tick's recorded commands
and writes body intent, and nothing has moved yet. An entity seated by phase 0 of this tick can
therefore be thrust in the same tick, and phase 1 applies the acceleration this system wrote.

### Safe zone

`zone_shrink` is the first `kPostKernel` system. It writes the `Zone` component and nothing else.

The zone is a circle centered on the arena center, and its center never moves:

```
zone_center = (bounds.width / 2, bounds.height / 2)
R_full      = sqrt((bounds.width / 2)² + (bounds.height / 2)²)
R_min       = zone_minimum_radius_world_units
```

Bounds come from the map through `TickContext::map()` (ADR 0004 § "Maps as data"), so the same mode
plays a different arena with no configuration change.

`R_full` is the circumscribed radius of the arena rectangle, so the whole arena starts inside the
zone. This is why no player can be outside at the moment `running` begins: ADR 0003 confines a
committed center to `[r, width - r] × [r, height - r]`, whose farthest point from the center is
`sqrt((width/2 - r)² + (height/2 - r)²)`, strictly less than `R_full` for any valid `r > 0`.

Let `e = tick_sequence - running_started_tick` be the number of committed ticks since `running`
began, and let `T = zone_shrink_ticks`. The radius is a pure function of `e`:

```
radius(e) = R_min                                        when T == 0, or when e >= T
radius(e) = R_full - (R_full - R_min) × (e / T)          when 0 <= e < T
```

The division precedes the multiplication in the written form and the implementation must preserve
that order. The `e >= T` branch returns `R_min` by assignment rather than by arithmetic, so the held
radius is exactly `R_min` rather than a rounding of it, and the `T == 0` case is defined instead of
dividing by zero.

The radius is a pure function of an integer tick difference, so the zone carries no accumulated
floating-point state: any committed snapshot fully determines every subsequent zone radius, and a
replay from an arbitrary snapshot reproduces the zone exactly.

The radius the system writes, by committed phase:

| Phase | Zone radius |
|---|---|
| `lobby` | `R_full` |
| `countdown` | `R_full` |
| `running` | `radius(tick_sequence - running_started_tick)` |
| `ended` | `radius(phase_started_tick - running_started_tick)`, frozen at the final `running` value |

Both boundary ticks are consistent without a re-evaluation after the transition, which is worth
showing because the transition commits at `kLifecycle`, after this system has already run. On the
tick that enters `running`, `zone_shrink` still observes `countdown` and writes `R_full`, which is
exactly what `radius(0)` returns for any `T > 0`, so the first snapshot that reports phase `running`
reports a radius consistent with it. On the first `ended` tick, `phase_started_tick` is the tick the
transition committed, so the frozen value is exactly what the last `running` tick wrote.

The one exception is the degenerate `zone_shrink_seconds = 0`, where `radius(0)` is `R_min` and that
first `running` snapshot still carries `R_full`. Nothing is decided on it: elimination evaluates
only when the committed phase is already `running`, so the published circle is one tick late rather
than in conflict with a rule.

For a `960 × 640` arena, `zone_center = (480, 320)` and `R_full = sqrt(332,800) ≈ 576.8882 wu`. With
the proposed values the zone contracts at about `5.743 wu/s`, roughly 35 times slower than the
`200 wu/s` terminal speed, so a thrusting player can always outrun the boundary.

### Elimination and placement

`zone_elimination` is the second `kPostKernel` system and evaluates only when the committed phase is
`running`. It reads `PhysicsBody::position` and the `Zone` this tick's `zone_shrink` wrote, writes
`ZoneExposure`, and emits `EliminationEvent`s. It removes nothing from the roster: a `kPostKernel`
system writes consequences, and roster bookkeeping is `kLifecycle` work (ADR 0004 § "The tick: one
fixed kernel, three named stages").

It reads the player's **center**, not its disc: a player may overlap the boundary and remain safe.

```
distance = sqrt((p.x - zone_center.x)² + (p.y - zone_center.y)²)
outside  = distance > radius + ε_position
```

`ε_position` is the `1e-9 wu` tolerance from ADR 0003 § "Floating-point contract". A center exactly
on the boundary is inside, consistent with the baseline's inclusive contact rule.

Per tick, for every live entity in ascending `EntityId` order, with `G = elimination_grace_ticks`:

* An entity that is inside sets `outside_ticks = 0`. Re-entering the zone therefore resets the
  counter and any partial grace is lost.
* An entity that is outside increments `outside_ticks` and, when `outside_ticks >= G`, is named in
  one `EliminationEvent`.

Increment precedes the test, so `G = 0` eliminates an entity on the first tick its center is outside
and `G = 1,200` eliminates it on the 1,200th consecutive outside tick. An entity that is inside is
never tested, so `G = 0` does not eliminate a safe player.

**Every `ZoneExposure` counter is zero when `running` is entered, and no rule has to reset it.** The
zone covers the whole arena during `lobby` and `countdown` (§ "Safe zone"), so no counter is ever
incremented before `running`; an eliminated entity's counter dies with the entity; and every entity
still alive when a match ends is destroyed on the first `lobby` tick (§ "Match lifecycle").

`placement_recorder` is the first of royale's `kLifecycle` systems and runs before the engine's
`MatchLifecycleSystem`. Each tick it performs four steps in this order:

1. When the committed phase is `running` and `previous_phase` is `countdown` — the first tick of a
   match — clear the placement list.
2. Read this tick's `EliminationEvent`s, compute one placement for the whole set, append one entry
   per entity in ascending `EntityId` order, and destroy each of those entities, emitting a
   `DespawnEvent` for each.
3. When the committed phase is `lobby` and `previous_phase` is `ended`, destroy every entity that
   owns both a `PhysicsBody` and a `Controllable`.
4. Record the committed phase as `previous_phase`.

The written order is the contract, and the reason is the one case where two of the steps coincide.
Steps 1 and 3 are mutually exclusive because one tick's committed phase cannot be both `running` and
`lobby`, and step 3 never coincides with step 2 because eliminations occur only during `running`.
Step 1 *can* coincide with step 2 — under a degenerate `zone_shrink_seconds = 0` and
`elimination_grace_seconds = 0`, a match's first `running` tick both starts the match and eliminates
— and step 1 running first is what guarantees the previous match's ranking is cleared before this
match's first placements are appended rather than after. Step 4 records the phase `MatchState` holds
while this system runs, which is the phase the previous tick committed; the engine's transition for
this tick has not run yet.

Placement is computed once per tick over the whole eliminated set, not once per entity:

```
eliminated_count = number of EliminationEvents this tick
alive_after      = alive count after step 2 has destroyed them
placement        = alive_after + 1     for every entity in the eliminated set
```

Entities eliminated in the same tick therefore share one placement. Each is appended as
`(entity_id, placement, elimination_tick)` in ascending `EntityId` order, where `elimination_tick`
is the tick sequence being committed. The list is bounded by `kMaximumPlayerCount`
(`src/simulation/simulation_limits.hpp`), the same bound the roster already carries.

Two consequences of the formula are worth stating so nobody re-derives them. A match that ends with
a winner assigns placement `2` to the last eliminated entity; the winner receives no placement entry
and is identified by the committed `MatchOutcome`, holding placement `1` implicitly. A match that
ends in a draw, where the final entities are eliminated in the same tick, gives `alive_after = 0`
and assigns those entities the shared placement `1`, which is the correct report for a mutual
finish.

Because step 2 destroys before the engine's `MatchLifecycleSystem` reads `outcome`, an elimination
and the end it causes commit in the same tick, and the placement list a snapshot carries always
agrees with the alive count that snapshot reports.

The list survives the whole `ended` phase and the `lobby` and `countdown` that follow, and is
cleared on the first `running` tick of the next match — which is also the first tick that can append
to it. A client therefore has the finished ranking on screen for the entire restart delay.

### Match lifecycle

The four-phase machine is engine-owned and generic. Royale supplies three predicates and two
durations through `MatchObjective` and declares no transition logic of its own (ADR 0004 § "Game
modes and the match lifecycle").

| Member | What `RoyaleObjective` returns |
|---|---|
| `can_start(world)` | true when every seat in `MatchState::seats` is filled **and** a start has been requested |
| `outcome(world)` | `won_by_entity` naming the single alive entity when the alive count is `1`; `drawn` when it is `0`; `undecided` otherwise |
| `durations()` | `countdown_ticks` and `restart_delay_ticks` from `[royale]` |

All three are total in every phase, which is what lets the engine call them without a guard. The
engine consults `outcome` only for the `running → ended` transition, so the `drawn` that an empty
lobby would return is never committed. A despawn reduces the alive count exactly as an elimination
does, so a disconnect can end a match, and neither the objective nor any royale system has to know
which happened.

What each phase means for royale:

| Phase | Zone radius | Spawn requests | Elimination | Placement list |
|---|---|---|---|---|
| `lobby` | `R_full` | seated, except on the first tick after `ended` | not evaluated | readable; cleared at the next `running` |
| `countdown` | `R_full` | seated | not evaluated | readable |
| `running` | `radius(tick_sequence - running_started_tick)` | deferred | evaluated every tick | appended |
| `ended` | frozen at the final `running` value | deferred | not evaluated | readable |

The one rule royale attaches to a transition is the restart wipe: on the first `lobby` tick after
`ended`, `placement_recorder` destroys every entity that is still alive — the winner, or nobody at
all after a draw — and `RotatingRingSpawnPolicy` defers seating on that same tick so the wipe cannot
delete an entity the tick just seated (§ "Spawning"). Without the wipe the winner would carry its
position, velocity, and stored acceleration into the next match and would never be re-seated on the
ring. `previous_phase` is what makes both halves expressible as an observation of committed state
rather than as a hook into the engine's transition, and it is what distinguishes `ended → lobby`
from `countdown → lobby`, which changes nothing but the phase and its start tick.

Exactly one `lobby` tick therefore has an alive count of zero after every match, and the controllers
whose entities are gone rejoin by spawn command like any other joiner (ADR 0004 § "Controllers").
Rejoining is a decision made outside the tick, so how quickly a lobby refills is a controller
property and never a simulation one.

**At most one phase transition commits per tick.** That is engine behavior this mode relies on
rather than restates (ADR 0004 § "Game modes and the match lifecycle"), and it is what keeps royale
total under a zero-duration configuration: without it, `countdown_seconds = 0` and
`restart_delay_seconds = 0` would chain `lobby → countdown → running → ended → lobby` inside one
tick and the transition evaluation would not terminate. With it, a zero-length phase occupies
exactly one tick, so such a configuration costs one tick per transition plus the zero-alive `lobby`
tick and the seating tick after it, and cycles every few ticks instead of hanging.

All four durations are the integer tick counts derived at load. No royale rule reads a clock, and
`TickContext` exposes none (ADR 0004 § "Determinism obligations for framework code").

### Spawning

Spawn positions are map data. `RotatingRingSpawnPolicy` chooses an index into `map.spawn_points()`,
the ordered projection of the map's markers of kind `spawn` (ADR 0004 § "Maps as data"), or returns
`nullopt` to defer the entity one tick. It computes no geometry and holds no state:

```
if the committed phase is running or ended: return nullopt
if the committed phase is lobby and previous_phase is ended: return nullopt
n = spawn_point_is_free.size()
for probe in [0, n):
  k = (rotation_counter + probe) mod n
  if spawn_point_is_free[k]: return k
return nullopt
```

The engine's `SpawnSystem` owns everything else: ascending-`EntityId` iteration over pending
entities, the occupancy test that produced `spawn_point_is_free`, the world-owned rotation counter
and its advance, and the seating write that gives the entity a `PhysicsBody` at rest — zero velocity
and zero stored acceleration — at the chosen point (ADR 0004 § "Game modes and the match
lifecycle"). A point is occupied when a live entity's center lies within `2r + ε_position` of it,
which is the baseline contact predicate of ADR 0003 § "Player-pair policy", so an entity seated
earlier in the same tick occupies its point for every later seating in that tick and two entities
are never seated in contact. A full ring defers the entity one tick; no point can free while
positions are frozen, so the deferred entity loses nothing by waiting, and the per-tick probe cost
is bounded by one pass over the points per pending entity.

The rotation is the whole of royale's policy: consecutive joiners are spread around the map's points
instead of stacking on the first free one, and the counter is world state, so seating is a
deterministic function of the committed world.

**Deferring during `running` and `ended` is the mode's rule, and it is what makes a match a closed
field.** A joiner who arrives mid-match waits for the next `lobby` rather than appearing inside a
shrinking circle with no chance of placing. The second deferral covers the single `lobby` tick on
which `placement_recorder` clears the arena (§ "Match lifecycle"); the policy reads the same
`previous_phase` the recorder does, at kernel phase 0 of the same tick and before the recorder
updates it, so the two rules cannot disagree about which tick that is.

**The ring of 32 is the shipped map's data, not mode arithmetic.** `maps/arena-960x640` ships 32
`spawn` markers in `markers.csv` (plan Step 25), authored from

```
slot_count  = 32
ring_radius = 0.75 × (min(width, height) / 2 - player_radius)
slot(k)     = (center.x + ring_radius × cos(2πk / 32),
               center.y + ring_radius × sin(2πk / 32))
```

with slot `0` on `+x` and the index increasing counter-clockwise. For the `960 × 640` arena and a
`10 wu` player radius that is `ring_radius = 232.5 wu` and an adjacent-slot chord of
`2 × 232.5 × sin(π/32) ≈ 45.58 wu`, comfortably above `2r = 20 wu`, so all 32 points are
simultaneously seatable. The layout is in bounds for the same reason it always was: writing
`m = min(width, height) / 2`, the requirement `0.75(m - r) <= width/2 - r` and
`0.75(m - r) <= height/2 - r` reduces to `r <= width/2` and `r <= height/2`, which follow from the
accepted `width > 2r` and `height > 2r`.

Recording the formula lets another map reproduce the layout; it is not evaluated at simulation time.
The markers are decimal literals in a CSV row, correctly rounded by any conforming `strtod`, so no
`cos` or `sin` runs inside a tick and the cross-toolchain reproducibility of the trigonometric
functions — which ADR 0003 § "Floating-point contract" would only have guaranteed to `ε_position`
anyway — never arises. A marker outside the arena is a map-load rejection, so the in-bounds property
is validated as data rather than proved as arithmetic.

`validate_map` requires at least `lobby_seat_count` markers of kind `spawn`, because a map with
fewer could not seat a full lobby, so a started match would leave joiners pending forever. A map with more
than 32 points, or fewer, is valid; the count is data. A layout whose adjacent chord falls below
`2r` is also valid: the occupancy test simply seats fewer entities per tick and the rest defer.

### Roster edge rules

These close the cases where a command disagrees with committed world state. The rule is fail-soft at
the untrusted boundary and fail-hard only on internal invariant violations, because the command
source is a network session and a hard failure would let one client stop the match.

* A spawn naming an entity that is already live or already pending is ignored. Session admission
  allocates the `EntityId` from the tick's `EntityIdReservation` (plan Step 22; ADR 0004
  § "Determinism obligations for framework code"), so a duplicate means the caller's model diverged,
  not that the world is inconsistent.
* A despawn naming an entity that is neither live nor pending is ignored. Close paths can run more
  than once.
* A despawn removes the entity from wherever it is — live or pending — and drops any placement it
  has not yet earned. A despawned entity is not re-seated; its controller rejoins by sending a new
  spawn.
* A thrust naming an entity that owns no `PhysicsBody` is ignored. `InputBatch::create` has already
  rejected a malformed one, and a well-formed one for a pending entity simply has nothing to write.
* An entity that both spawns and despawns in one batch is a rejection at the boundary rather than a
  race inside the tick (ADR 0004 § "Commands").
* Where a rule depends on ordering, the order is § "Scope, vocabulary, and evaluation order": the
  zone radius, then eliminations, then placements and the roster, then the single phase transition.

### Fixture expectations

**Baseline preservation.** Three conditions together reproduce every accepted ADR 0003 horizon
bit-for-bit: `drag_per_second = 0`, an empty `InputBatch`, and a mode whose systems do nothing —
either a test-only mode with an empty system list, or `sandbox`, whose only system is
`thrust_steering` and whose `kPostKernel` and `kLifecycle` lists are empty (ADR 0004 § "Game modes
and the match lifecycle"), so an empty batch leaves it with no command to interpret. Under those
three, phase 0 is a no-op, phase 1's damping is exactly `1.0`, the contact table is the built-in two
rows evaluating the accepted equations, and no stage contributes a system that touches a body. That
is the compatibility proof, and no fixture is regenerated.

**A seeded fixture world that runs under `royale` stays in `lobby` unless its command log fills
every seat and requests a start.** In `lobby` the radius is `R_full` and elimination is not
evaluated, so no royale system touches a body. Two additions to the world remain and must be
expected rather than asserted away: the zone entity and its `Zone` component, which own no
`PhysicsBody` and never reach the physics kernel, and the one `EntityId` drawn from the first tick's
reservation to create it.

**The primary Royale fixture is a replay.** ADR 0004 § "Determinism obligations for framework code"
fixes the format — a match is reproducible from `(map, mode configuration, seed, command log)` — and
plan Step 21 puts the suite under `tests/fixtures/replays/`. A replay fixture is a directory:
`match.ini` naming the mode, the map, the seed, and the `[royale]` values; `commands.csv` with one
row per command as `tick_sequence,entity_id,command_kind,<payload columns>`; and either expected
snapshot digests at named checkpoint ticks or named invariants. Every number in this ADR is asserted
there rather than in a hand-built world, because the same tuple is what a bug report and a replay
viewer carry.

A first suite covers six scenarios, one for each rule that can be wrong on its own:

| Scenario | What it pins |
|---|---|
| Thrust integration | Constant full thrust at zero drag reaches `thrust_max × t`; `(1, 1)` yields magnitude exactly `thrust_max`; acceleration persists with no further command. |
| Drag decay | At `drag_per_second = 2.0`, the per-tick damping factor, the coast-down from a set velocity, and the `199 wu/s` discrete fixed point rather than the `200 wu/s` continuous limit. |
| Spawn order | Joiners in one tick take consecutive points from the rotation counter; a full ring defers exactly one tick; two entities are never seated in contact; a joiner during `running` is deferred. |
| Elimination timing | Elimination on exactly the `G`th consecutive outside tick; re-entry resets the counter and loses partial grace; a center exactly on the boundary is inside; `G = 0` eliminates on the first outside tick and never a safe player. |
| Simultaneous elimination and draw | Two entities leaving together share one placement; the final two share placement `1` and the committed outcome is `drawn`. |
| Transition per tick | `countdown_seconds = 0` and `restart_delay_seconds = 0` advance one phase per tick and terminate; a one-seat lobby cycles instead of hanging, one requested start at a time. |

The 100-fresh-run bit-identity rule of ADR 0003 is asserted over a scripted multi-entity `royale`
replay from this suite (plan Step 21), which is the first time that rule covers a whole match rather
than a physics horizon.

### Match section fields

Protocol v2's `match` section carries the generic lifecycle header and one mode-state block
identified by a schema id (ADR 0004 § "Snapshots and protocol shape"). This ADR names fields; it
does not define a wire encoding. The JSON representation, field names, and schemas are
`docs/protocol/v2.md` (plan Step 14), and `@extension-point snapshot_encoding`
(`docs/architecture/0002-simulation-architecture.md` § "Extension points") keeps that change inside
`blob_protocol`.

* **mode name** — `royale`.
* **phase** — one of `lobby`, `countdown`, `running`, `ended`.
* **phase start tick** and **running start tick** — the tick sequences at which the current phase
  and the current match's `running` began.
* **outcome** — the committed `MatchOutcome`: undecided, won by entity, or drawn.
* **mode state**, schema id `royale_placements` — the ordered
  `(entity_id, placement, elimination_tick)` list bounded by `kMaximumPlayerCount`,
  `previous_phase`, and `elimination_grace_ticks`, the bound each published `ZoneExposure` counter
  is tested against (§ "Where zone and elimination state live").

**Zone center and radius are not `match` fields.** They are the `Zone` component of the zone entity
and travel in the snapshot's entity list with every other component, encoded by the `Zone` encoder
and drawn by the `Zone` circle renderer, which is one of the two implementations ADR 0004
§ "Justified extension points and what-if stress" registers for `@extension-point entity_renderer`.
Publishing the same circle twice, once as a component and once as a match field, would give a client
two sources for one value and a way to disagree with itself. **Alive count is not a field either:**
it is the number of published entities carrying both `PhysicsBody` and `Controllable`, which the
client counts from the array it is already rendering.

Snapshots remain immutable, copy-owned publication values created only after a complete tick
(`src/simulation/README.md` § "Ownership and invariants"). Nothing above is derived at read time on
the server.

### Justified extension points and what-if stress

This mode introduces no seam of its own. It is an implementation of seams ADR 0004 already accepted,
which is the point: the deferral ADR 0002 § "Adopt an ECS or actor-per-player model" recorded was
discharged by the framework, not by this game.

* **What if the zone must move rather than only shrink?** `zone_shrink` writes both fields of `Zone`
  already; make the center a function of the same integer `e`. The elimination predicate reads the
  component's center, the snapshot publishes it, and the client renders it. This is a versioned rule
  change because match outcomes change; it needs no new state, no new component, and no new seam.
* **What if more than 32 players must spawn at once?** Add rows to `markers.csv`, or ship a second
  map. `RotatingRingSpawnPolicy` is indexed over `map.spawn_points()` and never sees the count, so
  this is a data change with no code at all (ADR 0004 § "Maps as data").
* **What if placement must rank sub-tick order?** That requires time-of-impact resolution inside the
  tick, which ADR 0003 § "Justified extension points and what-if stress" already routes through a
  continuous-collision amendment. Shared placement is the honest report until that lands.
* **What if drag must be non-linear, or a speed cap is wanted anyway?** Drag is one scalar applied
  at one named kernel point, so replacing it is a versioned physics change on ADR 0003's path and
  not a mode's decision. A mode-scoped speed cap is *available* — a `kPostKernel` system may write
  velocity — and is rejected here for the same reason Option D was: a cap applied after the contact
  phase discards normal-component energy and breaks the accepted conservation assertions.
* **What if a team royale is wanted?** `Team` is registered from day one and `MatchOutcome` already
  has `won_by_team` (ADR 0004 § "Game modes and the match lifecycle"). The objective changes and
  every rule in this ADR is untouched, because nothing here reads team membership.
* **What if a second zone, or a per-team zone, is wanted?** A second entity carrying `Zone`.
  `zone_elimination` becomes a rule about which zones an entity must be inside; the component, the
  encoder, and the renderer are unchanged.

## Consequences

* **Positive:** Determinism is preserved end to end. Every duration is an integer tick count fixed
  at load, the zone radius is a pure function of an integer tick difference, every tie resolves by
  ascending `EntityId`, and every royale system is a `const` `apply` over world-owned state, so ADR
  0003's 100-fresh-run bit-identity requirement extends to whole matches with no new machinery.
* **Positive:** The zero-drag baseline is bit-identical, not merely close, and it is now a kernel
  property that every mode inherits rather than a claim this mode has to make. Every accepted ADR
  0003 fixture horizon stays valid unchanged, which is what lets gameplay land without regenerating
  physics fixtures.
* **Positive:** Royale adds no physical equation. `contact_rules()` is the built-in table verbatim,
  thrust writes the stored-acceleration field the baseline already carries, and the zone and
  elimination systems only read positions and write mode-owned components. The collision, wall, and
  integration mechanism is untouched and structurally unreachable from this mode.
* **Positive:** The whole game is new files plus registration: `src/gameplay/royale/`, two component
  headers with their encoders and client renderers, one line in `component_registry.hpp`, one line
  in `game_mode_registry.hpp`, and a map directory. The kernel, `sandbox`, the runtime, and the
  server are unchanged by it, and `blob_protocol` and the client gain only registered entries
  (ADR 0004 § "Libraries, and where a new thing goes"). This is the first real test of that claim.
* **Positive:** The zone holds no accumulated state, so any committed snapshot determines every
  future zone radius. A replay or a debug session can start from an arbitrary snapshot.
* **Positive:** Phase, phase start tick, outcome, placements, and the zone circle are all committed
  state, so the client renders the whole match from the snapshot alone and needs no local timers to
  stay in agreement with the server.
* **Positive:** Balance lives in `[royale]` and layout lives in `maps/`. Retuning thrust, shrink
  rate, or grace after a playtest is a configuration edit, and moving or adding spawn points is a
  CSV edit; neither is an architecture change.
* **Negative:** Elimination is evaluated once per tick, so two players who leave the zone at
  different sub-tick moments share one placement, and the placement list's within-tick ascending
  `EntityId` order is presentation order, not rank.
* **Mitigation:** Accept one-tick placement granularity as the honest resolution of a `2.5 ms`
  fixed-step model; a finer rank requires the continuous-collision amendment path ADR 0003 already
  documents.
* **Negative:** The shipped map's 32 points cap seating at 32 entities per tick and, while nobody
  has moved off a point, cap the simultaneously seatable roster at 32 — far below
  `kMaximumPlayerCount` of 4,096. A 33rd joiner re-probes every tick and waits.
* **Mitigation:** The deferral is bounded work and harmless at the prototype's two-to-eight player
  scale, and players drift off their points within a second of spawning, which frees them. Raising
  the count is now rows in `markers.csv` rather than a change to any rule in this ADR.
* **Negative:** Nonzero drag removes the elastic energy invariant. Kinetic energy is no longer
  conserved across a tick, so ADR 0003's conservation assertions hold only at `drag_per_second = 0`.
* **Mitigation:** Every fixture and test runs at zero drag by contract; conservation is asserted
  there and nowhere else. Drag is a deployment-only value.
* **Negative:** The advertised terminal speed `thrust_max / drag_per_second` is the continuous-limit
  figure; the discrete fixed point is lower by a factor of `(1 - drag_per_second × dt)`, which is
  `0.5 %` at the proposed values.
* **Mitigation:** Both formulas are stated above, and the exact discrete value is the one the replay
  suite asserts. The gap grows only with `drag_per_second`, which the `max(0, …)` clamp bounds.
* **Negative:** `lobby_seat_count = 1` is degenerate: one player fills the only seat, presses
  Start, and `running` immediately observes an alive count of `1`, so the match ends after the two
  configured durations plus a handful of transition ticks.
* **Mitigation:** The engine's one-transition-per-tick rule keeps that sequence terminating and
  observable rather than a hang, and the start request is one-shot -- cleared on every transition
  into `lobby` -- so the match stops there instead of cycling. It is a configuration choice, not a
  contract defect; the proposed value is `4`.
* **Negative:** `previous_phase` is mode state that duplicates knowledge the engine already has, and
  it makes royale react to a transition on the tick *after* the engine commits it.
* **Mitigation:** Neither reaction is observable a tick early: the placement clear lands on the
  first tick that could append to the list, and the restart wipe lands on the single zero-alive
  `lobby` tick that the rule exists to produce. The alternative is a transition callback on
  `GameMode`, which would be an eighth declaration every mode must implement to serve one mode's
  need — the interface change ADR 0004 § "Game modes and the match lifecycle" deliberately avoided.
* **Negative:** The zone is an entity that owns no body, so the snapshot contains an entity a naive
  client would try to draw as a disc.
* **Mitigation:** The client renders through a registry keyed by component kind and reports an
  unknown kind once per connection at `warn` (ADR 0004 § "Snapshots and protocol shape"), so a
  client with no `Zone` renderer skips it visibly rather than drawing nonsense.
* **Operational:** `[royale]` and `[simulation] drag_per_second` must be added to
  `config/blob-royale.cfg`, `deploy/ubuntu-pc/blob-royale.cfg`,
  `frontend-react/e2e/fixtures/blob-royale-browser-e2e.cfg`, and the fuzz corpus configurations
  under `tests/fuzz/corpus/application/`, with `drag_per_second=0` everywhere except the deployment
  file (plan Steps 25 and 31). A missing section is a startup rejection, not a default.
* **Operational:** `maps/arena-960x640` must ship with the 32 `spawn` markers described above so the
  deployed arena reproduces today's geometry (plan Step 25). A map with fewer than
  `lobby_seat_count` of them is rejected by `validate_map` at startup, naming the map and the
  cause.
* **Operational:** Per-tick royale cost is three linear passes over ascending component stores — one
  for thrust, one for elimination, one over this tick's events for placements — plus one pass over
  the map's spawn points per pending entity. No unbounded scan is introduced.
* **Operational:** The proposed values give a `5 s` countdown, a zone that reaches its `60 wu`
  minimum after `90 s` of `running`, and an `8 s` restart. A match can outlast the shrink if every
  survivor stays inside the final circle, since elimination only touches players outside it. A
  playtest that finds either figure wrong changes numbers, not rules.
* **Reversibility:** Deleting this game is deleting `src/gameplay/royale/`, its two component
  headers, two registry lines, the `[royale]` section, and the map directory. Nothing else refers to
  it: with `drag_per_second = 0`, an empty `InputBatch`, and any mode whose systems do nothing,
  `GameSimulation::step` reproduces the ADR 0003 baseline bit-for-bit, which is both the
  compatibility proof and the exit path — and it holds without touching world ownership, runtime
  publication, protocol encoding, or the server boundary.

## Related

* [`0002-simulation-architecture.md`](0002-simulation-architecture.md) — ownership, the dependency
  direction that puts this mode in `blob_gameplay`, the OOP policy that admits `GameMode`, and
  § "Extension points", which reserves `simulation_input`, `game_mode`, `simulation_system`,
  `map_definition`, and `snapshot_encoding`.
* [`0003-deterministic-simulation-contract.md`](0003-deterministic-simulation-contract.md) — units,
  exact `FixedDelta`, the canonical tick this mode's systems hang off, the drag step and the
  `[simulation] drag_per_second` key, contact and wall policy, tolerances, and the 100-fresh-run
  bit-identity rule.
* [`0004-gameplay-architecture.md`](0004-gameplay-architecture.md) — every interface this mode
  implements: components and stores, the staged tick, world events, contact rules, `GameMode`,
  `SpawnPolicy`, `MatchObjective`, maps, commands, controllers, snapshots, and the determinism
  obligations that bind mode code.
* [`../../.claude/plans/2026-09-06-playable-prototype-tailnet.md`](../../.claude/plans/2026-09-06-playable-prototype-tailnet.md)
  — accepted execution constraints, Step 12 which accepts this ADR, Step 21 which implements the
  mode and the replay suite, Step 25 which wires `[match]`, `[royale]`, and the map, and Step 31
  which deploys them.

**Amended 2026-09-07:** Royale declares hazards. It no longer returns
`ContactRuleTable::built_in()` verbatim: it declares `lethal_hazard` above the built-in rows, and
that row computes no physics at all — it returns both bodies unchanged and emits one
`EliminationEvent` — so the mode is still structurally incapable of reaching a different collision
equation for a pair of ordinary blobs, which is what § "The mode declaration" was really claiming.
`kLifecycle` gains `lifetime_expiry` and `hazard_spawn` beside `placement_recorder`, both of which
live in `src/gameplay/shared/` because objects crossing an arena are a mode-agnostic mechanic.
Nothing above about the zone, elimination, placement, spawning, or the match lifecycle changes, and
no fixture horizon moved.

**Amended 2026-09-08:** The mode-state block publishes `elimination_grace_ticks`, and royale
declares a fourth `kLifecycle` system, `elimination_grace_publisher`, whose only effect is to write
it. The block therefore now carries a value royale was *configured* with beside two it *observed*,
which is a real widening of § "Where zone and elimination state live" and is argued there rather
than assumed. The reason is the 2026-09-07 playtest: every snapshot already carries each entity's
`ZoneExposure` counter, the client had no way to learn the bound it is tested against, and a
counter without its bound cannot answer "how long do I have", so elimination read as arbitrary. No
royale rule reads the published value back — `zone_elimination` holds its own copy of the validated
configuration — and the two cannot drift because `RoyaleMode::systems()` builds both from one
`RoyaleConfiguration`. Publishing it is protocol minor `2.2` (`docs/protocol/v2.md` § "Versioning
and fail-closed decoding"); nothing about the elimination rule, the zone, or the placement rules
changes, and the decision and its `Accepted` status are unchanged.

**Amended 2026-09-09:** `lethal_hazard` is lethal only while the committed phase is `running`: its
first predicate reads the phase beside the `LethalOnContact` marker, so outside `running` the pair
falls through to `variable_impulse` and a comet shoves rather than kills. The row had landed without
that gate, and because a hazard outlives the phase it was spawned in by its whole `Lifetime` while
`placement_recorder` ranks every `EliminationEvent` in every phase, a comet in flight at
`running -> ended` could destroy the committed winner during the restart delay and append it to the
ranking at placement 1 (`docs/reviews/2026-09-08-lobby-and-hazard-review.md`, finding 2).
§ "Elimination and placement" is true again as written: eliminations occur only during `running`,
step 3 of the recorder never coincides with step 2, and the winner receives no placement entry. The
gate is the row's rather than the recorder's so that an event nobody consumes stays a visible
producer bug and a boulder does not vanish when a match ends. No number, rule, or fixture horizon
changes, and the decision and its `Accepted` status are unchanged.
