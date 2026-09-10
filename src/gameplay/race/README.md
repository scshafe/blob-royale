# Race

This domain owns the point-to-point race defined in ADR 0007. `RaceConfiguration` validates the
required `[race]` section and converts authored durations to ticks. `RaceCourse::create` projects
the map's ordered `track` and `checkpoint` markers and validates its track, gates, and starting
grid against that configuration.

`RaceCourse` exposes the centreline, checkpoints, corridor half-width, gate radius, and the
canonical `distance_to_centreline` calculation. The last checkpoint is the finish. Checkpoint
centres and spawn centres may lie anywhere in the closed corridor; only consecutive track nodes
are forbidden from coinciding. Other marker kinds are ignored.

The domain depends on simulation map and vector values, the shared duration and steering
validation rules, and the `GAMEPLAY.RACE_*` error vocabulary. Geometry and balance validation
happen before a match ticks.

`RaceMode::validate_map` binds the course during simulation setup, before `systems` reads it;
`GameSimulation::create` already calls those declarations in that order. The mode then hands
independently owned, immutable copies to its systems and is destroyed before any tick. Calling
`systems` before successful validation throws `GAMEPLAY.RACE_COURSE_UNBOUND`; failed validation
clears any prior binding.

The declared order is `thrust_steering` at `kPreKernel`, `checkpoint_progress` then `track_bounds`
at `kPostKernel`, and `standings_recorder`, `checkpoint_respawn`, shared `respawn`, `match_reset`,
`lifetime_expiry`, `hazard_spawn`, then `course_publisher` at `kLifecycle`. Progress attaches to
every alive racer while running and advances at most one gate per tick. Bounds emits eliminations
for centres outside the corridor; shared respawn removes the body in that same tick.

`GridSpawnPolicy` uses the shared next-free policy between matches and allows only a timer-free
`RaceProgress{0}` racer to return to the grid while running. `checkpoint_respawn` uses the engine's
occupancy predicate and at-rest seating for a racer returning to its last completed gate. It runs
before timer expiry so both return routes seat on tick `N + D + 1` after elimination on `N` with
delay `D`. An occupied gate postpones that return one tick at a time.

`standings_recorder` owns the observed `standings` member of `RaceModeState`, retaining controller
identity and sharing placements among same-tick finishers. It clears only on the first running
tick of a new match. `course_publisher` owns the declared geometry and clocks and stamps them in
every phase without changing standings. `RaceObjective` uses the shared lobby predicate, decides
by placement 1 once everyone finishes or the finish window expires, and otherwise uses gate-count
ranking at the time limit. A solo racer remains a time trial. No race rule writes royale's
elimination placements.
