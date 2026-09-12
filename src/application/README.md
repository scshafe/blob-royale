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
by the mode that owns it, plus the hazard table every declared `[hazard.<kind>]` section builds
and simulation's shared `MovementTuning` value.

One required `[movement]` section declares `acceleration_world_units_per_second_squared`
(0..10,000) and `normal_top_speed_world_units_per_second` (1..10,000). Both must be finite;
the three old per-mode thrust keys are rejected, with no alias or fallback. Production defaults
are 400/600. The composition root seeds each room's current pair and reset defaults before its
first snapshot, including Sandbox; revision and effective tick start at zero. Live room changes
never mutate process configuration or write the INI file. Round resets retain the active pair;
room recreation reseeds from authored values. Sandbox has no seated-tuning authority exception.

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
`common.schema.json#/$defs/kind_name`. Tactical profiles are the second customer:
`[bot_profile.<name>]` uses the same parser, with four required controller-owned values.

`TacticalProfileCatalogue` retains up to 16 unique profiles in declaration order. Each requires
`objective_seek_probability` (finite 0..1), `reaction_delay_ticks` (integer 0..4000), `aim_error`
(finite 0..0.25), and `target_persistence_ticks` (integer 0..4000). Seeking chooses go versus coast;
it never scales acceleration. Aim error is a perpendicular/forward ratio, not degrees. No implicit
profile or inactive combat setting is accepted. Roster terms are `tactical@<profile>:<count>`;
plain `kind:count` keeps its meaning. Profiled choices are supported in hill, race, and royale,
but rejected at Sandbox startup because that mode has no stable authored-seat identity.

The composition root derives one `NpcCatalogue` from registry metadata and these validated profile
values. The same value supplies runtime admission and the session's plain/profile projections.
Missing, unknown, and mismatched selections fail before a bot is constructed. Profile values stay
server-side; the browser names a published choice rather than sending numeric settings.

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
run on (including the room-bound tuning-result claim capability) -- numbered `1..N` and seeded
`seed + (lobby_id - 1)`; room 1 plays the world the map and
any scenario produced, and every further room plays the map alone, which is why the loader refuses
a scenario with more than one room. The application is the only file that knows every registry: it
resolves the mode once per room, hands each simulation the map and the mode, declares the `[match]
bots` roster into the first seats of a mode that has a lobby -- or opens one `CommandSink` session
per configured bot for a mode that has none -- and drives every room from one control loop on the
caller's thread: one decision pass per presentation frame for each host, and on every 25 ms poll
each room's dropped commands, overruns and re-bases, phase changes (`match.phase_changed`), and bot
reconciliation, every line carrying the room's `lobby_id`. `SeatBotReconciler` makes the live bots
match the committed seats: one bot for every declared seat nobody holds, joined to exactly that
seat, and none for a seat that was cleared, resized away, or taken by a person. Hosted ownership,
pending joins, and failed-creation caching use the full kind/profile declaration. Replaced
declarations retire immediately, and queued bot joins carry an exact-declaration guard so they
cannot fill a replacement. Unguarded human and literal replay joins keep their previous semantics.
Legacy diagnostic bots retain their room seed. Tactical bots use raw configured match seed,
lobby ID, authored seat index, profile name, and observed running-start tick; controller allocation
and catalogue order do not enter their seed. **A room nobody is
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

Step 18 adds one required shared `[abilities]` section, validated by `gameplay::AbilityConfiguration`
through the same duration owner: `shield_duration_seconds` (0.4), `shield_perfect_window_seconds`
(0.08), `shield_cooldown_seconds` (0.9), and `parry_stun_duration_seconds` (0.6), all required and
all finite. Those production values are the ADR's initial tuning assumptions, not owner-selected
balance. Rounded shield, perfect and parry-stun durations must be positive and the perfect window
may not exceed the shield; a zero cooldown, or one shorter than the shield, is deliberately legal.
`GameModeConfiguration` carries the validated value to every mode beside `movement` and `hazards`.
Because the loader requires every declared section regardless of `[match] mode`, this section
reaches every standalone `.cfg`, every inline unit-test configuration string, and
`tests/integration/server_process_fixture.cpp::write_fixture_inputs`. Replay fixtures are the
documented exception and gain nothing: their parser rejects unread keys and never reads
`[abilities]`, so a replay inherits `GameModeConfiguration::defaults()`, exactly as it does for
`[sandbox]`.

Step 19 adds three more required keys to that same section, and they are the first values in it
that are not durations: `charge_cooldown_seconds` (1.2), `charge_speed_fraction` (0.75), and
`charge_safety_envelope_speed` (20000). The cooldown goes through the shared duration owner like the
other four and is validated **strictly positive** — unlike `shield_cooldown_seconds`, whose zero is
legal only because shield admission is gated a second time by the end of its own protection.
`charge_speed_fraction` is a dimensionless multiple of the *current* normal ceiling, not a speed;
`charge_safety_envelope_speed` is a speed in `wu/s`, bounded above by the simulation's physical
component domain. Both are validated finite and strictly positive, and one cross-key rule ties them
together so that a charge from rest stays admissible at any tuned ceiling. As with the shield's
four, these are ADR 0008's initial tuning assumptions plus one engineering guard, not
owner-selected balance, and the envelope's number in particular is a first number the owner has
never chosen. Because the loader still requires every declared section regardless of `[match] mode`,
adding three keys migrated every standalone `.cfg`, every inline unit-test configuration string, and
`write_fixture_inputs` again; the replay exception is unchanged, and the frozen deployment snapshot
gained a fifth provenance entry, which will be the one retained benchmark field that differs from
Step 18's baseline.

Step 17 adds required `[sandbox] respawn_delay_seconds`, validated by the shared gameplay duration
owner. Match startup binds race checkpoint return clearance against the configured player radius
and complete terrain before building any room; unsupported discs fail with
`APPLICATION.MATCH.RACE_CHECKPOINT_UNSUPPORTED`. This is a cross-value validation exception to
mode-name isolation, not another game-mode factory. Runtime occupancy remains shared seating's job.
