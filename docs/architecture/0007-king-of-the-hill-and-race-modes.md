<!-- canonical: king_of_the_hill_and_race_modes -- the second and third games: a moving hill scored by presence, and a checkpoint race with an out-of-bounds reset -->

# 7. Define the King of the Hill and Race game modes

* **Status:** Accepted
* **Date:** 2026-09-09
* **Deciders:** Project owner

**Implementation note, 2026-09-09:** The source now implements both modes, protocol 2.5, and the
client/controller paths described below. The execution amendments record the concrete costs and
differences from the proposal. The owner accepted the design with the larger-world/client-camera
requirement below. At that checkpoint the camera was documented but not implemented, and
deployment remained pending; the dated continuation below records the subsequent state.

**Continuation, 2026-09-10:** The compact hill deployment and API match verification completed
at `b1e80d4`, as recorded in `docs/playtests/2026-09-10.md`. The subsequent local camera follow-up
implements the movable viewport and follow/manual controls described in ADR 0004. Its separate
plan records large-map browser verification; the live hill server has not received that follow-up.

## Context and Problem Statement

The gameplay framework has one competitive game in it. `docs/architecture/0004-gameplay-architecture.md`
was accepted on the promise that "the second and third games are the reason the project exists",
and it proved its seven-declaration `GameMode` against three modes on paper: `sandbox`, `royale`,
and `capture_the_flag`. `royale` is built (`docs/architecture/0005-royale-mode.md`), rooms exist
(`docs/architecture/0006-lobbies-as-rooms.md`), hazards are a configuration section, and a person
can play a match from a browser. Every rule the tree knows is still royale's: eliminate by
attrition, rank the fallen, wipe the field.

The owner asked for two differently shaped games, in their words:

* **A race.** Players stay on a path with obstacles that leads to a finish. A player bumped out
  of bounds is sent back to a previous checkpoint.
* **King of the hill.** A safe space moves around, and players acquire points while in it.

Neither is an attrition game. Both are *scored* or *progressed* rather than survived, both need a
match to end on a clock rather than on an empty field, and both need a player who is knocked out
of play to *come back* rather than to be ranked and destroyed. Those three needs are exactly what
royale never had to express, so this ADR is the first real test of whether the framework is a
framework or royale with a lid on it.

Two questions are open. The first is what the smallest playable version of each game is on the
accepted physics and the accepted framework -- equal discs, elastic pairs, reflecting walls, a
400 Hz quantum, one fixed kernel with three named stages, markers as the one map authoring concept,
and a closed component registry. "Smallest" is a constraint here for the same reason it was in ADR
0005: a rule that changes a collision equation or the quantum invalidates every accepted fixture.

The second is where the framework has to move. ADR 0004 § "Justified extension points and what-if
stress" answered "king-of-the-hill scoring" with *new files only*. That answer was written before
the interfaces existed and is not quite true. Four things in the engine are royale-shaped in a way
that only shows once a second competitive game is designed against them, and this ADR names each
one and decides it rather than working around it. Every other decision here is a mode's own and
costs the engine nothing.

This decision applies "separation of policy from mechanism" -- both games are declared systems,
components, map markers, configuration keys, and objective predicates over an unchanged kernel;
"one capability, one implementation" -- a rule that a second mode needs is promoted to `shared/`
the day it has a second customer, and never copied; and "total functions over partial" -- every
roster state, every phase, and every accepted configuration has a defined result, including a
solo racer, a contested hill, and an empty field.

## Considered Options

The options are grouped by the decision they answer. Each chosen option is marked; the rest are
rejected with the reason.

**Where the hill lives.**

* **A. A new `Hill` component kind on its own entity.** *(chosen.)* Same shape as `Zone` --
  a centre and a radius -- and a different meaning: inside is where you score, not where you are
  safe from elimination. A component kind is its meaning, not its geometry.
* **B. Reuse `Zone` with a moving centre.** Rejected. `Zone`'s wire schema, its renderer, and
  `ZoneExposure` all say "outside this circle you are in danger"; a client keyed by kind would
  draw a hill as a shrinking safe zone and count exposure against it. A future mode that fields a
  hill *inside* a shrinking zone -- the obvious hybrid -- needs both on distinct kinds anyway.

**How the hill moves.**

* **A. A declared tour of `hill` markers, position a pure function of elapsed running ticks.**
  *(chosen.)* The hill dwells at each marker, glides to the next, and cycles. Any committed
  snapshot determines every later position, the way `zone_radius` already works, and a designer
  who wants a different route edits a CSV.
* **B. Seeded hops between markers.** Rejected for the first version. It draws from the
  generator and needs the current target in mode state; it is the named extension below.
* **C. A continuous seeded random walk.** Rejected. It accumulates floating-point state across
  ticks, which is the exact property `zone_shrink` was written to avoid.

**How presence becomes points.**

* **A. A per-entity `HillPresence` counter that rolls over into one whole `Score` point.**
  *(chosen.)* Points are integers a scoreboard can show, the counter is the HUD's progress ring,
  and "hold the hill for a second to score" is the rule people already know.
* **B. `Score.points` counts ticks on the hill.** Rejected. `score.points` is published as "one
  integer scoreboard cell", and a cell that holds 400 per second of presence is a field whose
  wire name lies about its unit.
* **C. Fractional points per tick.** Rejected. `Score` is an integer by design so a penalty is
  expressible; a fraction is a second numeric type on a cell that has one.

**How a match ends on a clock.**

* **A. `MatchObjective::outcome` receives the `TickContext`.** *(chosen.)* The objective is the
  mode's whole answer to "when is it decided"; a clock is one of the answers ADR 0004 listed for
  `MatchOutcome::drawn` and never made reachable. Three existing objectives ignore the argument.
* **B. A `kLifecycle` system stamps the elapsed running ticks into mode state for the objective
  to read.** Rejected. It is a system whose only purpose is to smuggle the tick past an interface
  that should carry it, and it puts an observed value that changes every tick into a block meant
  for declared constants and rankings.
* **C. `MatchState` carries the current tick.** Rejected. The tick sequence would then exist twice
  in every committed world, and one of the two could be stale.

**What happens to a player knocked out of play.**

* **A. The body is erased, a `RespawnTimer` is attached, and when it runs out the engine's own
  `SpawnSystem` seats the entity again through the mode's policy.** *(chosen.)* The entity and
  every other component it carries -- its score, its race progress, its controller link -- survive.
  "An entity awaits a body when it carries a `Controllable` and no `PhysicsBody`" is already the
  engine's definition, so no new state is needed to say "this one is coming back".
* **B. Destroy the entity and let the session re-spawn it.** Rejected. It destroys the score and
  the progress with the body, and it makes the session's spawn-retry cadence a game rule.
* **C. Teleport the body in place at `kPostKernel`.** Rejected. It skips the occupancy test that
  keeps two bodies from being seated in contact, and it gives being knocked out no cost at all.

**How a race course is authored and published.**

* **A. Three marker kinds -- `track` nodes as the centreline, `checkpoint` gates, `spawn` as the
  starting grid -- with the corridor half-width in `[race]`, and the whole course published once
  per frame in the race's mode-state block.** *(chosen.)*
* **B. Walls only.** Rejected. A corridor fenced by static bodies cannot be left, so "bumped out of
  bounds" has no meaning; walls are welcome *inside* the course as obstacles, which is data.
* **C. Course nodes as entities with components.** Rejected. A system may create one entity per
  tick (`kSystemCreatedEntityHeadroom = 1`, held there so recorded replays keep their ids), so a
  twelve-node course would take twelve ticks to exist, and a renderer keyed by component kind draws
  one entity and cannot draw the corridor between two. A course is one declared thing; a hill is
  one entity. The two publish differently because they are different.
* **D. Per-marker metadata for widths and associations.** Rejected for the first version. The
  `Marker` value already carries `MapMetadata` but `markers.csv` has no column for it; authoring
  it is the named extension below, and nothing here needs it.

**What being out of bounds means.**

* **A. The `track_bounds` system emits the existing `EliminationEvent`, and the shared respawn
  mechanic returns the racer to its last checkpoint.** *(chosen.)* `elimination_event.hpp` states
  that what a set of eliminations *means* -- "a shared rank, a respawn timer, a life decrement" --
  is the consumer's decision. A hazard strike and a fall off the road are the same event with the
  same consequence, and one consumer handles both.
* **B. A new `OffTrackEvent` kind.** Rejected. It would be a second name for "removed from play
  this tick" consumed by the same system, which is one capability with two spellings.

**When a race ends.**

* **A. The first finisher wins, and the match runs on until everyone has finished or a finish
  window after the first finish has elapsed, so the rest are ranked.** *(chosen.)*
* **B. End the match on the first finish.** Rejected. Nobody else gets a placement, and the
  restart delay is the only time anyone would see one.
* **C. Rank unfinished racers by distance along the course.** Rejected for the first version.
  Checkpoint order is an integer comparison; distance is a second geometry the HUD would then have
  to agree with. Named below.

**Where a racer returns to.**

* **A. The checkpoint marker itself, waiting a tick at a time while another body occupies it.**
  *(chosen.)* One point per checkpoint, the engine's occupancy predicate, no new authoring.
* **B. A row of generated slots across the gate.** Rejected for the first version; it needs the
  local track direction and a slot count, and it is the extension a playtest may ask for.
* **C. `spawn` markers associated with a checkpoint.** Rejected; it needs marker metadata and it
  would make every respawn slot count toward the lobby's seat ceiling.

## Decision Outcome

Build both modes as declarations over the unchanged kernel, and make four amendments to the
framework that the two games expose. **Every rule below is one of five things**: a system at a
declared stage, a value in a component, a key of the mode's own configuration section, a predicate
of the mode's `MatchObjective`, or a marker in the map. None of them is a phase inside
`GameSimulation`, and none of them is a field on the world outside the seams ADR 0004 cut for
exactly this.

The two games, in one sentence each. **King of the hill:** a hill of configured radius tours the
map's `hill` markers; every tick a player's centre is inside it counts toward the next point, a
contested hill scores nobody, and the first to `points_to_win` -- or the leader when the clock runs
out -- wins. **Race:** a corridor of configured half-width follows the map's `track` markers past
its `checkpoint` gates to the last gate, which is the finish; a racer whose centre leaves the
corridor, or who is struck by a lethal hazard, is out of play for a moment and returns to the last
gate it passed; the first across the finish wins, and the match runs until everyone has finished or
the finish window closes.

### Where the framework has to move

These four are the whole of what the engine and the shared layer change. Each is a real gap, each is
the smallest change that closes it, and each is listed with the alternative it beat.

**1. The objective receives the tick context.** `MatchObjective::outcome` becomes
`outcome(const GameWorld& world, const TickContext& context)`. `MatchLifecycleSystem::apply`
already holds the context and passes it through; `IdleMatchObjective`, `FreePlayObjective`, and
`RoyaleObjective` take the parameter and ignore it. A time limit is
`context.tick_sequence() - world.match().running_started_tick >= time_limit_ticks`, with the
saturating subtraction the lifecycle system already uses. `can_start` is unchanged: nothing about
starting is clocked.

**2. `previous_phase` is engine state.** `MatchState` gains `MatchPhase previous_phase`, written by
`MatchLifecycleSystem` at the top of `apply` -- `previous_phase = phase`, before the switch that may
commit a transition. Because that system is appended last at `kLifecycle`, every declared system on
tick `N + 1` reads `phase` as the phase tick `N` committed and `previous_phase` as the phase tick
`N - 1` committed, which is precisely the pair royale's `placement_recorder` has been observing for
itself. Royale's spawn policy and recorder read the engine field; the member stays in royale's
mode-state block as a *published mirror* the recorder copies, so `royale-mode-state.schema.json`
and every frame on the wire are unchanged. The reason it moves is that three modes each observing
"which phase did the previous tick commit" is three implementations of an engine fact, and the
shared restart wipe below cannot exist without it.

**3. Respawn is a shared mechanic.** A `RespawnTimer { ticks_remaining }` component joins the
registry, and `shared/respawn_system` at `kLifecycle` consumes the tick's `EliminationEvent`s:

    for every entity carrying a RespawnTimer, ascending:
        ticks_remaining -= 1; at zero the timer is erased and the entity awaits a body
    for every distinct eliminated entity, ascending, that carries a Controllable and a PhysicsBody:
        erase the PhysicsBody
        if respawn_delay_ticks > 0: attach RespawnTimer { respawn_delay_ticks }

The written order is the contract: an entity eliminated this tick is not decremented this tick.
An entity eliminated on tick `N` with delay `D` therefore awaits a body from tick `N + D` and is
offered to the spawn policy at phase 0 of tick `N + D + 1`; with `D = 0` it is offered on `N + 1`.
**The body is the only thing erased.** Score, race progress, and the controller link stay on the
entity, which is the whole point of not destroying it; a mode that pairs respawn with a body-bound
counter of its own -- `ZoneExposure`, `HillPresence` -- erases that counter in the system that owns
it. Royale does not declare `respawn` and keeps destroying the eliminated: attrition is its game.
A mode's spawn policy must defer an entity that still carries a timer, which is one predicate.

**4. Seating is a public helper.** The occupancy predicate and the at-rest seating write that live
inside `SpawnSystem::seat_pending_entities` become `simulation::point_is_occupied` and
`simulation::seat_body_at_rest` in `spawn_seating.hpp`, and `SpawnSystem` calls them. Race returns a
racer to a checkpoint from its own `kLifecycle` system, and that system must seat a body exactly as
the engine does -- same `2r + kPositionTolerance` test, same zero velocity and acceleration, same
configured radius -- or a body could be seated in contact. Two seating sites, one implementation.

### Shared rules promoted, because they now have a second customer

`src/gameplay/README.md`'s rule is that a system one mode declares lives in that mode's directory
and moves to `shared/` the day a second mode declares it. Five things cross that line here, each a
pure move plus, where stated, one added predicate:

| From | To | Change beyond the move |
|---|---|---|
| `royale/royale_roster.hpp` | `shared/roster.hpp` | gains `participant_entities` and `participant_count`: every entity carrying a `Controllable`, alive, respawning, or awaiting a seat |
| `RoyaleObjective::can_start`'s conjunction | `shared/lobby_start_rule.hpp`, `lobby_ready_to_start(world)` | none; `seats.is_full() && seats.start_requested()` |
| `zone_elimination.cpp`'s `center_is_outside` | `shared/disc_geometry.hpp`, `center_is_outside(position, center, radius)` | none; the same written-out expression, bit for bit |
| the forward probe loop both spawn policies repeat (`royale/rotating_ring_spawn_policy.hpp`, `sandbox/next_free_spawn_point_policy.hpp`) | `shared/spawn_point_probe.hpp`, `next_free_spawn_point(rotation_counter, spawn_point_is_free)` | none; royale's ring policy stays in `royale/`, calls it, and reads the engine's `previous_phase` |
| `sandbox/next_free_spawn_point_policy.hpp` | `shared/next_free_spawn_point_policy.hpp` | defers an entity carrying a `RespawnTimer`, and defers on the one `lobby` tick after `ended` so the restart wipe cannot destroy an entity the same tick seated; sandbox never reaches `ended` and holds no timer, so its behaviour is unchanged |

One rule is new to `shared/` rather than promoted: **`match_reset_system`** at `kLifecycle`, which
on a `lobby` tick whose `previous_phase` is `ended` destroys every entity carrying a `Controllable`.
It is royale's restart wipe generalized from "every alive entity" to "every participant", because a
respawning entity with a score and a pending joiner with race progress must not carry either into
the next match. Sessions already re-request a spawn when they own no entity
(`session_websocket_session.cpp`, `request_spawn_if_absent`), so the visible effect is the one the
royale winner already sees. Royale declares it too, right after `placement_recorder`, whose own wipe
it replaced; the replay fixtures' pinned entity counts, unchanged across the swap, are the proof that
royale never held a participant without a body on a tick it wiped.

### King of the hill

#### The mode declaration

| Declaration | `king_of_the_hill` |
|---|---|
| `name()` | `king_of_the_hill` |
| Components used | `PhysicsBody`, `Controllable`, `Score`, `Hill`, `HillPresence`, `RespawnTimer`, `Lifetime`, `LethalOnContact` |
| `kPreKernel` systems | `thrust_steering` |
| `kPostKernel` systems | `hill_movement`, then `hill_scoring` |
| `kLifecycle` systems | `respawn`, `match_reset`, `lifetime_expiry`, `hazard_spawn`, then `hill_rules_publisher` |
| `contact_rules()` | `lethal_hazard`, then the built-in rows |
| `accepted_command_kinds()` | royale's nine: spawn, despawn, thrust, join, leave, and the four lobby kinds |
| `spawn_policy()` | `NextFreeSpawnPointPolicy`: the next free point, in every phase, deferring a respawning entity |
| `objective()` | `HillObjective` |
| `validate_map()` | at least one `hill` marker and at least one `spawn` marker |
| Mode snapshot state | `king_of_the_hill`: three declared constants |

The field is **open**: a joiner is seated in any phase, because a hill match loses nothing by
someone arriving late -- they start on zero, like everyone did. That is the one place this mode
differs from royale in how it treats the roster, and it is the sandbox policy with one predicate
added rather than a new one.

The `kLifecycle` order is remove, reset, expire, add, publish. `respawn` first because it erases
bodies this tick's rules condemned; `match_reset` second so it acts on the roster as the tick
leaves it; `lifetime_expiry` and `hazard_spawn` exactly where royale has them and for the same
reasons; `hill_rules_publisher` last because it is the sole writer of the mode-state block and
nothing may run after it.

#### The hill

The hill is **an entity with a `Hill { center, radius }` component**, created by `hill_movement`
from the tick's `EntityIdReservation` on the first tick no entity carries `Hill`, and rewritten
every tick after. It owns no `PhysicsBody` and no `Controllable`, so it never collides, never
integrates, is never a participant, and is never wiped. An empty reservation on that first tick is
a hard failure naming the cause, exactly as `zone_shrink` fails, because a hill match with no hill
is a different game played silently.

Its centre is a pure function of the map's `hill` markers `m_0 .. m_{n-1}` in declared order, the
configured dwell `D` and travel `T` in ticks, and the elapsed running ticks `e`:

    S = D + T                                  (validation requires S >= 1)
    if n == 1:            center(e) = m_0
    k = (e / S) mod n     integer division
    t = e mod S
    if t < D:             center(e) = m_k
    else:                 f = (t - D) / T      T > 0 here, because t >= D and t < S
                          center(e) = m_k + (m_{(k+1) mod n} - m_k) * f, per component

**The division precedes the multiplication and that order is the contract**, as it is for
`zone_radius`. Per phase: `lobby` and `countdown` hold `center(0)`, which is `m_0`; `running`
evaluates `center(tick_sequence - running_started_tick)`; `ended` freezes at
`center(phase_started_tick - running_started_tick)`, the last running value. The radius is
`hill_radius_world_units` in every phase. A designer who wants the hill to hop rather than glide
sets `hill_travel_seconds = 0`; one who wants it never to move authors one marker.

#### Scoring

`hill_scoring` runs at `kPostKernel` after `hill_movement`, **only while `running`**, and reads
the hill this tick's `hill_movement` wrote. A centre is inside when it is not outside by
`shared/disc_geometry`'s predicate -- `sqrt(dx*dx + dy*dy) > radius + kPositionTolerance` -- so a
centre exactly on the rim is inside, consistent with the zone and with the baseline's inclusive
contact rule. Per tick, with `I` the configured `point_interval_ticks`:

    inside_count = number of alive entities whose centre is inside
    for every alive entity, ascending EntityId:
        if not inside:                                     erase HillPresence
        else if inside_count > 1 and not contested_hill_scores:  leave HillPresence as it is
        else:
            presence = (HillPresence or 0) + 1
            if presence >= I:   Score += 1, erase HillPresence
            else:               HillPresence = presence
    for every entity named by this tick's EliminationEvents:  erase HillPresence
    for every entity carrying HillPresence and no PhysicsBody, ascending:  erase HillPresence

Three consequences are the rules a player feels. **Leaving the hill loses the partial point**; a
contested hill **freezes** progress rather than losing it, so a rival who touches the rim for one
tick cannot erase a second of holding; and being knocked out of play forgets the partial point.
The elimination-event cleanup runs **after scoring**, so a whole point completed on the knockout
tick stands, then partial presence is erased before the shared respawn removes the body at
`kLifecycle`. Checking only for a missing body was insufficient: with a zero respawn delay, phase 0
of the next tick can seat the player before scoring ever observes it bodyless. The event cleanup
handles that case on the knockout tick, and the final bodyless pass still removes any older stale
presence. Both cleanups belong to the hill; shared respawn's behavior is unchanged. `I = 0` awards
a point on the first inside tick because the increment precedes the test;
`contested_hill_scores = true` is the free-for-all variant in which
everyone inside scores.

`Score` sits on the player entity, absent reads as zero, and it survives a respawn because the
entity does. A scoreboard is the `score` components the client already receives, joined to
`controllable.display_name`, sorted descending and then by ascending entity id; the client counts
nothing the server has not published.

#### Lifecycle and objective

`HillObjective` supplies three predicates and two durations. `can_start` is
`lobby_ready_to_start`. `outcome`, consulted only in `running` and evaluated after this tick's
scoring:

    participants = participant_entities(world)
    if participants is empty:                         drawn
    if participants has one entity:                   won_by_entity(it)
    best = the maximum Score among participants, absent reading as zero
    leaders = participants holding best, ascending
    if best >= points_to_win:                         one leader ? won_by_entity : drawn
    if elapsed_running_ticks >= time_limit_ticks:     one leader ? won_by_entity : drawn
    undecided

The order matters twice. A field that collapses to one is decided before the scoreboard is read,
which is royale's rule and is what makes an abandoned room resolve the same way in every mode
(ADR 0006 § "Rooms"). And two players reaching the threshold on the same tick is a draw rather than
a tie-break by id, for the reason royale draws a simultaneous elimination: the tick is the finest
grain the simulation resolves, and pretending otherwise would be a rule nobody could see.
`durations()` are `countdown_ticks` and `restart_delay_ticks` from `[king_of_the_hill]`.

#### Mode configuration

One strict `[king_of_the_hill]` section; every key required, no silent default, every value finite,
durations converted once by `shared/duration_ticks`. The loader's rule that a mode's section is
required whatever `[match] mode` names applies (`application_config_loader.cpp`), so every
full application configuration gains this section and the race's; the cost is stated under
§ "Consequences".

| Key | Proposed | Rule |
|---|---:|---|
| `thrust_max_world_units_per_second_squared` | 400 | `require_valid_thrust_maximum` |
| `hill_radius_world_units` | 90 | finite, `0 < radius <= 10^12` |
| `hill_dwell_seconds` | 12 | finite, not negative |
| `hill_travel_seconds` | 4 | finite, not negative; dwell plus travel converts to at least one tick |
| `point_interval_seconds` | 1 | finite, not negative |
| `points_to_win` | 30 | integer, `1 <= points_to_win <= 2^53 - 1` |
| `contested_hill_scores` | false | boolean |
| `time_limit_seconds` | 240 | finite, strictly positive |
| `respawn_delay_seconds` | 2 | finite, not negative |
| `countdown_seconds` | 5 | finite, not negative |
| `restart_delay_seconds` | 8 | finite, not negative |

#### Mode state and the wire

The block `king_of_the_hill` carries three declared constants and nothing observed:
`points_to_win`, `point_interval_ticks`, `time_limit_ticks`. Scores are components, the hill is an
entity, and the elapsed running time is `tick_sequence - phase_started_tick` while `running`, all
of which the client already has; what it cannot compute is the three denominators. They are stamped
every tick by `hill_rules_publisher`, declared last, for the reason `elimination_grace_publisher`
gives: the block is the mode's contribution to every snapshot, a counter without its bound is
uninterpretable, and no committed tick may publish an unstamped zero.

#### Map requirements

`validate_map` rejects a map with no `hill` marker or no `spawn` marker, naming the map and the
missing kind. A hill marker may sit anywhere in the closed arena; its disc may extend past a wall,
which only makes that part of the hill unreachable. A map that also carries `track` or `checkpoint`
markers is playable; the mode ignores kinds it does not understand, which is the rule that lets one
map host more than one game.

### Race

#### The mode declaration

| Declaration | `race` |
|---|---|
| `name()` | `race` |
| Components used | `PhysicsBody`, `Controllable`, `RaceProgress`, `RespawnTimer`, `Lifetime`, `LethalOnContact` |
| `kPreKernel` systems | `thrust_steering` |
| `kPostKernel` systems | `checkpoint_progress`, then `track_bounds` |
| `kLifecycle` systems | `standings_recorder`, `checkpoint_respawn`, `respawn`, `match_reset`, `lifetime_expiry`, `hazard_spawn`, then `course_publisher` |
| `contact_rules()` | `lethal_hazard`, then the built-in rows |
| `accepted_command_kinds()` | royale's nine |
| `spawn_policy()` | `GridSpawnPolicy`: the grid between matches; mid-race, only a racer returning to the grid |
| `objective()` | `RaceObjective` |
| `validate_map()` | the course rules under § "Map requirements" |
| Mode snapshot state | `race`: the course, the standings, and two declared durations |

The field is **closed**, like royale's: a joiner who arrives mid-race waits for the next lobby
rather than starting a lap behind. `GridSpawnPolicy` is royale's ring rule over the grid -- the
shared forward probe, deferred while `running` or `ended` and on the lobby tick after `ended` --
with one predicate added: while `running` it seats an entity that carries `RaceProgress { 0 }` and
no `RespawnTimer`, which is a racer returning to the grid, and nothing else. The `kLifecycle` order is record, return, remove, reset, expire, add, publish, and two
adjacencies carry the design: `checkpoint_respawn` runs *before* `respawn` so that an entity whose
timer expires this tick is seated by the race system on the *next* tick, the same tick the engine's
`SpawnSystem` would have seated it, and the two routes back into the field agree to the tick;
`course_publisher` runs last as the sole final writer of the block.

#### The course

Three marker kinds, all read in declared order, and one number:

* **`track`** markers are the centreline, a polyline through them in declared order. At least two,
  with no two consecutive markers coincident.
* **`checkpoint`** markers are gates `c_0 .. c_{n-1}`, discs of the configured
  `checkpoint_radius_world_units`. At least one. **The last gate is the finish.**
* **`spawn`** markers are the starting grid. The application's `require_lobby_fits_map` already
  bounds the seat count by their number, so a grid is authored as one marker per seat the map is
  meant to hold, and that bound now means what a designer would guess it means.
* **`track_half_width_world_units`** in `[race]` is the corridor's half-width. It is configuration
  rather than map content only because `map.cfg` cannot yet carry an authored key; a deployment
  names one map and one mode, so the two travel together. Per-map and per-node widths are the
  marker-metadata extension named below.

The course value -- nodes, gates, half-width, gate radius -- is built once from the map and the
configuration by `RaceMode::validate_map` (`race/race_course.hpp`). The registry factory has no map;
the engine already calls this declaration before `systems()`, so setup binds the validated course
there and hands an independent immutable value to each system. `systems()` before successful
binding fails with `GAMEPLAY.RACE_COURSE_UNBOUND`; failed map validation clears an earlier binding.
The mode is destroyed before the first tick. Distance from a point `p` to the centreline is the minimum over segments
`(a, b)` of the written-out point-to-segment distance:

    dx = b.x - a.x;  dy = b.y - a.y;  wx = p.x - a.x;  wy = p.y - a.y
    t = (wx * dx + wy * dy) / (dx * dx + dy * dy)         the denominator is positive by validation
    t = 0 when t < 0;  t = 1 when t > 1
    cx = a.x + (dx * t);  cy = a.y + (dy * t)
    ex = p.x - cx;  ey = p.y - cy
    distance = sqrt(ex * ex + ey * ey)

The course uses `sqrt` of the written-out sum. The corridor is
every point whose distance is at most the half-width.

Obstacles are `static_bodies.csv` and cost no code, which was ADR 0004's obstacle-course answer and
is unchanged. Moving obstacles are `[hazard.<kind>]` sections; a lethal one throws a racer back a
checkpoint, a heavy one merely shoves.

#### Progress and finishing

`RaceProgress { next_checkpoint }` is the one per-racer value, and `checkpoint_progress` at
`kPostKernel`, **only while `running`**, is its only writer. Per tick, for every alive entity in
ascending order:

    progress = the entity's RaceProgress, or RaceProgress { 0 } attached now
    if progress.next_checkpoint < n and the centre is inside c_{next_checkpoint}:
        next_checkpoint += 1

Gates are taken **in order and one per tick**: only the next gate is tested, so a course that doubles
back past an earlier gate cannot be gamed, and skipping ahead is impossible by construction. An
entity with `next_checkpoint == n` has finished; it keeps its body and keeps driving, because a
finished blob that vanished would change every contact its rivals were about to have. Attaching
`RaceProgress { 0 }` to any alive entity that lacks one, on every running tick rather than only the
first, is what makes "an entity in the field is in the race" total; the spawn policy is what
prevents a fresh joiner from ever being in the field mid-race.

`standings_recorder` at `kLifecycle` is the sole writer of the observed half of the block. On the
first `running` tick -- `previous_phase == countdown` -- it clears the standings. Then, for the
finishers of this tick -- alive entities whose `next_checkpoint` is `n` and which the standings do
not yet name, ascending -- it appends one entry each `{ entity, controller, placement,
finished_tick }` with the one shared placement `standings.size() + 1`. Two racers crossing on the
same tick share a placement, for the reason royale shares an elimination placement. The controller
is recorded because it is the value a client recognizes its own result by after a wipe.

#### Out of bounds, and returning to a checkpoint

`track_bounds` at `kPostKernel`, after `checkpoint_progress` and only while `running`, emits one
`EliminationEvent` for every alive entity whose centre's distance to the centreline exceeds
`track_half_width + kPositionTolerance`. Progress runs first, so the tick retains a gate taken
before evaluating the return consequence. Validation keeps every gate's **centre** inside the
corridor, but an off-centre gate disc can extend beyond it. A racer in that overlap can therefore
take a gate and be thrown off the road on the same tick, returning to the gate it just took. The
proposal's claim that every centre inside a gate must also be on the road was too strong.

The shared `respawn` erases the body and starts the timer. What differs from the hill is *where* the
racer comes back, and that is `checkpoint_respawn`:

    for every entity carrying Controllable and RaceProgress, no PhysicsBody, no RespawnTimer,
    ascending EntityId:
        k = next_checkpoint
        target = the grid, seated through the spawn policy   when k == 0
                 c_{k-1}                                       when k >= 1
        if k >= 1 and not point_is_occupied(target):  seat_body_at_rest(entity, target)
        otherwise leave the entity awaiting a body: it is offered again next tick

A racer who never took a gate returns to the grid, which is the engine's own seating through the
mode's policy -- while `running` the policy seats an entity only when it carries `RaceProgress { 0 }`
and no timer, and every other entity only between matches. A racer who took at least one gate returns to the gate it last took,
seated by this system with the engine's own predicate and write. The one-tick alignment stated
under the declaration table is what keeps "seated on tick `N + D + 1`" true on both routes. A gate
somebody is standing on delays the return by a tick at a time rather than seating two bodies in
contact; a designer who sees that abused authors a wider course or asks for the slot extension.

#### Lifecycle and objective

`RaceObjective`: `can_start` is `lobby_ready_to_start`; `outcome`, consulted only in `running` and
evaluated after this tick's recording:

    participants = participant_entities(world)
    if participants is empty:                                        drawn
    if the standings are non-empty:
        if every participant is named in the standings,
           or elapsed since standings.front().finished_tick >= finish_window_ticks:
               winners = entries holding placement 1
               one winner ? won_by_entity(it) : drawn
        undecided
    if elapsed_running_ticks >= time_limit_ticks:
        leaders = participants holding the maximum next_checkpoint, absent reading as zero
        one leader ? won_by_entity(it) : drawn
    undecided

A field of one is **not** decided early here, unlike the hill: a solo race is a time trial and is a
legitimate way to learn a course. Nobody finishing before the clock ranks the field by gates taken,
which is the integer the HUD already shows, and a tie at the front is a draw. `durations()` are the
section's countdown and restart delay.

#### Mode configuration

One strict `[race]` section, same rules as the hill's:

| Key | Proposed | Rule |
|---|---:|---|
| `thrust_max_world_units_per_second_squared` | 400 | `require_valid_thrust_maximum` |
| `track_half_width_world_units` | 70 | finite, `0 < half_width <= 10^12` |
| `checkpoint_radius_world_units` | 40 | finite, `0 < radius <= 10^12`, at most the half-width |
| `respawn_delay_seconds` | 2 | finite, not negative |
| `finish_window_seconds` | 20 | finite, not negative |
| `time_limit_seconds` | 240 | finite, strictly positive |
| `countdown_seconds` | 5 | finite, not negative |
| `restart_delay_seconds` | 8 | finite, not negative |

#### Mode state and the wire

The block `race` carries the declared course -- `track_half_width`, `checkpoint_radius`, `track` as
an array of points, `checkpoints` as an array of points -- the declared `time_limit_ticks` and
`finish_window_ticks`, and the observed `standings`. The course is declared map content mirrored
onto every frame, which is what `elimination_grace_ticks` already is for a configuration value and
what a map's static bodies already are for geometry: a snapshot is a complete picture of the world,
and a client that had to fetch the course separately would be rendering two documents that could
disagree. `course_publisher` stamps the declared members every tick, last in its stage;
`standings_recorder` owns the observed one. A racer's own gate is its `race_progress` component;
its return countdown is its `respawn_timer`.

**Publication-bound correction, 2026-09-09:** Finite and positive alone did not make a configured
dimension publishable. Both configuration factories now enforce
`simulation::kMaximumPhysicalComponentMagnitude` (`10^12`) for hill radius and race half-width/radius;
the hill also enforces `simulation::kMaximumProtocolSafeInteger` (`2^53 - 1`) for `points_to_win`.
The inclusive bounds match protocol 2.5's existing schemas, so this is startup validation of the
accepted contract, not a schema revision. Oversized values use the existing scalar-out-of-range
errors and authored-key contexts; zero points keeps its specific error. Defaults and duration
conversion are unchanged.

#### Map requirements

`validate_map` rejects, naming the map and the cause: fewer than two `track` markers; two
consecutive `track` markers at the same position; no `checkpoint` marker; a `checkpoint` centre
whose distance to the centreline exceeds the half-width; a `spawn` marker outside the corridor; and
no `spawn` marker at all. A `checkpoint_radius` above the half-width is refused when the section is
validated, before any map is read. Gates need not lie on the centreline, and a course need not be
closed: laps are the extension named below, and the first version is point to point.

### Protocol 2.5

One minor, republishing the schema set with:

* four component kinds -- `hill { center, radius }`, `hill_presence { inside_ticks }`,
  `race_progress { next_checkpoint }`, `respawn_timer { ticks_remaining }` -- each its own schema,
  encoder, `kV2ComponentKindNames` entry, and client renderer registration;
* two mode-state schemas, `blob-royale://protocol/v2/mode-state/king-of-the-hill` and
  `.../mode-state/race`, as enum members of `common.schema.json#/$defs/mode_state_schema_id` with
  their `if/then` rows in `match-data.schema.json`, encoded in the member order each block's
  section above lists.

`welcome.mode` and the directory's `mode` are already the open `kind_name` grammar, so a new mode
name is not a version. `match.placements` stays royale's ranking and is empty for both new modes,
because its entry names an `eliminated_tick` and a race finish is not an elimination; the race
publishes `standings` with a `finished_tick` in its own block rather than lending a member a
meaning it does not have. Fail-closed decoding is unchanged: a 2.4 client refuses a 2.5 frame, and
the deployed client ships with the server that speaks it.

The concrete encoder extension is `ComponentObjectSink::set_object_array` in
`src/protocol/component_encoding.hpp`. The race specialization in `mode_state_wire_encoding.hpp`
uses it for ordered course points and standings; the JSON implementation supplies the storage.
This keeps those arrays within the existing mode-state encoding registration instead of adding a
race branch to the top-level snapshot encoder. Protocol 2.5's registrations and generated client
artifacts were complete at the mode-plan checkpoint; the compact hill deployment subsequently
completed as recorded in the 2026-09-10 continuation above.

### The client

**Larger worlds, independent viewport — owner amendment, 2026-09-09.** Neither game is constrained
to a one-screen map. Hill tours and race courses require worlds that can be much larger than the
displayed region, viewed at a useful local scale through a movable client camera. The canonical
contract is ADR 0004 § "World space and the client viewport": one transform shared by every visual
layer, a player-centred follow option, and independently positioned manual panning to consider with
an explicit return-to-follow control. Camera movement changes no world coordinates, race progress,
hill scoring, or commands. Follow retains strict centring at map edges by allowing outside-map
background, and reacquires the body by controller identity across returns and entity replacement.

The implementation originally verified by this mode plan fit the entire map into the canvas; it had
no movable camera or follow/manual controls. Those compact maps and scaled browser fixtures prove
the mode rules and original drawing, not large-map camera usability. The separate 2026-09-10 camera
follow-up converges entity and course rendering on one uniform translated projection, defaults to
strict player-follow, and offers manual drag/pan buttons with explicit return to follow. Its
larger-than-viewport verification is recorded separately, without rewriting the original mode
plan's evidence. ADR 0004 owns the concrete scale, input, bounds, DPR, and lifecycle choices.

The renderer registry gains four entries: `hill` draws a filled disc distinct from the zone's ring
at the zone layer; `hill_presence`, `race_progress`, and `respawn_timer` are non-visual and reach
the HUD through `sessionSelectors`. One new client seam earns its place, **`mode_state_renderer`**:
a registry keyed by mode-state schema id whose entry draws once per frame from the block, before
any entity layer. The race entry draws the corridor as a stroked polyline of `2 * track_half_width`
and each gate as a disc, the finish distinguished; the royale and hill entries are non-visual. It
exists because a course is one declared thing rather than a set of entities, and the per-entity
registry cannot draw a line between two of them.

The HUD selects each mode section by the closed `mode_state.schema_id`, matching the renderer
registry; `match.mode` is an open name and is not the source of block shape. The hill shows the
scoreboard, the local player's progress toward its next point as a ring against
`point_interval_ticks`, and the time remaining; the race shows the local racer's gate `k of n`, the
standings as they fill, and "back on the road in" from `respawn_timer.ticks_remaining`. Before the
first finish it shows the running time left; afterwards the first standing's `finished_tick` starts
the finish window, which replaces the running limit. A racer with progress and no body or timer
still sees zero seconds and a wait for a clear checkpoint or grid point. Recorded controller ids
identify the session's finish even after its live entity disappears. The results overlay at `ended`
reads the generic outcome and distinguishes a recorded finish from a no-finisher win on gates
taken, a shared finish, a clock tie, and an empty field. It does not construct a distance ranking.
Nothing here is computed from a wall clock: every countdown is a tick count or difference from the frame.

### Bots

Two controllers in `blob_controllers`, each new files plus one registry row, so a lobby can seat
them from the menu the client already has:

* **`hill_seeker`** thrusts toward the `hill` entity's centre and holds there; its personality is
  an approach bias and a seeded jitter so two seekers do not stack.
* **`racer`** reads the course from the frame's mode state and its own gate from `race_progress`;
  it thrusts toward the next gate, and toward the nearest centreline point instead whenever its
  centre is farther from the centreline than a caution fraction of the half-width. It will fall off
  a sharp course and come back, which is the game. The registered implementation's caution
  fraction is deterministic and it consumes no random draws. Its factory retains the seed for
  identity and future personality variation; that parameter does not imply current random steering.

Both decide from the snapshot alone, submit through the sink like every other controller, and are
replayed by the recorded command log rather than re-run, so a determinism fixture can carry either.

### Fixtures and verification

Each mode ships a map under `maps/` -- a hill arena with four `hill` markers and a point-to-point
circuit with a bend, three gates, a grid of four, and two static obstacles -- and replay fixtures
under `tests/fixtures/replays/` in the accepted `(map, mode configuration, seed, command log)`
format, whose reader gains the two sections. The invariants they pin, in the same style as royale's:
the tick the hill first moves; the tick a point is awarded and that a contested tick awards none;
the tick a hill match is decided by threshold and, in a second fixture, by the clock; the tick a
racer takes each gate; the tick its body disappears off the road and the tick it is back at the
gate; the shared placement of two same-tick finishers; the decision at the finish window. All run
under the 100-fresh-run bit-identity harness, which becomes mode-generic by building the mode
`match.ini` names through `GameModeRegistry::create`. The accepted baseline oracle, every royale
fixture, and every wall and pair fixture must pass untouched, and `git diff --stat -- maps/` over
the shipped arena stays empty: both new maps are new directories.

Browser coverage is one Playwright flow per mode on its own fixture configuration, driving the
lobby through the client's own controls exactly as the rooms flow does.

### What is deliberately not built

Named so that nobody mistakes the first version for the whole design:

* **Human large-map playtesting and camera deployment.** The local 2026-09-10 follow-up implements
  the shared projection, centred follow, and manual pan controls. Its own tests, not the original
  whole-map-fit tests, establish automated camera acceptance. Human impressions, live camera
  deployment, and the later live race session remain separate work.
* **Laps and closed courses.** `lap_count` and a closing segment from the last `track` node to the
  first; the gate rule is already order-only, so this is the objective and the course value.
* **Marker metadata authoring.** A `metadata` column in `markers.csv` and a `[metadata]` section in
  `map.cfg`, unlocking per-map and per-node corridor widths, gate-associated respawn slots, and
  per-hill dwell overrides. The `Marker` value already carries the field.
* **Generated respawn slots across a gate**, and a brief no-contact window after a return.
* **A seeded hill tour**, drawing the next marker from `GameWorld::random()` and holding the target
  in mode state.
* **Distance-along-course ranking** for unfinished racers, and a live position readout.
* **Team variants** of both games: `Team` and `MatchOutcome::won_by_team` exist, and per-team scores
  are the capture-the-flag block ADR 0004 sketched.
* **Per-room modes.** Every room still plays `[match] mode`; the directory already publishes `mode`
  per listing, and this is an ADR 0006 extension, not a mode question.

### Where each new thing goes

The honest count, in the shape ADR 0004 § "Libraries, and where a new thing goes" uses. Existing
files edited are listed in full because the table's whole purpose is that number.

| I add | I create | I edit |
|---|---|---|
| The objective's clock | nothing | `match_objective.hpp`, `match_lifecycle_system.cpp`, and the three objectives that take and ignore the argument |
| `previous_phase` | nothing | `match_state.hpp`, `match_lifecycle_system.cpp`, royale's policy and recorder, `royale_mode_state.hpp` |
| Respawn | `components/respawn_timer_component.hpp`, `shared/respawn_system.{hpp,cpp}`, the wire schema, encoder, and renderer entry | `component_registry.hpp`, `component_encoding_registry.hpp`, `common.schema.json`, `entityRendererRegistry.ts` |
| Public seating | `spawn_seating.{hpp,cpp}` | `spawn_system.cpp` |
| The five promotions and `match_reset` | `shared/{roster,lobby_start_rule,disc_geometry}.hpp`, `shared/match_reset_system.{hpp,cpp}` | royale's and sandbox's includes and declared lists, `zone_elimination.cpp` |
| A mode | `src/gameplay/<mode>/` with its mode, configuration, course or hill geometry, systems, objective, and mode-state header; its map directory; its fixtures | `game_mode_registry.hpp` (one row), `game_mode_configuration.hpp` (one member), `mode_match_state_registry.hpp` (one arm), `application_config_loader.cpp` (one section), `replay_fixture.cpp` (one section), `CMakeLists.txt`, and **every full application configuration** (one section) |
| A component kind | its header, schema, encoder, renderer | `component_registry.hpp`, `component_encoding_registry.hpp`, `common.schema.json`, `entityRendererRegistry.ts` |
| A mode-state block | its header, schema, encoder, mode-state renderer entry | `mode_match_state_registry.hpp`, `mode_state_wire_encoding.hpp`, `common.schema.json`, `match-data.schema.json`, `modeStateRendererRegistry.ts` |
| A bot | `src/controllers/<name>_controller.{hpp,cpp}` and its tests | `controller_registry.hpp`, `src/controllers/CMakeLists.txt` |

Two of the rows are the ones ADR 0004 counted and they count the same. The mode row is where the
first estimate was optimistic: "new files plus one registration line" was true of the mode class
and is not true of a *configured* mode, which touches registry/configuration declarations and every full application configuration.
The configuration-file cost is the loader's own rule and is accepted rather than worked around.

Measured from production `.hpp` and `.cpp` files under each mode directory on 2026-09-09, with
`wc -l` including comments and blank lines, after the publication-bound and zero-delay-return
review corrections: the hill is 15 files / 1,160 lines; race is 20 files / 1,188 lines. Their mode
declarations alone are 194 and 168 lines respectively. Tests, README files,
shared mechanics, component and mode-state values, protocol, and frontend are excluded from those
directory counts and remain additional work; `src/gameplay/README.md` gives the inventory.

## Consequences

* **Positive:** The framework survives two games with nothing in common with royale. Neither mode
  adds a kernel phase, a contact equation, a world field, or a controller capability; both are
  declared lists, components, markers, and predicates, and the baseline oracle does not move.
* **Positive:** Respawn, the restart wipe, the roster vocabulary, the lobby start rule, the disc
  predicate, the spawn probe, and the open-field spawn policy exist once. A fourth mode chooses
  among them rather than writing them.
* **Positive:** Bots play both games through the same two capabilities every controller has, and
  either controller's recorded command log is a fixture like any other.
* **Positive:** The four framework amendments are each a gap the existing ADR promised to fill --
  a clock-decided outcome, a team-agnostic restart, a respawn under the same `ControllerId`, and a
  second seating site -- and each is one small edit rather than a workaround a mode would carry.
* **Negative:** Protocol 2.5 is the largest minor so far: four kinds and two blocks. Every client
  registry gains four entries, and a new client seam exists for one customer.
* **Mitigation:** The second customer for the mode-state renderer is already named -- a capture
  zone, a per-team score bar -- and the four kinds are the smallest set that publishes a hill, a
  gate, a countdown, and a scoreboard without a second source for any of them.
* **Negative:** Every full application configuration gains two sections of balance for games it
  may never run. The measured standalone count is 14: `config/blob-royale.cfg`, the deployment
  configuration, seven browser configurations, and five full application fuzz corpus inputs.
  Inline unit-test configuration strings and the generated integration configuration in
  `tests/integration/server_process_fixture.cpp::write_fixture_inputs` are additional; the builder
  also emits both sections even for workloads running other modes. Replay `match.ini` files use
  their own selected-mode sections and are not counted as full application configurations.
* **Mitigation:** That is the loader's accepted rule -- switching `mode=` must not fail on a section
  nobody wrote -- and each section is eleven or eight lines once. A per-mode optional section is a
  loader decision to make on its own evidence, not here.
* **Negative:** `previous_phase` exists twice in a royale world: the engine field and the published
  mirror in royale's block.
* **Mitigation:** The mirror is write-only, copied from the engine field by the one system that
  publishes the block, and it exists so the wire does not change; retiring it is a major version
  and is not worth one.
* **Negative:** A racer returning to a gate somebody is standing on waits, a tick at a time, with no
  bound but the standing body leaving.
* **Mitigation:** It is total rather than failing, the timer the client shows keeps counting at
  zero so the wait is visible, and the slot extension is named and small.
* **Negative:** The corridor half-width is a `[race]` key rather than the map's own number, so a
  second race map needs a second configuration file until marker metadata is authorable.
* **Mitigation:** A deployment already names one map and one mode per file; the metadata extension
  is the honest fix and is listed.
* **Operational:** Two new maps, two new fixture families, two new browser flows, two new bots, and
  a 2.5 schema republish. The deployed `[match] mode` still selects one game per process; playing a
  different one on the tailnet is `./scripts/reconfigure-tailnet` with a different section, or the
  per-room extension.
* **Reversibility:** The mode directories, registry/configuration entries, and client paths can be
  removed, retaining the shared framework amendments. Removing published wire kinds from a
  deployed 2.5 contract requires a major protocol version; the source rollback is not permission to
  silently change that contract. The existing modes retain their accepted fixture behavior.

## Related

* [`0004-gameplay-architecture.md`](0004-gameplay-architecture.md) -- the framework this proves
  against, and the three sections it amends: the objective signature, `MatchState`, and the
  what-if row for king of the hill.
* [`0005-royale-mode.md`](0005-royale-mode.md) -- the first mode, whose roster, spawn probe, disc
  predicate, and lobby start rule are promoted here unchanged.
* [`0006-lobbies-as-rooms.md`](0006-lobbies-as-rooms.md) -- the abandonment rule both objectives
  honour, and the per-room mode extension this ADR declines to make.
* [`../protocol/v2.md`](../protocol/v2.md) -- protocol 2.5's registration procedure.
* [`../../src/gameplay/README.md`](../../src/gameplay/README.md) -- the `shared/` promotion rule
  applied five times here.
* [`../../.claude/plans/2026-09-09-king-of-the-hill-and-race-modes.md`](../../.claude/plans/2026-09-09-king-of-the-hill-and-race-modes.md)
  -- the execution plan.

**Amended 2026-09-10 (ADR 0008, plan Step 1):** ADR 0008 is accepted and changes five things here
when their steps land. § "The course" and § "Map requirements": the road is authored once as a
named terrain corridor, and `[race] track_half_width` and the `track` marker kind retire (plan
Steps 3 and 6). § "Mode state and the wire" and § "Protocol 2.5": the race block stops carrying
`track` and `track_half_width` after a one-step derived mirror under session v3 (Steps 7 and 8).
§ "Out of bounds, and returning to a checkpoint": endpoint sampling becomes a chronological motion
trigger with a within-tick finish offset (Step 17). § "King of the hill": the marker tour gains a
`random_roam` policy over committed motion state and a dedicated random stream (Steps 9 and 12).
§ "Bots": the racer's private polyline arithmetic is replaced by the canonical terrain query that
controllers may link (Step 8). The accepted hill and race replay expectations stand until each
step and change only with written rationale; the decision and its `Accepted` status are unchanged
today.

## Amended 2026-09-10 (ADR 0008, plan Step 6): named race road

§ "The course", § "Map requirements", and the race configuration table now select a named
terrain corridor with required `[race] road` (the explicit defaults-only value is `road`).
The former `[race] track_half_width_world_units` key and `track` marker geometry are retired.
The map's corridor owns its ordered points and half-width, under the existing terrain limits
of 8 corridors, 32 aggregate segments, 40 aggregate points, and dimensions at most 10^9 wu.
The terrain factory, not race marker validation, rejects degenerate segments. A malformed road
name fails configuration validation; a missing named road fails race map binding. Checkpoint
radius remains positive and bounded at configuration intake, with radius <= road half-width
checked after binding to the actual map.

`RaceCourse` retains owning immutable terrain and its corridor identity, so courses created
from temporary maps remain valid. Distance delegates to the canonical corridor query with
the promoted arithmetic unchanged. Startup checkpoint/spawn admission still compares exact
distance with the half-width; it does not substitute full Boolean terrain support or widen
the old boundary with another tolerance.

The race mode-state `track`/`track_half_width` fields remain a per-tick derived mirror of this
same corridor until Step 8. Wire, racer-controller geometry, rendering, endpoint track-bound
sampling, and checkpoint chronology are unchanged by this step. The full published mirrors,
commands, and independently derived replay outcomes remain the acceptance contract.

Replay maps now use the production map-directory format bundled beneath each replay. This
removes the fixture's separate marker/map reader without a new production parser API. Map
identity, bounds, marker order, and authored race geometry retain their previous values;
required display metadata and explicit terrain are new authored map facts, not a claim that
the old and new whole map objects are equal. The committed snapshot does not yet carry terrain
or map metadata; that publication is the separate Step 7 contract.

Step 6 verification on 2026-09-10 was advisory Mac/arm64-hosted Docker Linux/amd64:
425/425 selected unit/fixture cases passed on each required lane, including unchanged committed
replay expectations, and 55 fixed fuzz-corpus executions passed across seven harnesses. The
diagnostic Debug benchmark retained all eight correctness records and delivery correctness;
no release/native performance claim follows. The initial migration's misplaced CSV comments
were relocated into `map.cfg` before the complete reruns; production CSV parsing was not widened.
The plan records the complete commands, review, input audit, and correction history.
