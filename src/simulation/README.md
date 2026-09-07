<!-- canonical: simulation_domain -- deterministic mutation and value ownership -->

# Simulation domain

`blob_simulation` is the deterministic, single-threaded game-domain core. It has no network, JSON,
logging, Boost, mutex, condition-variable, or thread dependency. This keeps the result reproducible,
cheap to test, and impossible to couple accidentally to transport timing.

## The component model

An **entity** is an `EntityId` and nothing else. Everything an entity *is* on a given tick is the set
of components keyed by its id. A **component** is a typed value struct with no behavior beyond
construction and comparison; each kind lives in its own header and declares its own wire name through
a `ComponentKindName` specialization. There is no entity base class and no entity hierarchy
(`docs/architecture/0004-gameplay-architecture.md` § "Entities, components, and stores").

`ComponentStore<C>` is the only component storage implementation. Its `entries()` are strict
ascending `EntityId` order by construction, so **every loop that walks a store is ascending-`EntityId`
for free**, `find` is a binary search, and a reader needing two kinds performs an ordered merge of two
ascending spans rather than a hash lookup. That merge has one implementation and one name:
`for_each_entity_with_both` in `component_join.hpp`, with `count_entities_with_both` as its counting
form. It is what `WorldSnapshot::players()` and every mode's steering system walk, and it is the
answer to "what walks two stores together?".

**Only the value half of a store is mutable.** `mutable_values()` hands out `C&` and no `EntityId` at
all, and `mutable_find` hands out one `C*`, so a caller cannot rewrite the key that the ascending
order and every binary search depend on; `insert_or_assign` and `erase` remain the only operations
that change which ids a store holds. The tick has only ever needed the values.

`component_registry.hpp` is the closed, ordered list of kinds:
`ComponentList<PhysicsBody, Controllable, Lifetime, Score, Team>`. Because it is a type list, three
behaviors are **generated rather than maintained** — structural world equality, `destroy_entity`
erasing from every store, and snapshot publication of every kind — so a new kind cannot forget to
participate in any of them.

The world's seat count is `kMaximumEntityCount`, and it says entities because it bounds entities: a
wall, a projectile, a pickup, and a zone each take a seat and none of them is a player.
`kMaximumPlayerCount` remains beside it as the protocol v1 snapshot *player* limit, which
`src/protocol/protocol_constants.hpp` pins with a `static_assert` and which nothing but a publication
reads.

A **player** is not a type: it is an entity carrying both a `PhysicsBody` and a `Controllable`.
`PhysicsBody` is the one body value in the game and carries position, velocity, stored acceleration,
radius, mass, the collision layer and mask pair, and `is_static`; the accepted physics phases still
read the one common radius from `SimulationConfig`, so radius, mass, the masks, and `is_static` are
carried but not yet consulted. `ControllerId` is the durable identity of the deciding agent and
outlives the entities it drives; `Controllable` is the only place the two identity spaces meet.

## The command vocabulary

`command_registry.hpp` is the closed, ordered list of command kinds: the variant
`Command = SpawnCommand | DespawnCommand | ThrustCommand`, the `CommandKind` bit enumerators, each
kind's wire name, and each kind's position in phase 0's application order. A command is a value
struct in its own header under `commands/`. `SpawnCommand` names only a `ControllerId` — the engine
draws the new `EntityId` from the tick's reservation and the mode seats it — so a spawn addresses its
controller while every other kind addresses the `EntityId` it names.

`CommandKindMask` is the set of kinds a mode accepts, one integer with `create`, `none`, `all`,
`contains`, and an immutable `with`.

`InputBatch` is the one validated command value a tick may read, and `InputBatch::empty()` is the
no-input tick. `InputBatch::create` canonicalizes to phase 0's application order — despawns, then
spawns, then the remaining kinds, each group ascending by the identity it addresses — keeps the last
submitted command of a kind for an identity, and rejects an unaccepted kind, a thrust direction
component outside `[-1, 1]`, a despawn naming an id inside the batch's own reservation, and a
submitted count above the accepted limit. A thrust direction is carried verbatim: the magnitude clamp
belongs to the mode's steering system, whose written operation order is the contract
(`docs/architecture/0005-royale-mode.md` § "Steering").

`EntityIdReservation` is the contiguous half-open block of ids one tick may bring into existence.
`draw_next` advances it and throws on exhaustion; it never wraps and never reissues a drawn id,
because a reused id would graft one entity's components onto another.

Phase 0 fills `Controllable::commands_this_tick` **in the batch's order and no other**: one canonical
order governs one command list, and the recorded list is phase 0's application order because that is
the order the batch already arrives in. Phase 0 does not interpret what it records. **Command meaning
is a system's job**, so a thrust becomes stored acceleration only when a mode's `kPreKernel` steering
system reads it — `thrust_steering` in `src/gameplay/shared/`.

## The event vocabulary

`world_event_registry.hpp` is the closed, ordered list of in-tick event kinds: the variant
`WorldEvent = ContactEvent | DespawnEvent | EliminationEvent`, the `WorldEventKind` enumerators, and
each kind's diagnostic name. An event is a value struct in its own header under `events/`.

**Every registered kind has a producer, and the list is kept that way rather than kept full.** An
event is a channel between two stages of one tick, so a kind nothing emits is a switch arm, a name,
and a header no reader can reach. `SpawnEvent` and `ScoreEvent` were registered ahead of any producer
and were removed in plan Step 21 (engine review finding 17). Today the contact phase emits
`ContactEvent`, royale's `zone_elimination` emits `EliminationEvent`, and royale's
`placement_recorder` emits `DespawnEvent`, which the commit applies. Re-adding a kind is its header
plus four lines in the registry, so removing an unused one costs nothing to reverse.

Systems within one tick communicate through the bounded, ordered list `GameWorld` owns.
`GameWorld::emit` appends in production order and `GameWorld::events()` publishes it; the list is
cleared at every commit, so **events are tick-local and never appear in a snapshot**. A consequence
that must outlive the tick is written into a component instead. The list is bounded by
`kMaximumWorldEventCount` and overflow is a hard failure with
`SIMULATION.GAME_WORLD_EVENT_LIMIT_EXCEEDED`, never a silent drop, because a dropped event would
convert a failure into a differently wrong tick.

## The tick: one fixed kernel, three named stages

`GameSimulation::step(FixedDelta, const InputBatch&)` is the only mutable world operation, and a
tick with no commands is that same call with `InputBatch::empty()`. The numbered phases are kernel
mechanism that no mode may reorder, skip, replace, or add; the stages hold the mode's declared
systems:

```
  phase 0            despawns; spawns draw an id and get a Controllable; the engine SpawnSystem
                     seats them through the mode's SpawnPolicy; remaining commands recorded
  ---- kPreKernel -- the mode's systems, declared order
  phase 1            stored acceleration, then drag
  phase 2            canonical candidate pairs
  phase 3            admission, narrow phase, then the mode's ContactRuleTable
  phases 4-6         world bounds, integration, spatial reindex
  ---- kPostKernel - the mode's systems, declared order
  ---- kLifecycle -- the mode's systems, declared order, then the engine MatchLifecycleSystem
  phase 10           apply DespawnEvent removals, validate the survivors, reindex, clear the
                     tick-local state, publish
```

**Removal precedes validation at the commit**, which deviates from the written order of ADR 0003
§ "Canonical tick" phase 10 and is deliberate: validating first makes an entity that a system both
pushed out of bounds and marked for despawn stop the match, and royale's elimination pairs exactly
those two. The question the commit asks is whether the world it is about to *publish* is legal.
Everything the original order guaranteed still holds — removal precedes the rebuild, the rebuild
precedes publication, no committed grid holds a non-live `EntityId`, and no snapshot observes a
half-applied removal. ADR 0003 owes this reordering an amendment.

A `SimulationSystem` is one interface with `name()` and `apply(GameWorld&, const TickContext&)
const`. **`apply` is `const` on purpose:** a system holds immutable configuration and nothing else,
so a tick's result stays a function of the committed world and the tick's `InputBatch` alone.
`TickContext` carries the sequence this tick commits, the fixed delta, the configuration, the map,
and a read-only `spatial_index()` -- and no clock and no `InputBatch`, so no system can read a wall
time or observe a half-applied intake. `spatial_index()` is a promise about *which world* the index
describes: during `kPreKernel` it is the index of the bodies phase 0 left at start-of-tick
positions, and from `kPostKernel` onward it is this tick's phase 6 rebuild. It never reflects the
reading stage's own writes, so a system that must see those reads the component stores instead.

The kernel has exactly **two policy sockets**, both evaluated at a fixed point against declared
data: the mode's `SpawnPolicy` in phase 0 and its `ContactRuleTable` in phase 3. There is
no third. Phase 3 applies three gates in a fixed order -- the pure integer collision-admission
predicate over the two bodies' layers and masks, the narrow phase that rejects non-contacts and
separating contacts, then the table walked in declared row order with the canonical orientation
tried before the swapped one. The first matching (row, orientation) wins and a pair matching no row
is unchanged, which makes the table total without a default row. A response may write only the two
bodies; every other consequence leaves as a `WorldEvent`.

The **spatial index is a function of the body store, not of the entity roster**, so every rebuild
decision compares the ids and positions the index was built from. A stage that creates a body,
destroys an entity through `destroy_entity`, or moves one is therefore observed, and a debug build
additionally asserts at commit that the committed index equals a fresh rebuild.

`SystemPipeline` stable-partitions a mode's declared list by `SystemStage` and preserves the
declared order inside each stage, so precedence is a property of the mode's written list and never
of insertion, allocation, or static-initialization order. It rejects a null system, an empty name,
and a duplicate name.

Drag is kernel mechanism, not mode configuration: `SimulationConfig::drag_per_second` is validated
finite and non-negative and phase 1 scales the accelerated velocity by
`max(0, 1 - drag_per_second * dt)`. At the accepted `drag_per_second = 0` the factor is exactly
`1.0`, so **an empty pipeline, zero drag, and an empty batch reproduce every accepted horizon
bit-for-bit** -- asserted against a second, in-test implementation of the accepted seven-phase tick
in `tests/unit/simulation/game_simulation_tests.cpp`.

## Maps and the arena

`MapDefinition` is the static, mode-independent content of one arena: a name, `ArenaBounds`, static
bodies, markers, and bounded `MapMetadata`. **Markers are the one authoring concept**, and
`spawn_points()` is the ordered projection of the markers of kind `spawn`, materialized once at
construction because every mode needs it.

**The map is the arena source.** Phase 4's fold, the commit-time bounds validation, and `SpatialGrid`
all read `MapDefinition::bounds()`. `SimulationConfig` keeps `world_width` and `world_height`
because protocol v1's `/api/v1/config` publishes them through `PublicConfiguration` and
`ScenarioLoader` validates seeded centres against them; no kernel phase reads them any more, and the
`[simulation]` INI keys retire into the map file when Step 25's loader arrives. The overloads that
take no map synthesize `MapDefinition::bare_arena` from those same scalars, which is why no accepted
fixture had to change to gain a map.

A **static body** takes part in the broad phase and in contact resolution and is never integrated,
accelerated, or dragged: phases 1, 4, and 5 skip it. Its centre obeys the closed arena rectangle
rather than the disc-centre interval a dynamic body is folded into, because a wall legitimately sits
on or past the arena edge -- and getting that distinction wrong is what would make the obvious
boundary obstacle unrepresentable. `MapDefinition::static_bodies()` is declared content, and
`GameWorld::create(configuration, map, seed)` seats it: the world owns the id policy for map
content and numbers a map's bodies `kMinimumEntityId + index` in declared order, so a map's
entities are a deterministic function of the map file alone. That factory also rejects a map whose
spawn points cannot seat a disc of the configured radius, which is the one place a spawn point
meets a radius.

## Ownership and invariants

`GameSimulation` owns one `GameWorld`, one `MapDefinition`, one `SpatialGrid`, one `SystemPipeline`
with the engine's `MatchLifecycleSystem` appended last at `kLifecycle`, one `ContactRuleTable`, one
`SpawnSystem` holding the mode's `SpawnPolicy`, and the mode's declared name and accepted command
kinds. **Every declaration is read exactly once, at construction, and the mode object is then
destroyed**, so "nothing calls into the mode during a tick" is structural rather than a rule to
remember; the corresponding obligation on a mode author is that every declaration it returns is
independently owned.

`GameWorld` owns one `ComponentStore` per registered component kind reached through `store<C>()`
and `mutable_store<C>()`, `MatchState`, `DeterministicRandom`, the tick's `WorldEvent` list, and the
tick's `EntityIdReservation`. **`entities()` is derived from the stores, not stored beside them**:
an entity exists exactly while some registered store holds its id, so "which entities exist" has
one answer and a store write cannot desynchronize a roster. `create_entity()` draws an id from the
tick's reservation and the entity comes into existence when its first component is written;
exhaustion is a hard failure. A world outside a tick holds the empty reservation, so nothing but a
tick can create.

Grid cells contain non-owning `EntityId` values and are rebuilt deterministically after a committed
tick. A `GameWorld&` exists only inside `step`, so nothing outside a tick can obtain one.

**There is one exception vocabulary.** Every rejection and every violated invariant in this domain is
a `SimulationValidationError` carrying one greppable `SIMULATION.*` code, including the engine
invariants that used to throw a bare `std::logic_error`: an unknown id reached through the spatial
index, an incoherent wall-motion list, and a committed index that is not a rebuild of the committed
world. A caller that has to tell an input rejection from a broken invariant reads the code rather
than the exception type. A mode in `blob_gameplay` raises `GameplayValidationError` with a
`GAMEPLAY.*` code; both derive from `std::invalid_argument`.

Constructors and named factories reject invalid values before they enter the world. A tick computes
against a working copy of the committed world, so if any phase or stage fails, no partial tick
becomes observable and the previous commit stands unchanged. The numbered phases read and write only
the `PhysicsBody` store and the `Controllable` command lists, so every other registered component
survives a tick unless a system writes it.

`WorldSnapshot` and `PlayerSnapshot` are immutable, copy-owned publication values. A snapshot
carries the ascending entity roster, every registered component kind through `components<C>()`, the
protocol v1 `players()` projection of the entities carrying both a `PhysicsBody` and a
`Controllable`, the `MatchSnapshot` section, and the generator's `random_draw_count()`. Its roster
is derived from the stores it just copied, so a published component whose entity is missing from
`entities()` is unrepresentable rather than merely avoided. **A published component is what its kind
declares it publishes** (`component_publication.hpp`): `Controllable::commands_this_tick` is
tick-local live input and is stripped here, so a snapshot never discloses a player's input for the
tick it is rendering. Snapshot creation happens only after a complete tick and retains canonical
entity ordering. Older snapshots never change when the simulation advances.

## Extension points

`@extension-point entity_component` — `component_registry.hpp`. Adding an entity kind's vocabulary is
a new value-struct header under `components/` declaring its own `ComponentKindName`, plus one type in
the registry list. `GameWorld`, `GameSimulation`, and existing systems are untouched. Two
implementations beyond the engine set are registered: `Zone` and `ZoneExposure` for royale, added in
plan Step 21 as two headers and one edited line, which is the first measured use of this seam. `Flag`
for capture the flag is the next.

`@extension-point game_mode` (mode state) — `mode_match_state_registry.hpp`. A mode's match-wide
state that is **not** entity-shaped is one arm of the `ModeMatchState` variant plus one
`ModeMatchStateSchemaId` specialization. Mode state should be a component wherever it can be, so this
carries only what has no entity: `NoModeState` for every mode whose state is entity-shaped, and
`RoyalePlacementsModeState` (schema id `royale_placements`) for royale's ordered placement list and
the phase it observed on the previous tick.

`@extension-point command_kind` — `command_registry.hpp`. Adding a command kind edits **two**
existing files in this domain:

```
new  src/simulation/commands/<kind>_command.hpp  the value struct and its fields
edit src/simulation/command_registry.hpp         one type in the Command variant, one enumerator,
                                                 one CommandKindName, one CommandKindOf, one
                                                 application rank, one addressed_identity_of arm
edit src/simulation/input_batch.cpp              the kind's value validation, if it has any
new  src/gameplay/...                            the consuming system
new  docs/protocol/schema/v2/...                 its wire schema, a protocol minor version
```

`kCommandKinds`, `CommandKindMask::all()`, and the rank-injectivity check are **derived** from the
variant through `CommandKindOf` (`kind_registry.hpp`), so none of them is an edit and none of them
can fall behind the variant: an alternative that copies a neighbour's enumerator fails to compile
on `values_are_distinct(kCommandKinds)` rather than silently dropping a kind from the complete mask.
`addressed_identity_of` is the one implementation of "which identity does this command address?",
shared by `InputBatch::create` and kernel phase 0.

The kernel records commands without interpreting them, and a mode that omits the kind from its
accepted set never sees it. Two implementations beyond `SpawnCommand` and `DespawnCommand`:
`ThrustCommand` for steering, and a later `UseAbilityCommand` for a dash or a weapon. **Command
meaning is a system's job**, so a new kind adds a consuming system rather than a new kernel
sub-step.

`@extension-point simulation_system` — `system_pipeline.hpp`. A mechanic is a new
`SimulationSystem` file plus one line in a mode's declared staged list; the kernel, the other
systems, and every other mode are untouched. Two implementations beyond the engine set:
`zone_shrink` for royale, `hill_scoring` for king of the hill. `SystemStage` decides what a system
may see, not when it happens to have been registered: `kPreKernel` sees start-of-tick positions and
this tick's recorded commands, `kPostKernel` sees committed positions and this tick's events, and
`kLifecycle` sees the tick's final world.

`@extension-point contact_rule` — `contact_rule.hpp`, ordered by `contact_rule_table.hpp`. An
interaction is a new row: two `Predicate` free-function pointers and one `Response` free-function
pointer, plus one line in a mode's `contact_rules()`. Function pointers rather than `std::function`
are what make purity structural -- a predicate or a response cannot capture state. `physics.hpp` and
phase 3 are untouched; the equations stay named pure functions and the table selects among them and
contains no physics. Row order is the declared precedence, and a mode that wants the defaults writes
them into its own order, so precedence between mode rows and built-in rows is visible in the mode's
source. Two implementations beyond `elastic_disc`: `reflect_static` for a dynamic body meeting a
wall, and a `flag_pickup` pass-through row that changes no body and emits one event.

`@extension-point game_mode` — `game_mode.hpp`, with the mode-state seam in
`mode_match_state_registry.hpp`. A `GameMode` is the complete declared ruleset of one playable game
and the one accepted inheritance hierarchy here. Its seven declarations — `name`, `systems`,
`contact_rules`, `accepted_command_kinds`, `spawn_policy`, `objective`, `validate_map` — are read
once at construction through `GameSimulationSetup::of_mode(map, mode)`, and the two sub-interfaces a
mode declares are `SpawnPolicy` (which index into `map.spawn_points()`, or defer) and
`MatchObjective` (`can_start`, `outcome`, `durations`). Everything else about seating and the match
machine is engine mechanism: `SpawnSystem` owns iteration, occupancy, the rotation counter, and the
seating write, and `MatchLifecycleSystem` runs last at `kLifecycle` and commits at most one phase
transition per tick. A mode's own match-wide state that is genuinely not entity-shaped is one arm of
`ModeMatchState` plus one registration line — mode state should be a component wherever it can be.
`validate_map` throws its own library's typed, coded validation error: `SimulationValidationError`
inside this domain, `GameplayValidationError` for a mode in `blob_gameplay`.

Two implementations: the engine's own `idle` declarations (`idle_spawn_policy.hpp`,
`idle_match_objective.hpp`), which never seat and never start a match, and `sandbox` in
`src/gameplay/sandbox/`; `royale` follows in `blob_gameplay` at plan Step 21.

`@extension-point map_definition` — `map_definition.hpp`. A map is a data directory and one line of
match configuration: `map.ini` for name, bounds, and metadata, `static_bodies.csv` for obstacles,
`markers.csv` for spawn points and mode props. No C++ file changes at all. Two implementations: the
960x640 arena, and an obstacle course whose walls are `static_bodies.csv` rows resolved by the
built-in `reflect_static` row. A mode reads the marker kinds it understands and ignores the rest,
which is what lets any mode play any map; a mode that *requires* a kind rejects the map at startup
in `validate_map` rather than discovering the absence mid-match.

Adding an event kind is a new value-struct header under `events/` plus one type, one enumerator, one
`WorldEventKindName`, and one `WorldEventKindOf` in `world_event_registry.hpp`, and a consuming
system. `kWorldEventKinds` is derived from the variant like `kCommandKinds`, so it is not an edit.
An event has no meaning until a stage reads it.

Pure equations in `physics.hpp` are the appropriate seam for a newly specified physical rule.
Per-entity durable state belongs in a component, never in a new field on `GameWorld`; avoid a generic
entity hierarchy or a class for each stateless equation. New tick behavior must be placed in the
explicit phase sequence documented by `docs/architecture/0003-deterministic-simulation-contract.md`
and proven deterministic before it is wired into `GameSimulation`.

Player commands are not a simulation shortcut. `InputBatch` is the deterministic domain input and
nothing more: identity, ownership, authorization, tick addressing, rate limiting, and cross-session
ordering are decided at the application and protocol boundary, before a batch exists. A command
source stamps the entity it owns, so no controller can command a foreign entity, and the simulation
never learns whether a command came from a human session or a bot.

## Verification

The canonical gate is `./scripts/verify-linux pr`. Focused tests are registered under the
`blob_simulation_unit_tests` CTest target. Linux performance measurements use
`./scripts/run-benchmarks-linux`; benchmark hashes are correctness assertions, not a second engine.
