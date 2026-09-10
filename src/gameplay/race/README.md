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
