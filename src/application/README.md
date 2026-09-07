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
