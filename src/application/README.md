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
by the mode that owns it.

`--scenario` is optional. `[match]` and the map it names describe a whole match; a scenario only
seeds extra entities on top of the map's static content, which is what fixtures need and a live
deployment does not.

`match_startup_validation.hpp` holds the rules that span two independently validated values: the
worst-case published population against protocol v2's 1,024-entity snapshot bound, and the map's
arena against the `[world]` scalars protocol v1 publishes. Neither value can own its rule, and both
would otherwise only fail once a match was being played.

`BlobRoyaleApplication` owns immutable configuration, then `SimulationRuntime`, then
`ControllerHost`, then `GameServer` in destruction-safe order. It is the only file that knows every
registry: it resolves the mode, hands the simulation the map and the mode, opens one `CommandSink`
session per configured bot, and drives the host one decision pass per presentation frame on its own
control thread. `blob_controllers` and `blob_runtime` link no logger by contract, so this is also
where a rising dropped-command count, a controller failure, and a refused bot submission become
structured log lines. It installs process signal handling on the caller thread, starts the runtime,
runs the server on its owned `std::jthread`, and coordinates idempotent shutdown. Network acceptance
and sessions stop before the runtime is joined. `SIGINT` and `SIGTERM` are normal successful exits;
initialization, worker, encoding, or server failure is retained and rethrown to `main()`.

Future orchestration belongs here only when it coordinates existing domain capabilities. Domain
rules stay in simulation, wire representation stays in protocol, and transport policy stays in
server. In particular, never inject `GameSimulation&` or `SimulationRuntime&` into network code.
