# Race

This domain owns the point-to-point race defined in ADR 0007. `RaceConfiguration` validates the
required `[race]` section and converts authored durations to ticks. `RaceCourse::create` binds
the configured `road` name to one terrain corridor and projects ordered `checkpoint` markers.
Terrain owns centreline nodes and width; race validates gate radius and checkpoint/spawn centres
against the selected road. Missing road names raise `GAMEPLAY.RACE_MAP_ROAD_MISSING`.

`RaceCourse` exposes the selected road name, centreline, checkpoints, corridor half-width, gate radius, and the
`distance_to_centreline` view of the canonical simulation corridor query. The last checkpoint is
the finish. Checkpoint and spawn admission retains exact distance <= half-width, independently
of terrain holes or the support query's tolerance. Terrain validates consecutive nodes and shape
limits before binding; unrelated marker kinds are ignored.

The domain depends on simulation map and vector values, the shared duration and steering
validation rules, and the `GAMEPLAY.RACE_*` error vocabulary. Geometry and balance validation
happen before a match ticks.

`RaceMode::validate_map` binds the course during simulation setup, before `systems` reads it;
`GameSimulation::create` already calls those declarations in that order. The mode then hands
immutable course values to its systems and is destroyed before any tick. Each course retains
shared immutable terrain storage plus a stable corridor index, so temporary map destruction or
copying/moving a course cannot leave its centreline dangling. Calling
`systems` before successful validation throws `GAMEPLAY.RACE_COURSE_UNBOUND`; failed validation
clears any prior binding.

The declared order is `course_publisher` then `thrust_steering` at `kPreKernel`,
`checkpoint_progress` then shared `status` at `kPostKernel`, and `standings_recorder`,
`checkpoint_respawn`, shared `respawn`, `match_reset`, `lifetime_expiry`, `hazard_spawn` at
`kLifecycle`. The motion declaration owns shared running-only support loss and ordered gates.
Both use the canonical solver queries. Progress attaches at zero even before the first gate,
then consumes certified RaceCheckpointEvent facts; several gates can advance in a tick.
Support loss terminates immediately and wins exact ties with finish. Earlier gates remain earned
after later falling. Finishing zeros motion, terminates the quantum, and releases input intents.
The shared input lock recognizes completed progress; first PreKernel publication establishes its
course facts even for a directly seeded completed racer. TrackBoundsSystem is deleted.

`GridSpawnPolicy` uses the shared next-free policy between matches and allows only a timer-free
`RaceProgress{0}` racer to return to the grid while running. `checkpoint_respawn` uses the engine's
supported/actual-radius occupancy predicate and at-rest seating for return to the last gate. It runs
before timer expiry so both return routes seat on tick `N + D + 1` after elimination on `N` with
delay `D`. An occupied gate postpones that return one tick at a time. Application startup rejects
checkpoint return discs that cannot be terrain-supported for the configured player radius; selected
road binding alone remains intentionally independent of this cross-value clearance rule.

`standings_recorder` owns the observed `standings` member of `RaceModeState`, retaining controller
identity and sharing placements only among equal certified tick-and-offset times. It clears on the first running
tick of a new match. `course_publisher` stamps the validated selected `road` identity, gates, and
clocks in every phase without changing standings. Centreline and width live only in terrain;
the temporary Step 7 wire mirror is removed. The shared race-state initializer takes the bound
course, with no empty/default road name. Ordinary tick-zero snapshots retain `NoModeState`.
Standings publish normalized `finished_tick_offset` in [0,1] alongside the tick; compare the pair,
never its binary64 sum. Coincident very large courses may exhaust the existing motion work cap;
the whole tick fails atomically rather than crediting a partial finish.
`RaceObjective` uses the shared lobby predicate, decides
by placement 1 once everyone finishes or the finish window expires, and otherwise uses gate-count
ranking at the time limit. A solo racer remains a time trial. No race rule writes royale's
elimination placements.
