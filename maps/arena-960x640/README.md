# arena-960x640

The shipped arena. Its bounds reproduce the world the accepted fixtures and the deployed read-only
build already used, so playing a mode on this map changes no committed physics.

| File | Contents |
|---|---|
| `map.cfg` | Name, display name, and arena bounds. |
| `markers.csv` | 32 `spawn` markers on a ring, authored from the formula below. |
| `static_bodies.csv` | Header only: this arena has no obstacles. Add a row to add one. |

Columns are `position_x_world_units,position_y_world_units,collision_layer,collision_mask` for
`static_bodies.csv` and `marker_kind,position_x_world_units,position_y_world_units,team_id` for
`markers.csv`; `team_id` is empty for an unaligned marker, which is what every `spawn` marker is.

A static body declares **no radius**, and the column that once suggested otherwise was removed
before any obstacle was authored. No accepted phase reads `PhysicsBody::radius()` -- the pair
predicate, the wall fold, the spatial index, and the spawn occupancy test all measure with
`[world] player_radius_world_units` -- so an authored per-body radius would publish a size the
collision kernel does not use. `GameWorld` fills every seated body's radius in from the
configuration, so what a snapshot publishes is what the kernel measured. Differently sized bodies
arrive with the growing-blob physics amendment (`docs/architecture/0003-deterministic-simulation-contract.md`
§ "Justified extension points and what-if stress") and the column arrives with them.

The ring is `docs/architecture/0005-royale-mode.md` § "Spawning": slot `k` sits at
`center + 0.75 * (min(width, height) / 2 - player_radius) * (cos(2*pi*k/32), sin(2*pi*k/32))`, with
slot 0 on `+x` and the index increasing counter-clockwise. For this arena and a 10 wu player radius
that is a radius of 232.5 wu and an adjacent-slot chord of about 45.58 wu, comfortably clear of the
20 wu two-player contact distance. The markers are literals rather than arithmetic so the map is
data a person can read and edit, and so no trigonometry runs at load time.

An obstacle is a row in `static_bodies.csv` and no C++ at all.
