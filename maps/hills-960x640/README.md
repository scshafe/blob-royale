# hills-960x640

The shipped king-of-the-hill arena (`docs/architecture/0007-king-of-the-hill-and-race-modes.md`
§ "King of the hill"). Its bounds and authored markers retain the historical 960x640 layout.
Live body motion uses the Step 16 continuous solver; preserving the layout does not preserve the
old discrete kernel's contact or wall chronology.

| File | Contents |
|---|---|
| `map.cfg` | Name, display name, arena bounds, and explicit solid-ground terrain. |
| `markers.csv` | 8 `spawn` markers on a ring and 4 `hill` markers on a diamond, authored from the formulas below. |
| `static_bodies.csv` | Header only: this arena has no obstacles. Add a row to add one. |

Columns are `position_x_world_units,position_y_world_units,collision_layer,collision_mask,contact_effect_policy` for
`static_bodies.csv` and `marker_kind,position_x_world_units,position_y_world_units,team_id` for
`markers.csv`; `team_id` is empty for an unaligned marker, which is what every marker here is.

Each static row explicitly selects `closing_impact` or `any_touch`; missing policy columns and
the old header are rejected. Any-touch policy admits the source object's gameplay effects without
granting a non-closing physical impulse. Static CSV authoring still has no radius column:
`GameWorld` normalizes map-object radii to the configured player radius, which the live solver
then uses as their effective radius. Static centers must be inside closed bounds and on supported
terrain; spawn clearance remains a separate full-disc placement rule.

**The spawn ring is every fourth slot of `arena-960x640`'s.** Slot `k` of that ring sits at
`center + 232.5 * (cos(2*pi*k/32), sin(2*pi*k/32))`; this map takes `k = 0, 4, 8, ..., 28`, so
eight players spawn 45 degrees apart at a chord of about 178 wu, and every literal is one the
arena already carries. Eight rather than thirty-two because a hill match is an open field --
joiners are seated in any phase -- and eight seats is the most `require_lobby_fits_map` will admit
here, which is a deliberate ceiling for a scoring game on one hill.

**Marker-tour motion follows a diamond.** The four `hill` markers, in the order the tour visits them, are
`(480, 200)`, `(700, 320)`, `(480, 440)`, and `(260, 320)`: 120 wu above, 220 wu right of, 120 wu
below, and 220 wu left of the arena's centre. Each sits at least 90 wu from every wall, so a hill
of the proposed `hill_radius_world_units=90` is whole at every stop; the second stop overlaps the
first spawn point by design, so the player seated there begins on the hill when the tour reaches
it. The glide between stops is `hill_geometry.hpp`'s straight line, so the tour is a diamond with
straight edges, not a circle.

Under configured `random_roam`, the non-physical hill instead uses its deterministic velocity and
retarget schedule. It may cross interior cliffs/holes; only outward position change and velocity
at the outer boundary are canceled, permitting edge sliding or rest without bouncing. Player
falling/support trigger registration remains Step 17, not an effect of this map's markers.

The markers are literals rather than arithmetic so the map is data a person can read and edit, and
so no trigonometry runs at load time. An obstacle is a row in `static_bodies.csv` and no C++ at
all.
