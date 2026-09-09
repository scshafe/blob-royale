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
  game_mode_configuration.hpp   every configured mode's `[<mode>]` section and the hazard table
  gameplay_validation_error.hpp the one exception vocabulary of this library
  shared/                       mechanics and values more than one mode or section uses
    thrust_steering_system.*    a validated thrust direction becomes stored acceleration
    duration_ticks.*            the one conversion from an authored duration to tick counts
    hazard_archetype.*          one validated `[hazard.<kind>]` section, in the units a spawner reads
    hazard_spawn_system.*       seats a crossing body per archetype whose interval is due
    lethal_hazard_contact_rule.* touching a lethal hazard eliminates the player, while a match runs
    lifetime_expiry_system.*    decrements `Lifetime` and despawns what runs out
    roster.hpp                  the two populations a rule reads: who is alive, who is playing
    lobby_start_rule.hpp        every seat filled and a start requested
    disc_geometry.hpp           whether a centre is outside a circle, written once
    spawn_point_probe.hpp       the forward probe from the rotation counter every policy shares
    next_free_spawn_point_policy.hpp  the next free point, in every phase but the wipe tick
    respawn_system.*            an elimination erases the body and starts a `RespawnTimer`
    match_reset_system.*        the restart wipe of every participant, on the lobby tick after ended
  king_of_the_hill/             a hill that tours the map, scored by holding it (ADR 0007)
    king_of_the_hill_configuration.*  the validated `[king_of_the_hill]` section, in the units systems read
    king_of_the_hill_mode.*     the seven declarations
    hill_geometry.*             where the hill is, as a pure function of the map's `hill` markers and one integer
    hill_movement_system.*      creates the hill entity once and writes its `Hill` each tick
    hill_scoring_system.*       presence toward the next point, and the point itself
    hill_objective.*            ending by points or by the clock, as three total predicates
    hill_rules_publisher_system.*  the three denominators on the wire
    king_of_the_hill_mode_state.hpp  the one answer to "what if the world holds another arm"
  sandbox/                      free play: thrust, bump, and nothing ever ends
    sandbox_mode.*              the seven declarations
    free_play_objective.hpp     always startable, never decided, zero durations
  royale/                       thrust and drag inside a shrinking zone, last blob standing
    royale_mode.*               the seven declarations
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
declare it, it moves to `shared/` and takes its scale as a constructor argument.

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
sandbox never reaches). Every royale fixture and the accepted baseline are unchanged by the five.

## Adding a game

```
new  src/gameplay/<mode>/<mode>_mode.{hpp,cpp}  the mode class, its systems, rules, policies
new  src/gameplay/<mode>/*_system.{hpp,cpp}     any mechanic only that mode declares
edit src/gameplay/game_mode_registry.hpp        one include and one row in kGameModeRegistrations
edit src/gameplay/CMakeLists.txt                the new .cpp files; the list is explicit, not a glob
edit match configuration                        `[match] mode=`
only if it is configured                        a `<mode>_configuration.{hpp,cpp}`, one member on
                                                GameModeConfiguration, and the `[<mode>]` fields in
                                                src/application/application_config_loader.cpp
do not touch                                    blob_simulation, blob_runtime, blob_server,
                                                blob_protocol, or any other mode
```

`@extension-point game_mode` — `game_mode_registry.hpp`. The table is `constexpr`, so two rows
claiming one name fail to compile rather than resolving to whichever was written first. A factory
takes one argument, the validated `GameModeConfiguration` carrying every configured mode's own
`[<mode>]` section, and reads only its own member. **One factory signature rather than one per
configured mode** is what keeps this the only file that knows which games exist: a caller that had
to choose a factory shape per mode would be a second such file. A mode whose balance is entirely
defaulted ignores the argument, which is what `GameModeRegistry::create(mode_name)` — the
defaults-only overload a test or a diagnostic uses — is for.

Two implementations of the seam, both registered: `sandbox` and `royale`.

## `sandbox`

Free play. It accepts `spawn`, `despawn`, and `thrust`; seats every joiner at the next free spawn
point in every phase; uses the engine's two built-in contact rows unchanged; declares one
`kPreKernel` system, `thrust_steering`; and never leaves `running` because its objective can always
start and is never decided. It contributes no component kind, no contact rule, no world event, no
mode-state block, and no `kPostKernel` or `kLifecycle` system.

`SandboxMode` is **89 lines** — a 52-line class block plus 37 lines of definitions — of which **58
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
and lethality — and the *kind name is the section's own instance name*, so nothing in `src/` names a
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
both must come from the world's seeded generator, so configuring the edge would make one component
of that geometry authored and the rest drawn. It would also need a closed edge vocabulary in C++,
and a designer wanting an edge the enumeration does not name would be back to writing code, which is
the bar the whole mechanic exists to clear.

## `royale`

Thrust and drag inside a linearly shrinking circular safe zone, last blob standing
(`docs/architecture/0005-royale-mode.md`). It accepts `spawn`, `despawn`, and `thrust`; uses the
engine's built-in contact rows unchanged beneath one row of its own, `lethal_hazard`, which computes
no physics at all, so it is still structurally incapable of reaching a different collision equation
for a pair of ordinary blobs; declares `thrust_steering` at `kPreKernel`, `zone_shrink` then
`zone_elimination` at `kPostKernel`, and `placement_recorder`, `match_reset`, `lifetime_expiry`,
`hazard_spawn` then `elimination_grace_publisher` at `kLifecycle`; seats joiners on a rotating ring
and only between matches; and ends when one blob or none is alive.

`RoyaleMode` is **137 lines** — a 69-line class block plus 68 lines of definitions — of which **89
are code**, against `SandboxMode`'s 89 and 58 measured the same way. Their class blocks are 35 and
30 lines of code, because every one of the seven declarations is still one line in both and the
whole of royale's excess there is two extra `create` overloads for its hazard table; the rest of the
difference is in the definitions, which are `systems()`'s seven rows instead of one and
`validate_map`'s two rejections instead of one. That is the seam holding: a second, far richer game
— a shrinking zone, elimination with a published grace, and crossing hazards — cost the mode class
five lines of code and its definitions twenty-six, and everything else it needed went into new
files.

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
timer at zero -- at which point the entity, still carrying its `Controllable` and everything else it
owned, is exactly what the engine's `SpawnSystem` calls "awaiting a body". An entity eliminated on
tick `N` with delay `D` is offered to the mode's spawn policy at phase 0 of tick `N + D + 1`; with
`D = 0`, on `N + 1`. A policy defers an entity that carries a timer, which is one predicate
(`shared/next_free_spawn_point_policy.hpp`). The body is the only thing erased; a mode that pairs
respawn with a body-bound counter of its own erases that counter in the system that owns it.

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
royale's nine command kinds; uses the engine's built-in contact rows beneath `lethal_hazard`;
declares `thrust_steering` at `kPreKernel`, `hill_movement` then `hill_scoring` at `kPostKernel`,
and `respawn`, `match_reset`, `lifetime_expiry`, `hazard_spawn` then `hill_rules_publisher` at
`kLifecycle`; seats joiners at the next free point in every phase, because the field is open; and
returns a knocked-out player after the configured respawn delay with its score intact.

What it contributed outside its own directory is two component headers plus one line in
`component_registry.hpp`, one mode-state header plus one type and one schema id in
`mode_match_state_registry.hpp`, one row in `game_mode_registry.hpp`, one member on
`GameModeConfiguration`, and the `[king_of_the_hill]` fields in the loader. It added no command
kind, no contact rule, no world event kind, and no kernel phase, and it is the first mode built on
the framework amendments of ADR 0007: the objective's tick context for its clock, the engine's
`previous_phase` through the shared reset, and the shared respawn.

`validate_map` rejects a map with no `hill` marker or no `spawn` marker at startup, naming the map.

## Steering

`steered_acceleration` is the one place a thrust direction becomes an acceleration, and **the
magnitude clamp happens exactly once, there**. `ThrustCommand` carries the submitted direction
verbatim and `InputBatch::create` only range-checks each component against `[-1, 1]`, because
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

## Determinism obligations

Everything `docs/architecture/0004-gameplay-architecture.md` § "Determinism obligations for
framework code" binds mode code to holds here: a system's `apply` is `const` and it holds immutable
configuration and nothing else, every value it mutates is world-owned, every iteration is over an
ascending component store or the canonical two-store join (`component_join.hpp`), and no mode reads
a clock, an unordered container, a pointer order, or a global.

Every declaration a mode returns is **independently owned**: the engine reads the seven declarations
once at construction and then destroys the mode, so a system, policy, or objective holding a pointer
back into its mode would dangle on the first tick.

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

`royale-drag-decay` is **the one fixture in the tree that runs at a nonzero drag**, which is what the
ADR's scenario table asks of it. Every other fixture keeps `drag_per_second = 0`, so every accepted
ADR 0003 horizon stays bit-identical.
