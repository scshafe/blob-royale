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
`ComponentList<PhysicsBody, Controllable, Lifetime, Score, Team, Zone, ZoneExposure,
LethalOnContact, RespawnTimer, Hill, HillPresence, RaceProgress, HillMotion>`, where `Zone` and `ZoneExposure`
are royale's, `Hill` and `HillPresence` describe hill scoring, and `RaceProgress` counts ordered
gates; `HillMotion` carries roaming velocity and private scheduling, while `LethalOnContact` and
`RespawnTimer` support shared mechanics. Because it is a type list,
four behaviors are **generated rather than maintained** — structural world equality,
`destroy_entity` erasing from every store, body-bound lifetime cleanup, and snapshot
publication of every kind — so a new kind cannot forget to participate in any of them.

`component_lifetime.hpp` owns the default-false `ComponentLifetime<C>::bound_to_body` trait.
`HillPresence` and `ZoneExposure` declare it beside their values. The one registry-generated,
allocation-free `GameWorld::erase_body_bound_components_without_body()` sweep removes those
kinds from every bodyless entity, including non-participants and already-bodyless entities.
Shared respawn calls it after body removal, before commit; arbitrary intermediate store edits do
not promise that invariant. Score, race progress, controller identity, hill geometry/motion, and
respawn timers remain body-independent. Whole-entity destruction already erases every kind.

The world's seat count is `kMaximumEntityCount`, and it says entities because it bounds entities: a
wall, a projectile, a pickup, and a zone each take a seat and none of them is a player.
`kMaximumPlayerCount` remains beside it as the protocol v1 snapshot *player* limit, which
`src/protocol/protocol_json_encoding.cpp` pins to `kSnapshotPlayerLimit` with a `static_assert` and
which nothing but a publication reads. Protocol v3 bounds a snapshot's *entities* at 1,024, which is
below both; `src/application/match_startup_validation.hpp` is what refuses a configuration whose
worst-case published population could cross it, because the kernel's seat count alone would not.

A **player** is not a type: it is an entity carrying both a `PhysicsBody` and a `Controllable`.
`PhysicsBody` is the one body value in the game and carries position, velocity, stored acceleration,
radius, mass, restitution, the collision layer and mask pair, `is_static`, and a `BoundsBehavior`;
the accepted physics phases still read the one common radius from `SimulationConfig`, so radius is
carried but not consulted. Mass and restitution *are* consulted, by the `variable_impulse` row and
nothing else: both default to the accepted baseline — unit mass, perfectly elastic — and
`body_has_baseline_physics` is the predicate that keeps a pair of ordinary blobs on the accepted
equal-unit-mass equation. `BoundsBehavior` defaults to `kFold`, which is the accepted wall policy;
`kCross` is what a body that travels through the arena rather than bouncing inside it declares, and
it changes exactly three places — phase 4 resolves its motion unfolded, the commit-time bounds check
does not hold it to the disc-centre interval, and the broad phase clamps its coverage to the edge
cells rather than rejecting it. `ControllerId` is the durable identity of the deciding agent and
outlives the entities it drives; `Controllable` is the only place the two identity spaces meet.

## The command vocabulary

`command_registry.hpp` is the closed, ordered list of command kinds: the variant
`Command = SpawnCommand | DespawnCommand | ThrustCommand | SetSeatCountCommand | ClearSeatCommand |
SeatNpcCommand | StartMatchCommand`, the `CommandKind` bit enumerators, each kind's wire name, and
each kind's position in phase 0's application order. A command is a value struct in its own header
under `commands/`.

Step 10 appends `SetMovementTuningCommand` to that closed vocabulary without changing existing
bits. Its explicit application rank follows thrust and precedes the existing lobby command order.
It addresses a controller and carries request ID, expected revision, and one validated
`MovementTuning` pair. `InputBatch` validates safe integer ranges and preserves existing
submission-based deduplication; correlation IDs never select command order.

`MatchState::movement` owns current/default tuning, revision, and effective tick. The existing
phase-0 handler freezes entry revision R, checks seated membership at each command's canonical
position in every phase, and chooses the last eligible contender against R. A winner updates once
to R+1/effective N, including an equal-value request. Other outcomes are superseded, stale revision,
not seated, or revision exhausted; no winner changes no movement state. Later Join cannot grant
retroactive authority, and later Leave cannot undo an admitted choice. `GameSimulation::step`
reserves bounded decision storage before commit and releases `MovementTuningDecisions` only after
the existing nonthrowing world/grid/tick commit. It adds no callback, event bus, or policy socket.

`Controllable::normalized_thrust_intent` preserves normalized input between commands. Absence
retains authored acceleration until the first input; explicit zero is held coast. Shared gameplay
locomotion owns its interpretation, and `ComponentPublication<Controllable>` strips both that
private intent and recorded commands from every snapshot.

**A kind addresses whichever identity it carries.** `SpawnCommand` names only a `ControllerId` — the
engine draws the new `EntityId` from the tick's reservation and the mode seats it — and the four
lobby kinds name only the `ControllerId` the boundary stamped them with, because a lobby command acts
on the match rather than on a body. Every kind that names an entity addresses that `EntityId`.
Addressing the sender is what makes "the first of two clients to seat one seat wins" a stated rule:
both commands survive de-duplication and apply in ascending sender order, and seating never
overwrites.

**The four lobby kinds are the only commands the engine itself interprets besides spawn and
despawn**, and for the same reason: they write `MatchState`, which is engine-owned state gating an
engine-owned transition. A mode's meaning is still a mode's system, which is why `thrust` is recorded
rather than applied.

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

`MapDefinition` is the static, mode-independent content of one arena: a name, immutable terrain,
static bodies, markers, and bounded `MapMetadata`. **Markers are the one point-prop authoring concept**, and
`spawn_points()` is the ordered projection of the markers of kind `spawn`, materialized once at
construction because every mode needs it.

`terrain_definition.hpp` owns the rectangular envelope, either solid ground or named polyline
corridors, and circular holes. `bounds()` delegates to that envelope; it does not store another
rectangle. The existing programmatic bounds factory explicitly constructs solid ground, while map
files require terrain declarations. `terrain_queries.hpp` is the canonical point support, swept
interval/first-exit, nearest-supported-point, and radius-clearance capability. Clearance measures
the exposed Boolean boundary, so overlapping road seams are not cliffs. Its analytic line/arc
cache shares one immutable allocation with the authored geometry and is compiled only at creation.
Analytic feature selection precedes at most four directed coordinate-rounding steps to obtain a
supported witness; clearance rounds toward the supported center and cannot increase its computed
distance. Recovery at an intersection uses the Boolean supported angular sector, not one curve's
normal. Compilation retains authored side orientation and arc endpoint directions, and classifies
open sectors before constructing the selected supported recovery ray. Known endpoint incidence
is retained only with a construction certificate, never a proximity merge. A missing strict
angular cone does not erase supported curved cusps, rims, or isolated points.
The bounded selected-ray correction is neither an exhaustive nearby-point search nor an
exact-real interval certificate (ADR 0008).
Shape, temporary-work, and cache limits live in `simulation_limits.hpp`; exhaustion or lost
precision is a named failure, never partial terrain. `swept_geometry.hpp` and
`motion_event_order.hpp` own every root and exact event order. Live swept-kernel adoption still
requires the ADR 0008 physics gate. Race, controllers, and the session client already read the
shared terrain. One centreline projection core preserves authored ties and written arithmetic for
the racer's point/distance query and the no-throw distance-only query; the latter never acquires
point-materialization validation on standalone signed/extreme corridors.

**The map is the arena source.** Phase 4's fold, the commit-time bounds validation, and `SpatialGrid`
all read `MapDefinition::bounds()`. `SimulationConfig` keeps `world_width` and `world_height`
because protocol v1's `/api/v1/config` publishes them through `PublicConfiguration` and
`ScenarioLoader` validates seeded centres against them; no kernel phase reads them any more. The
`[world]` INI keys therefore stay in every configuration beside `[match] map=`, and
`match_startup_validation.hpp` is what keeps the two agreeing: it rejects a map whose arena is not
the `[world]` rectangle protocol v1 publishes, so the duplication cannot silently diverge. The
overloads that take no map synthesize `MapDefinition::bare_arena` from those same scalars, which is
why no accepted fixture had to change to gain a map.

A **static body** takes part in the broad phase and in contact resolution and is never integrated,
accelerated, or dragged: phases 1, 4, and 5 skip it. Its centre obeys the closed arena rectangle
rather than the disc-centre interval a dynamic body is folded into, because a wall legitimately sits
on or past the arena edge -- and getting that distinction wrong is what would make the obvious
boundary obstacle unrepresentable. `MapDefinition::static_bodies()` is declared content, and
`GameWorld::create(configuration, map, seed)` seats it: the world owns the id policy for map
content and numbers a map's bodies `kMinimumEntityId + index` in declared order, so a map's
entities are a deterministic function of the map file alone. That factory also rejects a map whose
spawn points cannot seat a disc of the configured radius inside the envelope and over supported
terrain, which is the one place a spawn point meets a radius. A static body's center must also be
supported at map construction; this adds no new dynamic-body or live contact policy.

## Ownership and invariants

`GameSimulation` owns one `GameWorld`, one `MapDefinition`, one `SpatialGrid`, one `SystemPipeline`
with the engine's `MatchLifecycleSystem` appended last at `kLifecycle`, one `ContactRuleTable`, one
`SpawnSystem` holding the mode's `SpawnPolicy`, and the mode's declared name and accepted command
kinds. **Every declaration is read exactly once, at construction, and the mode object is then
destroyed**, so "nothing calls into the mode during a tick" is structural rather than a rule to
remember; the corresponding obligation on a mode author is that every declaration it returns is
independently owned.

`validate_map(map)` is called before `systems()`. The race uses that setup ordering to bind its
validated course once from the map and configuration, then passes independent immutable copies to
its declared systems. Its registry factory still takes only `GameModeConfiguration`; no mode
lookup, course construction, or mode callback occurs during a tick.

`MatchState::previous_phase` is the phase observed before the last lifecycle transition. The engine
writes it at the start of `MatchLifecycleSystem::apply`, before the switch, on every tick. Because
that system is last at `kLifecycle`, earlier systems on tick `N + 1` read `phase` as tick `N`'s
committed phase and `previous_phase` as tick `N - 1`'s. The pair identifies the one post-transition
tick without a separate observer in every mode. Royale retains a published mirror in its own block
for wire compatibility; gameplay reads the engine field. The objective now receives `TickContext`
with the world, so a time limit uses committing ticks without publishing a second current tick.

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

`simulation_limits.hpp` owns the bounds shared by input and publication: physical component
magnitudes at most `10^12`, protocol-safe integers at most `2^53 - 1`. The hill and race
configuration factories enforce those bounds for their published geometry and winning score, so
startup cannot accept values that only fail when a snapshot is encoded. Body-bound partial
progress is cleared by shared respawn's registry sweep after scoring and body removal on the
knockout tick, so zero-delay seating cannot hide the lost body from counter cleanup. Scoring
systems retain their normal counter rollover and leaving-region rules, not body-loss cleanup.

`WorldSnapshot` and `PlayerSnapshot` are immutable, copy-owned publication values. A snapshot
carries the ascending entity roster, every registered component kind through `components<C>()`, the
protocol v1 `players()` projection of the entities carrying both a `PhysicsBody` and a
`Controllable`, the `MatchSnapshot` section, and the named generators' `random_draw_counts()`.
Its roster is derived from the stores it just copied, so a published component whose entity is missing from
`entities()` is unrepresentable rather than merely avoided. **A published component is what its kind
declares it publishes** (`component_publication.hpp`): `Controllable::commands_this_tick` is
tick-local live input and is stripped here, so a snapshot never discloses a player's input for the
tick it is rendering. Snapshot creation happens only after a complete tick and retains canonical
entity ordering. Older snapshots never change when the simulation advances.

`ComponentPublication<HillMotion>` strips its optional next-retarget tick and stream identity,
leaving only current velocity. The component belongs to the non-physical hill entity only under
`random_roam`; marker tours retain their original component set. Generator state stays exclusively
in `RandomStreams`, so motion and draw state share the normal whole-world commit/rollback.

`RandomStreams` owns a fixed array of committed generators, addressed through the closed ordered
registry in `random_stream_registry.hpp`. Stable ordinals are `hazards = 0` and `hill = 1`.
Hazards use the old match seed unchanged; hill uses the canonical SplitMix64 finalizer of
`match_seed + golden_gamma * 1`, with unsigned wrap and no initialization draws. Extra draws on
one stream never advance the other. Invalid runtime identities are named failures, not fallbacks.
The existing working-world copy and whole-world commit cover every stream; there is no separate
rollback mechanism. Snapshots own only the count array. C++ counts remain uint64, while the v3
encoder rejects a count outside the wire's safe-integer range rather than silently rounding it.

## Extension points

`@extension-point entity_component` — `component_registry.hpp`. Adding an entity kind's vocabulary is
a new value-struct header under `components/` declaring its own `ComponentKindName`, plus one type in
the registry list. `GameWorld`, `GameSimulation`, and existing systems are untouched. Two
implementations beyond the engine set are registered: `Zone` and `ZoneExposure` for royale, added in
plan Step 21 as two headers and one edited line, which is the first measured use of this seam. The
third, `LethalOnContact`, is the one that measures it hardest, because it is declared by
`src/gameplay/shared/` rather than by any mode and still cost the same one header plus one line; it
is also the first kind whose presence is its entire value, so it publishes an empty wire object.
`Flag` for capture the flag is the next.

`@extension-point game_mode` (mode state) — `mode_match_state_registry.hpp`. A mode's match-wide
state that is **not** entity-shaped is one arm of the `ModeMatchState` variant plus one
`ModeMatchStateSchemaId` specialization. Mode state should be a component wherever it can be, so this
carries only what has no entity: `NoModeState` for every mode whose state is entity-shaped, and
`RoyalePlacementsModeState` (schema id `royale_placements`) for royale's ordered placement list and
the published mirror of the engine's previous phase; `KingOfTheHillModeState` for three declared
constants; and `RaceModeState` for required selected-road identity, gates, durations, and recorded
finish standings. Road geometry remains only in terrain. The race arm cannot default-construct an
empty road; race-owned initialization supplies its bound course, while a default match still holds
`NoModeState`. Hill scores and race progress remain entity components.

`@extension-point bounded_name` — `bounded_name.hpp`. Distinct policies own grammar, capacity,
domain diagnostics, and whether an empty default sentinel is permitted; one fixed-storage value
owns copying, zero tails, equality, and lifetime-safe views. Seat/contact names preserve their
empty sentinels. Required race-road names do not have one. Name syntax does not prove membership:
consuming boundaries resolve the selected road against their actual terrain.

`@extension-point command_kind` — `command_registry.hpp`. Adding a command kind edits **two**
existing files in this domain:

```
new  src/simulation/commands/<kind>_command.hpp  the value struct and its fields
edit src/simulation/command_registry.hpp         one type in the Command variant, one enumerator,
                                                 one CommandKindName, one CommandKindOf, one
                                                 application rank, one addressed_identity_of arm
edit src/simulation/input_batch.cpp              the kind's value validation, if it has any
new  src/gameplay/...                            the consuming system, unless the meaning is the
                                                 engine's own, in which case one arm of phase 0
edit src/runtime/command_mailbox.hpp             one arm of is_entity_lifecycle_command, saying
                                                 whether losing it changes whether an entity exists
edit src/protocol/command_wire_kind.hpp          one specialization saying whether a client may
                                                 send it, and under what wire name
```

Three of those five are enforced by a `static_assert` or a `-Werror=switch` rather than by this
list: the mailbox pins `kCommandKindCount`, `CommandWireKind`'s primary template is declared and
never defined, and `addressed_identity_of`'s fallback arm reads `value.entity`, which a command
carrying no entity does not have. A kind that skips one of them fails to compile.

A **client-sendable** kind costs three more edits outside this domain, and a server-issued one
costs none of them, because `CommandWireKind` answering `std::nullopt` is what makes it unreachable
from the decoder:

```
edit src/protocol/protocol_v3_constants.hpp      its wire name in kV3ClientCommandKindNames
edit src/protocol/command_decoding.cpp           its payload decoder and one arm of decode_payload
new  docs/protocol/schema/v3/...                 its wire schema, a protocol minor version
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
wall, and a `flag_pickup` pass-through row that changes no body and emits one event. The built-in
table declares three rows — `variable_impulse`, then `elastic_disc`, then `reflect_static` — and
that order is the whole reason per-body mass and restitution are an addition rather than a versioned
physics change: `variable_impulse` matches only a dynamic pair in which a body differs from the
baseline, so an ordinary pair falls through to `elastic_disc` and the accepted arithmetic.

`@extension-point game_mode` — `game_mode.hpp`, with the mode-state seam in
`mode_match_state_registry.hpp`. A `GameMode` is the complete declared ruleset of one playable game
and the one accepted inheritance hierarchy here. Its seven declarations — `name`, `systems`,
`contact_rules`, `accepted_command_kinds`, `spawn_policy`, `objective`, `validate_map` — are read
once at construction through `GameSimulationSetup::of_mode(map, mode)`, and the two sub-interfaces a
mode declares are `SpawnPolicy` (which index into `map.spawn_points()`, or defer) and
`MatchObjective` (`can_start`, `outcome`, `durations`). Everything else about seating and the match
machine is engine mechanism: `SpawnSystem` owns iteration, the policy call, and the rotation
counter, and seats through the occupancy predicate and at-rest write of `spawn_seating.hpp`, which a
mode system that returns a player to a point of its own uses too. `point_is_occupied` scans the live
body store with the existing `2 * player_radius` contact range and position tolerance;
`seat_body_at_rest` writes zero velocity and acceleration with the configured radius and ordinary
blob defaults. The race checkpoint return uses both operations, reading live stores so earlier
returns in the same stage are visible. Timer expiry leaves an entity awaiting a body; retrying a
blocked point does not require another timer. `MatchLifecycleSystem` runs last
at `kLifecycle` and commits at most one phase transition per tick. A mode's own match-wide state that is genuinely not entity-shaped is one arm of
`ModeMatchState` plus one registration line — mode state should be a component wherever it can be.
`validate_map` throws its own library's typed, coded validation error: `SimulationValidationError`
inside this domain, `GameplayValidationError` for a mode in `blob_gameplay`.

Five implementations: the engine's own `idle` declarations (`idle_spawn_policy.hpp`,
`idle_match_objective.hpp`), which never seat and never start a match; `sandbox` in
`src/gameplay/sandbox/`, which declares one `kPreKernel` system and nothing else; and `royale` in
`src/gameplay/royale/`, the first to declare a system at every stage and the first to contribute a
component kind and a mode-state arm; `king_of_the_hill`, which reuses respawn and scores presence;
and `race`, which returns racers to checkpoints and records finishes. The last two extend the
component and mode-state registries without changing the numbered kernel phases.

`@extension-point map_definition` — `map_definition.hpp`. A map is a data directory and one line of
match configuration: `map.cfg` for name, terrain (including bounds), and metadata,
`static_bodies.csv` for obstacles,
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

### Immutable terrain publication (2026-09-10)

`GameSimulation` retains a shared immutable map. `WorldSnapshot::terrain()` aliases that map's
actual terrain member, preserving its lifetime without copying geometry every tick. Snapshot
value equality compares authored terrain rather than allocation identity. Controllers see the
same retained value through `Observation`; session v3 sends it once in welcome, not every frame.
