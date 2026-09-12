# arena-960x640

The shipped arena. Its 960x640 bounds and authored spawn positions retain the historical fixture
layout. Live motion uses the Step 16 continuous solver; identical map geometry does not promise
the old discrete kernel's contact or wall chronology.

| File | Contents |
|---|---|
| `map.cfg` | Name, display name, arena bounds, and explicit solid-ground terrain. |
| `markers.csv` | 32 `spawn` markers on a ring, authored from the formula below. |
| `static_bodies.csv` | Header only: this arena has no obstacles. Add a row to add one. |

Columns are `position_x_world_units,position_y_world_units,collision_layer,collision_mask,contact_effect_policy` for
`static_bodies.csv` and `marker_kind,position_x_world_units,position_y_world_units,team_id` for
`markers.csv`; `team_id` is empty for an unaligned marker, which is what every `spawn` marker is.

Every static row requires `contact_effect_policy` to be exactly `closing_impact` or `any_touch`;
the old header and a missing policy column are rejected. `any_touch` admits the object's gameplay
effects at a certified graze or stationary/separating touch; it does not grant a physical impulse.
The body-bound `ContactEffectAdmission` component stores only that nondefault. Closing impact is
represented by absence, and remains the explicit choice in existing authored content.

A static CSV body still declares **no radius**. `GameWorld` preserves configured-radius
normalization when seating map objects. The live solver does consult effective body radii for pair
geometry and walls, including differently sized dynamic hazards; this authoring migration adds
no radius column. A static center must lie inside the closed arena and on supported terrain, while
spawn clearance separately checks the full configured-radius player disc.

The ring is `docs/architecture/0005-royale-mode.md` § "Spawning": slot `k` sits at
`center + 0.75 * (min(width, height) / 2 - player_radius) * (cos(2*pi*k/32), sin(2*pi*k/32))`, with
slot 0 on `+x` and the index increasing counter-clockwise. For this arena and a 10 wu player radius
that is a radius of 232.5 wu and an adjacent-slot chord of about 45.58 wu, comfortably clear of the
20 wu two-player contact distance. The markers are literals rather than arithmetic so the map is
data a person can read and edit, and so no trigonometry runs at load time.

An obstacle is a row in `static_bodies.csv` and no C++ at all.
