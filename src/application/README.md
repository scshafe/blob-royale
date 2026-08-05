<!-- canonical: application_domain -- validated startup and process composition -->

# Application domain

`blob_application` is the composition and process-lifecycle boundary. It exists as a library so the
same production translation units are exercised by tests; the executable adds only `main.cpp`.

`ApplicationConfigLoader` is the sole CLI and INI parser. `ScenarioLoader` is the sole scenario CSV
parser. Both create validated domain values before `BlobRoyaleApplication` is constructed and throw
typed errors with stable codes, safe context, and actionable detail. There are no legacy aliases,
ambient defaults, or partially accepted documents.

`BlobRoyaleApplication` owns immutable configuration, then `SimulationRuntime`, then `GameServer` in
destruction-safe order. It installs process signal handling on the caller thread, starts the runtime,
runs the server on its owned `std::jthread`, and coordinates idempotent shutdown. Network acceptance
and sessions stop before the runtime is joined. `SIGINT` and `SIGTERM` are normal successful exits;
initialization, worker, encoding, or server failure is retained and rethrown to `main()`.

Future orchestration belongs here only when it coordinates existing domain capabilities. Domain
rules stay in simulation, wire representation stays in protocol, and transport policy stays in
server. In particular, never inject `GameSimulation&` or `SimulationRuntime&` into network code.
