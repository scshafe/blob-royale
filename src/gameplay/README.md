<!-- canonical: gameplay_domain -- the games, and where a new one goes -->

# Gameplay domain

`blob_gameplay` is where the **rules** live. `blob_simulation` owns values and mechanism — components,
stores, the fixed tick kernel, the match machine, the closed command and event vocabularies — and
this library owns the declarations that turn that mechanism into a playable game
(`docs/architecture/0002-simulation-architecture.md` § "Decision";
`docs/architecture/0004-gameplay-architecture.md` § "Libraries, and where a new thing goes").

It depends on **`blob_simulation` and nothing else**. No network, no JSON, no Boost, no logging, no
thread, no clock. A mode that wanted any of those has misplaced a rule that belongs above the
simulation boundary.

## What is here

```
src/gameplay/
  game_mode_registry.hpp/.cpp   the closed map from one mode name to its factory
  game_mode_configuration.hpp   mode sections, hazard table, and shared authored movement tuning
  gameplay_validation_error.hpp the one exception vocabulary of this library
  shared/                       mechanics and values more than one mode or section uses
    thrust_steering_system.*    held intent and current match tuning become stored acceleration
    input_lock.*               canonical active-stun admission predicate for self-propulsion
    status_system.*            absolute stun windows, input invalidation, and expiry at PostKernel
    locomotion.*               canonical normalization, scaling, and finite-step propulsion cap
    duration_ticks.*            the one conversion from an authored duration to tick counts
    hazard_archetype.*          one validated `[hazard.<kind>]` section, in the units a spawner reads
    hazard_spawn_system.*       schedules crossing births with pre-draw reservation/capacity checks
    create_crossing_hazard.*    canonical birth, lifetime, marker, and per-instance effect policy
    lethal_hazard_contact_rule.* admitted source effect terminates player motion and emits elimination
    lifetime_expiry_system.*    decrements `Lifetime` and despawns what runs out
    roster.hpp                  the two populations a rule reads: who is alive, who is playing
    lobby_start_rule.hpp        every seat filled and a start requested
    disc_geometry.hpp           whether a centre is outside a circle, written once
    spawn_point_probe.hpp       the forward probe from the rotation counter every policy shares
    next_free_spawn_point_policy.hpp  the next free point, in every phase but the wipe tick
    respawn_system.*            an elimination erases the body and starts a `RespawnTimer`
    match_reset_system.*        the restart wipe of every participant, on the lobby tick after ended
  king_of_the_hill/             a touring or randomly roaming capture zone (ADRs 0007/0008)
    king_of_the_hill_configuration.*  the validated `[king_of_the_hill]` section, in the units systems read
    king_of_the_hill_mode.*     the eight declarations (motion triggers currently default empty)
    hill_geometry.*             where the hill is, as a pure function of the map's `hill` markers and one integer
    hill_roaming.*              deterministic heading/speed/retarget sampling and outer-axis cancellation
    hill_movement_system.*      creates the hill entity once and writes its `Hill` each tick
    hill_scoring_system.*       presence toward the next point, and the point itself
    hill_objective.*            ending by points or by the clock, as three total predicates
    hill_rules_publisher_system.*  the three denominators on the wire
    king_of_the_hill_mode_state.hpp  the one answer to "what if the world holds another arm"
  race/                         an ordered course, checkpoint returns, and recorded finishes (ADR 0007)
    race_configuration.*        the validated `[race]` section
    race_mode.*                 the eight declarations (motion triggers currently default empty), binding the course before systems are built
    race_course.*               marker projection, validation, and point-to-centreline distance
    checkpoint_progress_system.*  advances at most one ordered gate per tick
    track_bounds_system.*       leaving the corridor emits an elimination
    standings_recorder_system.*  records same-tick finishers with a shared placement
    checkpoint_respawn_system.*  returns a bodyless racer to its last gate once it is clear
    grid_spawn_policy.hpp       the starting grid and returns before the first gate
    race_objective.*            finish window, then the no-finisher clock rule
    course_publisher_system.*   immutable course and durations on every frame
    race_mode_state.hpp         the one answer to "what if the world holds another arm"
  sandbox/                      free play: thrust, bump, and nothing ever ends
    sandbox_mode.*              eight declarations, shared support loss and respawn
    free_play_objective.hpp     always startable, never decided, zero durations
  royale/                       thrust and drag inside a shrinking zone, last blob standing
    royale_mode.*               the eight declarations (motion triggers currently default empty)
    royale_configuration.*      the validated `[royale]` section, in the units systems read
    zone_shrink_system.*        the zone's geometry, and the component it writes each tick
    zone_elimination_system.*   grace against the zone, and who is out
    placement_recorder_system.* ranking and roster removal
    elimination_grace_publisher_system.*  `G` on the wire, so a client can count down
    rotating_ring_spawn_policy.hpp  the next free point, and only between matches
    royale_objective.hpp        ending by attrition, as three total predicates
    royale_mode_state.hpp       the one answer to "what if the world holds another arm"
```

**`shared/` is where a thing more than one mode uses lives.** ADR 0004 files a mechanic under
`src/gameplay/<mode>/`, which is right for a mechanic one mode owns. `thrust_steering` is declared by
`sandbox` and, from plan Step 21, by `royale`, and filing it under either would make the other reach
into its neighbour. The rule is: one mode declares it, it lives in that mode's directory; two modes
declare it, it moves to `shared/`. Immutable mechanic configuration may be captured by systems;
live movement tuning is match-owned, so shared steering is scalar-free and reads it each tick.

`duration_ticks` is the rule applied to a value rather than a system. It was written inside
`royale/royale_configuration.cpp` with a note saying it would move here the day something else
configured a duration in seconds, and `hazard_archetype` is that second customer — a `shared/` file
may not include `royale/`, so the move was forced rather than optional. Two things changed with it:
the parameter is now the full configuration context (`royale.zone_shrink_seconds`,
`hazard.comet.spawn_interval_seconds`) instead of a bare key it prefixed with `royale.`, and its
three rejections carry owner-neutral `GAMEPLAY.DURATION_*` codes instead of `GAMEPLAY.ROYALE_*`
ones. The arithmetic is untouched, so every accepted tick count is the value it always was, and
`royale_configuration_tests.cpp` pins the contexts to prove it.

Five more things crossed the line on 2026-09-09, the day the second and third competitive modes
were designed (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "Shared rules
promoted, because they now have a second customer"), each a pure move with at most one predicate
added: `roster.hpp` (royale's "alive", plus "participant" -- every entity carrying a
`Controllable`, which is the field a mode whose fallen come back decides by);
`lobby_start_rule.hpp` (`RoyaleObjective::can_start`'s conjunction, now called by every lobby
mode); `disc_geometry.hpp` (`zone_elimination`'s centre-outside-a-circle predicate, byte for byte);
`spawn_point_probe.hpp` (the forward probe both spawn policies had written out); and
`next_free_spawn_point_policy.hpp` (sandbox's open-field policy, which now also defers on the one
`lobby` tick after `ended` so a restart wipe never destroys what the same tick seated -- a phase
sandbox never reaches). Those five historical promotions preserved their accepted royale fixtures
and discrete baseline; that evidence is not a universal equivalence claim for Step 16's live solver.

## Adding a game

```
new  src/gameplay/<mode>/<mode>_mode.{hpp,cpp}  the mode class, its systems, rules, policies
new  src/gameplay/<mode>/*_system.{hpp,cpp}     any mechanic only that mode declares
edit src/gameplay/game_mode_registry.hpp        one include and one row in kGameModeRegistrations
edit src/gameplay/CMakeLists.txt                the new .cpp files; the list is explicit, not a glob
edit match configuration                        `[match] mode=`
only if it is configured                        a `<mode>_configuration.{hpp,cpp}`, one member on
                                                GameModeConfiguration, and the `[<mode>]` fields in
                                                application/replay loaders and full application configs
only if it adds published vocabulary             component/mode-state registration, encoders,
                                                schemas, generated client types, and client rendering
do not touch                                    kernel phase order, blob_runtime, blob_server,
                                                or another mode's rules
```

`@extension-point game_mode` — `game_mode_registry.hpp`. The table is `constexpr`, so two rows
claiming one name fail to compile rather than resolving to whichever was written first. A factory
takes one argument, the validated `GameModeConfiguration` carrying every configured mode's own
`[<mode>]` section, and reads only its own member. **One factory signature rather than one per
configured mode** is what keeps this the only file that knows which games exist: a caller that had
to choose a factory shape per mode would be a second such file. A mode whose balance is entirely
defaulted ignores the argument, which is what `GameModeRegistry::create(mode_name)` — the
defaults-only overload a test or a diagnostic uses — is for.

Four implementations are registered: `sandbox`, `royale`, `king_of_the_hill`, and `race`.

`GameSimulation::create` reads all eight mode declarations once and destroys the mode. The new
`motion_triggers()` declaration returns an independently owned `MotionTriggerTable`; its default
is empty, which all current production modes inherit. Together with spawn policy and contact rules,
it is the third kernel policy socket, not a fourth stage or a mode-owned event loop. Step 16 supplies
injected support-capable trigger tests; production ground attachment, falling, and chronological
race triggers remain Step 17. Shield/parry composition and its production stun requests remain
Step 18. No current mode acquires those mechanics merely by inheriting the declaration.

Hill motion is selected through the existing `HillMovementSystem`, not a second scoring path.
`marker_tour` keeps its original arithmetic and zero hill-stream draws. `random_roam` commits
`HillMotion` velocity and a private schedule; its pure sampler reads only the world's named hill
generator. The center may cross interior cliffs/holes, but outward-axis displacement and velocity
are canceled at the closed outer map bounds. This permits edge sliding or rest, without bouncing,
forced steering, or creating ground. The ordinary retarget schedule continues at rest. Scoring and
rendering still read only `Hill`. See ADR 0008 § "Continuously roaming hill" for numeric and lifecycle
contracts, and `king_of_the_hill/README.md` for the implementation ownership.

The application loader requires every mode's section, regardless of the selected mode. Its full
configuration inventory includes standalone `.cfg` files, inline unit-test strings, and
`tests/integration/server_process_fixture.cpp::write_fixture_inputs`, which builds a temporary
configuration for each server workload. A new required section must reach that builder too.

## `sandbox`

Free play automatically enters countdown on tick one and running on tick two, never ends, accepts
spawn/despawn/leave/thrust, and seats joiners at supported free markers. It uses built-in contacts,
shared PreKernel steering and PostKernel status, an always-active
support-loss trigger, and shared lifecycle respawn with explicit `[sandbox] respawn_delay_seconds`.
The initial delay is two seconds. No Sandbox-only component, event, or mode-state block is added.

The historical pre-status `SandboxMode` measurement was **89 lines** — a 52-line class block plus 37 lines of definitions — of which **58
are code** once blank and `//` lines are removed. The measurement is the `class SandboxMode final`
block in the header and everything between the namespace braces in the `.cpp`, so anyone can rerun
it. That is what ADR 0004's claim that "a mode is a declaration, not machinery" is answerable to,
and every one of those lines is a declaration: the longest function body in the mode is
`validate_map`'s five-line rejection. Its two sub-declarations are 44- and 57-line files whose class
blocks are 14 and 18 lines, 11 and 17 of them code.

`validate_map` rejects a map with no `spawn` marker at startup, naming the map: free play with
nowhere to seat a joiner would silently defer every spawn command forever.

## Hazards

**A hazard kind is a configuration section and no C++ at all.** `hazard_archetype.hpp` is the
validated form of one `[hazard.<kind>]` section — radius, mass, restitution, speed, spawn interval,
lethality, and required `contact_effect_policy` — and the *kind name is the section's own instance name*, so nothing in `src/` names a
kind. The table hangs off `GameModeConfiguration` beside `royale`, because hazards are a
mode-agnostic mechanic: any mode may declare the systems that read the table, and a mode that
declares none never reads it, which is the same relationship `sandbox` already has with `[royale]`.

The open name reaches the value through the loader's **section family**
(`src/application/application_config_loader.cpp`): a declared section-name prefix whose instance
names are open, whose key schema is closed and shared by every instance, and whose instances collect
into a list. That is the only open name in the configuration schema. Everything else stayed
fail-closed — an unknown key inside an instance, a section matching no name and no prefix, a
repeated instance, and an instance missing a key are all the rejections they were.

There is **no `[hazards]` section and no `kinds=` key**. A list of kinds beside the sections that
declare them is two sources of truth for one list, and its failure mode is silent: a kind declared
and not listed simply never spawns. Dropping it leaves nothing family-wide but a spawn interval,
which is better per kind anyway — "a comet every six seconds and a boulder every twenty" is the
first thing a designer asks for and one cadence cannot say it. So the whole declaration of a kind is
one section, and a configuration that declares none has no hazards.

The archetype **names no entry edge**. A hazard needs a reproducible entry point *and* direction and
both come from the world's named `hazards` stream, so configuring the edge would make one component
of that geometry authored and the rest drawn. It would also need a closed edge vocabulary in C++,
and a designer wanting an edge the enumeration does not name would be back to writing code, which is
the bar the whole mechanic exists to clear.

That stream retains the original match seed and draw order: edge, entry fraction, exit fraction.
`HazardSpawnSystem` keeps phase/interval, declaration-order, reservation, and body-store admission
before every draw, then delegates the whole birth to `create_crossing_hazard`. The promoted
operation uses the unchanged crossing and lifetime arithmetic, seats a dynamic `kCross` body with
zero drag, attaches its lifetime and optional lethal marker, and assigns its sparse effect policy.
An optional typed instance override wins over the archetype default; explicit `closing_impact`
removes a nondefault rather than inheriting `any_touch`. Invalid overrides fail before draws/IDs.
The frozen old creation/RNG references remain independent of this operation.

Every authored hazard section requires exactly `contact_effect_policy=closing_impact` or
`contact_effect_policy=any_touch`; existing configurations explicitly retain closing impact.
Programmatic archetype sections retain the historical closing default. Simulation's
`ContactEffectAdmission` component stores only `any_touch`, belongs to the body, and is published
in v3; absence means closing impact. This is source eligibility for gameplay effects, not a switch
that gives a tangent or separating contact an impulse.

The live `lethal_hazard` row reads that source eligibility. During a running match, an admitted
hazard effect emits elimination and immediately terminates the victim's remaining motion, so it
cannot continue to later contacts/triggers in that solve. Existing elimination consumers still
remove or respawn bodies after successful solving. The row remains first-match; production guard
composition and its replacement are Step 18 work.

The live motion cap is 256 physical bodies, including static objects, independently of the larger
component-store and publication limits. The spawner's historical store check does not bypass that
cap: excess final survivors fail the whole tick transactionally. No hazards or physics bodies are
silently dropped to meet a motion budget. These correctness ceilings are not a certified operating
capacity. The separate `hill` stream cannot advance hazards; marker tours draw nothing while
`random_roam` uses its own stream. Both streams roll back with the working world on failure.

## `royale`

Thrust and drag inside a linearly shrinking circular safe zone, last blob standing
(`docs/architecture/0005-royale-mode.md`). It accepts `spawn`, `despawn`, and `thrust`; uses the
engine's built-in contact rows beneath `lethal_hazard`, which emits an eligible source effect and
terminates victim motion without selecting another impulse equation for ordinary blobs; declares
`thrust_steering` at `kPreKernel`, `zone_shrink` then
`zone_elimination`, then shared `status` at `kPostKernel`, and `placement_recorder`, `match_reset`, `lifetime_expiry`,
`hazard_spawn` then `elimination_grace_publisher` at `kLifecycle`; seats joiners on a rotating ring
and only between matches; and ends when one blob or none is alive.

A historical pre-Step-16 mode-shape measurement recorded `RoyaleMode` as **137 lines** — a 69-line
class block plus 68 lines of definitions — of which **89
are code**, against `SandboxMode`'s 89 and 58 measured the same way. Their class blocks are 35 and
30 lines of code in that measurement, when the interface had seven declarations. The
whole of royale's excess there is two extra `create` overloads for its hazard table; the rest of the
difference is in the definitions, which are `systems()`'s seven rows instead of one and
`validate_map`'s two rejections instead of one. That is the seam holding: a second, far richer game
— a shrinking zone, elimination with a published grace, and crossing hazards — cost the mode class
five lines of code and its definitions twenty-six, and everything else it needed went into new
files. These are historical measurements, not current source counts after the eighth declaration
and later shared systems.

What it contributed outside its own directory is two component headers plus one line in
`component_registry.hpp`, one mode-state header plus one type and one schema id in
`mode_match_state_registry.hpp`, and one row in `game_mode_registry.hpp`. It added no command kind,
no contact rule, no world event kind, and no kernel phase.

`validate_map` rejects one map at startup, naming the map and the cause: one whose arena's
circumscribed radius is not strictly greater than `zone_minimum_radius_world_units`, which would
start the zone already at its floor so it never contracts and the game never ends. A map with fewer
`spawn` markers than the lobby has seats is rejected too, but by the application's
`require_lobby_fits_map`, because a marker per seat is every lobby mode's rule and the seat count
is a `[match]` fact rather than royale's.

**Balance is `[royale]` and layout is the map.** `RoyaleConfiguration` is the validated section: it
converts the four durations to integer tick counts once, at load, so no system ever sees a value in
seconds and nothing multiplies by the tick rate at runtime. The proposed values give
`countdown_ticks = 2,000`, `zone_shrink_ticks = 36,000`, `elimination_grace_ticks = 1,200`, and
`restart_delay_ticks = 3,200`.

**Royale is the first mode that creates an entity.** `zone_shrink` draws one `EntityId` from the
tick's reservation on the first tick it observes no `Zone`, so a royale simulation cannot be stepped
with `InputBatch::empty()`: the no-input tick carries no reservation and a tick handed nothing may
create nothing. That is a hard failure with `GAMEPLAY.ROYALE_ZONE_ENTITY_UNRESERVED` rather than a
match quietly played with no zone and therefore no elimination.

## Respawn and reset

**What an elimination means is the consuming system's decision**, and the tree now has both
answers. Royale's `placement_recorder` ranks and destroys. `shared/respawn_system` is the other
answer, for a mode whose fallen come back: it erases the eliminated entity's `PhysicsBody`, attaches
a `RespawnTimer` of the mode's configured delay, counts existing timers down first, and erases a
timer at zero -- at which point the entity, still carrying its `Controllable` and body-independent
state, is exactly what the engine's `SpawnSystem` calls "awaiting a body". An entity eliminated on
tick `N` with delay `D` is offered to the mode's spawn policy at phase 0 of tick `N + D + 1`; with
`D = 0`, on `N + 1`. A policy defers an entity that carries a timer, which is one predicate
(`shared/next_free_spawn_point_policy.hpp`). At the end of the same lifecycle pass, the canonical
registry sweep erases every `ComponentLifetime<C>::bound_to_body` kind on every bodyless entity.
`HillPresence`, `ZoneExposure`, `Stun`, and `ContactEffectAdmission` declare that trait; their
consumers perform no separate body-loss cleanup. Score, checkpoint progress, controller identity, and the timer survive. Royale still
destroys whole entities, so its elimination path requires no additional sweep.

`shared/match_reset_system` is royale's restart wipe generalized: on the single `lobby` tick whose
`previous_phase` is `ended` it destroys every participant -- alive, respawning, or awaiting a seat
-- so a score or a gate count on an entity with no body cannot leak into the next match. Royale
declares it too, right after `placement_recorder`, whose own wipe it replaced; the replay fixtures'
pinned counts proved the two agree on every tick royale has recorded, which is the guard
`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "What is deliberately not built" set
for the adoption.

## `king_of_the_hill`

A hill of configured radius tours the map's `hill` markers -- dwelling, gliding, cycling, as a pure
function of elapsed running ticks -- and every tick a player's centre is inside it counts toward
the next point; a contested hill scores nobody unless `contested_hill_scores` says otherwise, and
the first to `points_to_win`, or the leader when the clock runs out, wins
(`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "King of the hill"). It accepts
royale's ten command kinds; uses the engine's built-in contact rows beneath `lethal_hazard`;
declares `thrust_steering` at `kPreKernel`, `hill_movement`, `hill_scoring`, then shared `status` at `kPostKernel`,
and `respawn`, `match_reset`, `lifetime_expiry`, `hazard_spawn` then `hill_rules_publisher` at
`kLifecycle`; seats joiners at the next free point in every phase, because the field is open; and
returns a knocked-out player after the configured respawn delay with its score intact.

After scoring, shared respawn removes eliminated bodies and sweeps body-bound `HillPresence`,
including entries already lacking a body. A completed point on the knockout tick remains earned.
The sweep happens on that tick before commit, so next tick's zero-delay seating cannot carry
partial progress through the return. Hill scoring owns only presence accrual, leaving-region
erasure, and point rollover; body-loss cleanup has one shared owner.

What it contributed outside its own directory is two component headers plus one line in
`component_registry.hpp`, one mode-state header plus one type and one schema id in
`mode_match_state_registry.hpp`, one row in `game_mode_registry.hpp`, one member on
`GameModeConfiguration`, and the `[king_of_the_hill]` fields in the loader. It added no command
kind, no contact rule, no world event kind, and no kernel phase, and it is the first mode built on
the framework amendments of ADR 0007: the objective's tick context for its clock, the engine's
`previous_phase` through the shared reset, and the shared respawn.

`validate_map` rejects a map with no `hill` marker or no `spawn` marker at startup, naming the map.
The configuration factory accepts hill radii in `(0, 10^12]` and `points_to_win` in
`[1, 2^53 - 1]`, using the simulation's publication bounds. A larger value fails at its authored
key before systems are built; zero points retains its specific validation error. Exact upper
bounds are legal and survive serialization and client schema validation.

Measured on 2026-09-09 with `wc -l` over `.hpp` and `.cpp` files directly in
`src/gameplay/king_of_the_hill/`: 15 files, 1,160 physical lines, including comments and blank
lines. The mode declaration alone is 194 lines across its header/source. These counts exclude
shared mechanics, simulation components, protocol, frontend, and tests; they measure the complete
mode-owned production implementation, not the total cross-domain cost.

## `race`

The course binds `[race] road` to a named terrain corridor and uses ordered `checkpoint` markers
as gates, with the last gate as the finish. Owned motion policies reuse canonical support-loss and
ordered-gate queries; they terminate falling/finished racers within the tick. Course publication
runs first PreKernel, before shared steering. PostKernel progress consumes certified facts, followed
by shared status. The lifecycle systems run in this
order: `standings_recorder`, `checkpoint_respawn`, `respawn`, `match_reset`, `lifetime_expiry`,
`hazard_spawn`; the engine evaluates the objective afterwards. The mode uses
shared steering and lethal-hazard contact, and accepts the same ten command kinds as royale.

`RaceMode::validate_map` builds and validates one `RaceCourse` before `systems()` reads it. The
registry factory has configuration but no map, so the existing map-bearing declaration is the
binding point. Every system receives an immutable course value retaining the terrain and its
selected corridor identity; temporary map destruction leaves those views valid. An unbound mode
raises `GAMEPLAY.RACE_COURSE_UNBOUND` instead of constructing incomplete systems. Terrain owns
node/shape validation; race requires the named road, a checkpoint, a spawn marker, and checkpoint/
spawn centres satisfying the old exact distance <= width convention. It does not require every
point of the gate's detection disc to be on the road. Application startup separately requires the
configured player's return disc at each checkpoint to be supported by full terrain, including
holes. Earlier gates remain earned after later falling, but support loss at a tied time wins.

The configuration factory validates the road name and bounds checkpoint radius to `(0, 10^12]`.
Binding checks that radius is no greater than the selected terrain half-width, whose terrain
authoring bound is `(0, 10^9]`. Equality with the selected width is accepted. `course_publisher`
publishes the selected road identity, gates, and timing. The client reads road geometry from the
shared terrain; the old track/width wire mirror was removed in Step 8. Duration conversion and
default balance values are unchanged.

A fallen racer keeps its entity, controller, and `RaceProgress`. With zero gates taken it returns
through `GridSpawnPolicy`; after a gate it returns to that checkpoint through the public
`simulation::seat_is_supported_and_unoccupied` and `simulation::seat_body_at_rest` operations. A blocked point is
retried one tick at a time. Both routes first offer the body on tick `N + D + 1` after elimination
at `N` with delay `D`. Mid-race joiners have no progress and wait for the next lobby.

The race block publishes the course and finish standings once per frame. Finishers on one tick
are ordered by certified normalized finish offset; exactly equal times share a placement. Their
controller ids preserve identity after an entity disappears. Once
someone finishes, the finish window replaces the time limit; when everyone finishes or the window
ends, the recorded first place wins, with a shared first place producing a draw. With no finisher,
the time limit ranks by gates taken only. No participants is a draw before either branch. The
client's course renderer, gates, countdowns, standings, and result sentences all use this same
block, and the `racer` controller reads it through snapshots without a gameplay dependency.

Measured by the same method as the hill: 20 C++ headers/sources, 1,188 physical lines; the mode
declaration alone is 168 lines across its header/source. The race README is excluded. Beyond that
directory, race adds one `RaceProgress` component and one mode-state arm with their protocol and
client registrations, one mode registry row, one configuration aggregate member, loader/build
entries, its map and replay/browser fixtures. It adds no command or event kind, contact equation,
or numbered kernel phase.

## Steering

`shared/locomotion` owns normalization, scaling, and the finite-step normal propulsion cap.
`steered_acceleration` delegates normalization and scaling while retaining its wider standalone
scalar domain. The magnitude clamp happens exactly once per new command, and the normalized
intent is retained privately on `Controllable`. `ThrustCommand` carries the submitted direction
verbatim and `InputBatch::create` range-checks each component against `[-1, 1]` and rejects a
present zero input generation, because
clamping at construction and again in the system would scale twice and is not bit-identical to
scaling once. The written operation order is the contract of
`docs/architecture/0005-royale-mode.md` § "Steering":

```
m = sqrt(x * x + y * y)
s = 1        when m <= 1
s = 1 / m    when m > 1
acceleration = ((x * s) * thrust_max, (y * s) * thrust_max)
```

`sqrt(x * x + y * y)` is written out rather than delegated to `std::hypot`, which computes a
different binary64 value for the same inputs.

The shared required `[movement]` pair replaces per-mode thrust authoring, including Sandbox's old
scalar. `simulation::MovementTuning` validates acceleration in `[0, 10000]` wu/s² and normal top
speed in `[1, 10000]` wu/s; runtime defaults are `400/600`. `GameModeConfiguration::movement`
carries startup authoring, and `MatchState::movement` owns current/default values, revision, and
effective tick. The stateless steering system reads current tuning every tick in every existing
phase. Competitive modes advertise seated `set_movement_tuning`; Sandbox has no seats and keeps
that command absent, but its locomotion still reads the shared authored pair.

Absent intent preserves authored acceleration before the first command. Explicit zero is a held
coast, not absence. Each later tick scales the retained intent without reclamping, so a committed
tuning change affects held input immediately. `seat_body_at_rest` clears previous-body intent,
including zero-delay replacements, without discarding newly recorded commands. Publication strips
private intent with the command list; room tuning remains public and survives round resets.

`input_is_locked` is the one active-stun predicate. Steering checks it before command handling
or the absent-intent early return, clearing intent to explicit zero and self-propulsion to zero.
Outside the lock, commands (including zero release) require exact optional `input_generation`
equality; a stale command does not replace a valid retained intent. Absent generation means never
invalidated and stays absent for all unchanged fixtures.

`status` runs last at PostKernel in all four modes. It validates and aggregates applicable
positive `StunRequest`s before any mutation, merges active windows by maximum expiry, replaces
expired windows without bridging gaps, and assigns the positive committing tick as generation.
Zero duration and missing/bodyless/static targets are no-ops. Expired status is erased; body loss
is cleaned only through the Step 13 trait. Neither first application nor later locked ticks
zero velocity: Step 18's impact owns momentum cancellation, and later bumps/lifetime continue.
The `StunRequest` producer is test-injected until Step 18, a narrow ADR 0004 foundation exception,
not a production stun command. `simulation::TickWindow` owns the checked half-open interval.

The propulsion cap constrains the canonical requested Euler endpoint to the computed squared
speed bound `max(normal_top_speed², current_velocity·current_velocity)`, returning acceleration,
never rewriting velocity. An already-admissible request is returned verbatim. Otherwise the
canonical locomotion owner uses its bounded radial/acceleration correction policy and rechecks
canonical integration plus non-amplification. Only the first computed projected endpoint equal
to current velocity is accepted as quantized coast; later identity or correction exhaustion fails
with `GAMEPLAY.LOCOMOTION_PRECISION_LOST`. This policy can reject valid ordinary-scale inputs;
it is not a totality or exact-real projection guarantee. Canonical physical-domain failures remain
visible. Integration, drag, and external collision momentum retain their original owners.

The independent frozen arithmetic and real-system sequence proof remains after delegation.
Legacy gameplay/replay construction explicitly retains its prior acceleration and an unreachable
normal ceiling of `10000`; its retained promotion evidence proves cap inactivity for those
horizons. This does not imply live continuous trajectories equal the discrete oracle.

## Determinism obligations

Everything `docs/architecture/0004-gameplay-architecture.md` § "Determinism obligations for
framework code" binds mode code to holds here: a system's `apply` is `const` and it holds immutable
configuration and nothing else, every value it mutates is world-owned, every iteration is over an
ascending component store or the canonical two-store join (`component_join.hpp`), and no mode reads
a clock, an unordered container, a pointer order, or a global.

Every declaration a mode returns is **independently owned**: `GameSimulation::create` reads the eight
declarations once and then destroys the mode, so a system, trigger policy, spawn policy, or objective
holding a pointer back into its mode would dangle. Motion predicates, binding and responses read the
same frozen post-intake/post-PreKernel world; their supplied subjects carry current resolved motion.
They return bodies/dispositions and typed effects without writing world state during solving.
The canonical solver owns swept candidates, contact/wall/trigger chronology, actual paths, and work
budgets. Source-effect admission never substitutes for its certified closing-impact requirement.
`GameSimulation` applies returned effects only after success and commits all world/RNG/tuning state
atomically after the remaining stages and validations.

## Verification

Focused tests are registered under the `blob_gameplay_unit_tests` CTest target with the
`unit.gameplay.` prefix, mirroring this directory under `tests/unit/gameplay/`. Run
`./scripts/verify-focused 'unit.gameplay'`; the canonical gate remains `./scripts/verify-linux pr`.

**The primary gameplay fixture is a replay**, not a hand-built world:
`(map, mode configuration, seed, command log)` as a directory under `tests/fixtures/replays/`, read
by `tests/fixtures/replay_fixture.hpp` and asserted by `tests/fixtures/royale_replay_fixture_tests.cpp`
under the `fixtures.` prefix. Every number in `docs/architecture/0005-royale-mode.md` is asserted
there rather than in a world assembled in C++, because the same tuple is what a bug report and a
replay viewer carry. The suite covers the six scenarios the ADR names, one directory each:

```
royale-thrust-integration     thrust_max * t at zero drag; (1, 1) at magnitude thrust_max;
                              acceleration persists with no further command
royale-drag-decay             the exact per-tick damping factor and the 199 wu/s discrete fixed
                              point rather than the 200 wu/s continuous limit
royale-spawn-order            consecutive ring points, a full ring deferring, a freed point seated
                              the next tick, and a joiner deferred for the whole match
royale-elimination-timing     elimination on exactly the Gth consecutive outside tick, a centre on
                              the boundary staying inside, and re-entry losing partial grace
royale-simultaneous-draw      one shared placement per tick, and placement 1 with a drawn outcome
                              for a mutual finish
royale-transition-per-tick    one phase per tick and a terminating cycle under all-zero durations
royale-scripted-match         the multi-entity match 100 fresh runs must reproduce bit-identically
```

`royale-drag-decay` pins the historical per-tick nonzero-drag equation requested by that ADR.
Zero drag elsewhere does not imply discrete/live equivalence: Step 16 deliberately changes swept
contact and wall chronology. Retain the independent old detector/oracle and explain each affected
live expectation; do not regenerate historical references to make them agree. Mac-hosted
Linux/amd64 verification and benchmark results are advisory, not native release/performance
certification.
