# circuit-960x640

The shipped point-to-point race course from ADR 0007's Race section. It uses the accepted
960 by 640 world, the proposed `[race] track_half_width_world_units=70`, and
`checkpoint_radius_world_units=40`. Width and gate radius belong to the configuration;
the map files carry their centres. Set `[match] mode=race`, `map=circuit-960x640`, and
use a lobby of at most four seats when selecting this course.

| File | Contents |
|---|---|
| `map.cfg` | Directory-matching name, display name, and world bounds. |
| `markers.csv` | Three centreline nodes, three ordered gates, and four starting-grid positions. |
| `static_bodies.csv` | Two static obstacles on the players' collision layer. |

Every coordinate is a literal derived below. Let `W=960`, `H=640`, the half-width `h=70`,
and gate radius `r=40`. All markers are unaligned, so their `team_id` column is empty.

The centreline starts at `(W/6, 3H/4) = (160,480)`, turns at
`(3W/4, 3H/4) = (720,480)`, and ends at `(3W/4, H/4) = (720,160)`.
Its legs are 560 and 320 world units long, with one right-angle bend and no closing segment.
The first gate is `(3W/8, 3H/4) = (360,480)`, the second is the bend `(720,480)`,
and the last, the finish, is `(3W/4, 5H/16) = (720,200)`. Each centre lies on the
centreline. The finish is 40 units before its endpoint. The corridor's nearest wall clearance
is 90 units: the rightmost edge is `720+70=790`, and its bottom edge is `480+70=550`.

The grid uses two columns, `W/6=160` and `W/6+h=230`, and two rows,
`3H/4-h/2=445` and `3H/4+h/2=515`. Their CSV order is `(160,445)`, `(160,515)`,
`(230,445)`, `(230,515)`. Every centre is 35 units from the centreline; nearest neighbours
are 70 units apart, comfortably beyond the 20-unit contact distance of the configured
10-unit player radius. Four markers give the lobby its four-seat ceiling.

The first obstacle is halfway across the world and `h-r=30` units above the first leg:
`(W/2, 3H/4-(h-r)) = (480,450)`. The second is 30 units right of the second leg and
halfway between its two gates: `(3W/4+(h-r), (480+200)/2) = (750,340)`.
Both use collision layer `1` and mask `1`, the same bit as ordinary spawned players, so the
built-in static contact rule reflects a racer that hits them. Static-body radius is not a
CSV field: world seating assigns the configured player radius, as for every map.
With a 10-unit radius, each obstacle fits inside the 70-unit corridor and leaves the exact
centreline clear.

The production-loader fixture verifies these literals and the race's map requirements.
The separate `race-*` replay fixtures isolate ordered gates, a bend, return timing, shared
finishes, and clock decisions with independently derived tick expectations. They do not
depend on browser rendering or a bot, which belong to the next phase.
