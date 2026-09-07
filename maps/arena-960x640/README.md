# arena-960x640

The shipped arena. Its bounds reproduce the world the accepted fixtures and the deployed read-only
build already used, so playing a mode on this map changes no committed physics.

| File | Contents |
|---|---|
| `map.cfg` | Name, display name, and arena bounds. |
| `markers.csv` | 32 `spawn` markers on a ring, authored from the formula below. |
| `static_bodies.csv` | Header only: this arena has no obstacles. Add a row to add one. |

The ring is `docs/architecture/0005-royale-mode.md` § "Spawning": slot `k` sits at
`center + 0.75 * (min(width, height) / 2 - player_radius) * (cos(2*pi*k/32), sin(2*pi*k/32))`, with
slot 0 on `+x` and the index increasing counter-clockwise. For this arena and a 10 wu player radius
that is a radius of 232.5 wu and an adjacent-slot chord of about 45.58 wu, comfortably clear of the
20 wu two-player contact distance. The markers are literals rather than arithmetic so the map is
data a person can read and edit, and so no trigonometry runs at load time.

An obstacle is a row in `static_bodies.csv` and no C++ at all.
