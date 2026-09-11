<!-- canonical: application_domain -- validated startup and process composition -->

# Application domain

`blob_application` is the composition and process-lifecycle boundary. It exists as a library so the
same production translation units are exercised by tests; the executable adds only `main.cpp`.

`ApplicationConfigLoader` is the sole CLI and INI parser. `ScenarioLoader` is the sole scenario CSV
parser. `MapLoader` is the sole map-directory parser. All three create validated domain values
before `BlobRoyaleApplication` is constructed and throw typed errors with stable codes, safe
context, and actionable detail. There are no legacy aliases, ambient defaults, or partially accepted
documents.

`MatchConfiguration` is the validated `[match]` section -- which game, on which map, from which maps
directory, under which seed, with which bots. It is the value that resolves `mode` through
`gameplay::GameModeRegistry` and each bot kind through `controllers::ControllerRegistry`, so a
misspelled name is a startup rejection rather than a match that quietly plays the wrong game.
`gameplay::GameModeConfiguration` carries each configured mode's own `[<mode>]` section, validated
by the mode that owns it, plus the hazard table every declared `[hazard.<kind>]` section builds.

`--scenario` is optional. `[match]` and the map it names describe a whole match; a scenario only
seeds extra entities on top of the map's static content, which is what fixtures need and a live
deployment does not.

**One thing in the configuration schema is open, and exactly one: a section family.** A family is a
declared section-name prefix (`hazard`) whose *instance* names are open (`[hazard.comet]`), whose key
schema is closed and shared by every instance, and whose instances collect into a list rather than
into a fixed field. It exists so that adding a hazard kind is one configuration section and no C++ at
all, which is what `gameplay::HazardArchetype` is the validated form of. Everything around it stayed
fail-closed: a section matching neither a fixed name nor a declared prefix is still rejected, an
unknown key inside an instance is rejected by the same lookup `[royale]` uses, a repeated instance
name is a duplicate section, and an instance that omits one of its family's keys is a missing key.
Zero instances is legal and is what every configuration in this tree looked like before hazards. The
instance-name *grammar* belongs to the value that publishes the name, exactly as `[match] mode` does:
the loader refuses only an empty instance name, and `HazardArchetype::create` refuses one outside
`common.schema.json#/$defs/kind_name`. The obvious second customer is a per-bot roster
(`[bot.wanderer]`), which costs one prefix, one enumerator, and its keys — no new structure.

Map authoring uses the same section-family convention in `MapLoader`'s private strict INI reader.
Every `map.cfg` must declare `[terrain] ground=solid` or `ground=corridors`; an older file with no
terrain declaration is rejected, not defaulted. `[terrain.corridor.<name>]` requires
`half_width_world_units` and `points_world_units=x,y;x,y;...`; `[terrain.hole.<name>]` requires
`center_x_world_units`, `center_y_world_units`, and `radius_world_units`. Names are open, but each
family's keys are closed. All fields in a declared instance are required, section and key duplicates
are rejected, and point lists permit whitespace but no empty pairs or trailing semicolon. Geometry,
snake_case names, per-family uniqueness, and shape counts belong to simulation's terrain factories.
Solid ground forbids corridor declarations; corridor ground requires at least one. Holes may be
declared for either ground type. The fixed map name/display name/bounds and the two CSV files remain
required. See `maps/circuit-960x640/map.cfg` for an authored corridor.

`match_startup_validation.hpp` holds the rules that span two independently validated values: the
worst-case published population against protocol v3's 1,024-entity snapshot bound, the map's
arena against the `[world]` scalars protocol v1 publishes, and the map's `spawn` markers against
`[match] lobby_seat_count` for a mode that has a lobby. No one value can own its rule, and each
would otherwise only fail once a match was being played. `LobbiesConfiguration` is the validated
`[lobbies]` section -- how many rooms the process runs, bounded by the protocol's directory limit.

`BlobRoyaleApplication` owns immutable configuration, then `[lobbies] count` `Room`s, then the
`LobbyDirectory` the server reads them through, then `GameServer`, in destruction-safe order. A
`Room` is the single-match server this process used to be -- its `SimulationRuntime` on its own
thread, its `ControllerHost`, its `SeatBotReconciler`, and the `MatchSessionContext` its sessions
run on -- numbered `1..N` and seeded `seed + (lobby_id - 1)`; room 1 plays the world the map and
any scenario produced, and every further room plays the map alone, which is why the loader refuses
a scenario with more than one room. The application is the only file that knows every registry: it
resolves the mode once per room, hands each simulation the map and the mode, declares the `[match]
bots` roster into the first seats of a mode that has a lobby -- or opens one `CommandSink` session
per configured bot for a mode that has none -- and drives every room from one control loop on the
caller's thread: one decision pass per presentation frame for each host, and on every 25 ms poll
each room's dropped commands, overruns and re-bases, phase changes (`match.phase_changed`), and bot
reconciliation, every line carrying the room's `lobby_id`. `SeatBotReconciler` makes the live bots
match the committed seats: one bot for every declared seat nobody holds, joined to exactly that
seat, and none for a seat that was cleared, resized away, or taken by a person. **A room nobody is
in has no bots**: when a room's session count is zero while its match is in `countdown` or
`running`, the loop tells the reconciliation the room is abandoned, its bots leave, the match ends
by attrition, and the machine walks back to `lobby`, where the bots are reseated. **A room that
fails does not stop the process**: the loop logs `runtime.failed` once with the exception, the
room's sessions close themselves because its publication is not ready, the other rooms keep
serving, readiness reports room 1, and the failure is rethrown at shutdown. `blob_controllers` and `blob_runtime` link no logger by contract, so this is also
where a rising dropped-command count, a controller failure, and a refused bot submission become
structured log lines. It installs process signal handling on the caller thread, starts the runtime,
runs the server on its owned `std::jthread`, and coordinates idempotent shutdown. Network acceptance
and sessions stop before the runtime is joined. `SIGINT` and `SIGTERM` are normal successful exits;
initialization, worker, encoding, or server failure is retained and rethrown to `main()`.

Future orchestration belongs here only when it coordinates existing domain capabilities. Domain
rules stay in simulation, wire representation stays in protocol, and transport policy stays in
server. In particular, never inject `GameSimulation&` or `SimulationRuntime&` into network code.
