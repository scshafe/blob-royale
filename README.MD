# Blob Royale

Blob Royale is a deterministic multiplayer-simulation foundation with a read-only HTTP/WebSocket
presentation API and a React canvas client. The native service is modern C++20; the browser client
is strict TypeScript. Gameplay commands, identity, matchmaking, and persistence are deliberately
outside protocol v1 so they can be designed on top of a trustworthy core.

The supported release platform is the digest-pinned Ubuntu 24.04 `linux/amd64` image in
`ci/linux/Dockerfile`. GCC is the primary compiler and Clang supplies an independent build plus
sanitizer coverage. Native macOS builds and Docker-on-macOS results are useful diagnostics, but they
are never release evidence and must not change Linux behavior.

## Architecture

The production dependency direction is intentionally one-way:

```text
blob-royale       -> blob_application
blob_application -> blob_server, blob_runtime, blob_simulation, blob_observability
blob_server      -> blob_runtime, blob_protocol, blob_observability
blob_runtime     -> blob_simulation
blob_protocol    -> blob_simulation
```

`GameSimulation` is the sole deterministic world mutator. `SimulationRuntime` owns its one worker
thread and atomically publishes retained, immutable snapshots. The server receives only a
`const SnapshotPublication&`, so transport code cannot mutate or control the simulation.
`BlobRoyaleApplication` is the composition and process-lifecycle root; `main.cpp` only parses input,
constructs the application, and translates terminal errors.

The accepted decisions and wire contract live in `docs/architecture` and `docs/protocol/v1.md`.

## Verify

Docker is the only host prerequisite for the canonical checks. From the repository root:

```sh
./scripts/verify-linux pr
```

That command builds the pinned Linux image and performs clean GCC and Clang builds, all registered
C++ tests, ASan/UBSan and TSan execution, a real-process shutdown smoke test, and the complete web
pipeline. The `nightly` and `release` profiles are intentionally no weaker:

```sh
./scripts/verify-linux nightly
./scripts/verify-linux release
```

Only a native Linux/x86_64 Docker client running against a native Linux/x86_64 Docker daemon is
authoritative. The gate records both identities, so a remote context or an arm64 daemon using amd64
emulation remains advisory even when its client runs on Linux/x86_64. On Apple Silicon, amd64
emulation may be unable to execute sanitizer binaries under the available memory limit; that is an
environment failure, not permission to skip the gate. Every source stamp, release layout, evidence
manifest, and publication record durably says `authoritative` or `advisory`; an advisory execution
environment cannot move a release-profile current pointer.

The gate captures its source identity before any check, then rejects changes before certifying the
post-check server and web artifacts. The host verifies that deterministic stamp, stages immutable
server and web deployables, builds and saves the final runtime image, and
scans that image archive plus the production npm graph. High or critical shipped-dependency
findings fail every profile. The web SBOM is deterministically rendered from npm's complete
production graph and enriched from the committed lockfile; build-only packages are excluded. For a
standalone release-tier invocation use:

```sh
./scripts/build-release-linux
```

This delegates to `./scripts/verify-linux release`, so it cannot build from stale or unverified
outputs. It produces `blob-royale:linux-amd64-local` and records its immutable content ID in the
external evidence manifest. The native image contains only the root-owned, non-writable server,
example inputs, notices, licenses, and its pinned runtime userspace; it runs as numeric user 65532.

`out/release/current` is the single atomic commit point. It selects one immutable publication record
that binds a release layout, dependency evidence, runtime image content ID, and verification
authority. `out/release/blob-royale` and `out/security/current` are stable aliases through that one
pointer, so they cannot select different releases. The one-time conversion of legacy aliases keeps
recovery copies but is permitted only with an explicit readers-stopped maintenance acknowledgement;
without it, publication fails before changing either path. The layout contains separate `server/`
and `web/` deployables. Deploy `web/` on the HTTPS reverse proxy and route its same-origin `/api`
requests to the private server container. Browser files are intentionally absent from the native
image; the matching Docker archive, CycloneDX documents, vulnerability reports, npm audit, and
content-ID manifest remain external.

For a focused clean native build inside the same image:

```sh
./scripts/run-linux-toolchain -- ./scripts/verify-native-build
```

Record a deterministic advisory performance report with `./scripts/run-benchmarks-linux`. Only a
dedicated native Linux runner is suitable for accepting future regression budgets.

## Run the server

The process runs in the foreground and requires an explicit configuration path:

```sh
out/build/linux-gcc-debug/blob-royale --config config/blob-royale.cfg
```

`--scenario` is optional and seeds extra entities on top of the map:

```sh
out/build/linux-gcc-debug/blob-royale \
  --config config/blob-royale.cfg \
  --scenario tests/fixtures/player-on-wall-collision-test.csv
```

The configuration's `[match]` section names the game, the map, and the bots, and the map directory
it names must exist relative to the process working directory -- which for the command above is the
repository root, where `maps/` lives. Use `--help` for the exact invocation. The checked-in
development configuration listens on `127.0.0.1:8000` and plays `royale` on `maps/arena-960x640`
with no bots. Send `SIGINT` or `SIGTERM` for bounded graceful shutdown; normal signal-driven
shutdown exits zero. Startup and runtime failures produce one-line structured JSON diagnostics and
exit nonzero.

Scenario CSV files have one required header and exactly seven columns per player:

```text
entity_id,position_x_world_units,position_y_world_units,velocity_x_world_units_per_second,velocity_y_world_units_per_second,acceleration_x_world_units_per_second_squared,acceleration_y_world_units_per_second_squared
```

Configuration and scenario input is strict: missing, duplicate, unknown, malformed, non-finite, or
unsafe values fail before any runtime object starts.

## Run the browser client

With the server listening on port 8000:

```sh
cd frontend-react
npm ci
npm run dev
```

Vite binds loopback and proxies same-origin `/api` HTTP and WebSocket traffic to the server.
Browsers always send an `Origin` header on WebSocket upgrades, so the checked-in configuration
allowlists both Vite development origins in `allowed_origins`; a deployment must list its exact
external origin the same way. The production bundle is produced with `npm run build`;
`./scripts/verify-web` is the canonical Linux client gate.

The client starts in **Follow player**, keeping your current blob centred on a movable view of
the map. Choose **Manual view** to drag the map or use its pan buttons independently, then
**Follow player** to resume tracking. Click the arena, point toward where you want to accelerate,
and hold **Space** to go; release Space to coast. Pointer distance does not change thrust strength,
and WASD/arrows are no longer movement bindings. Larger worlds
extend beyond the viewport instead of being shrunk to fit it. These camera controls are part of
the local follow-up implementation; the recorded `b1e80d4` tailnet deployment predates them.

The room's movement panel adjusts acceleration and normal top speed for everyone through explicit
Apply/Reset actions. If someone changes the room while you are editing, your draft is retained and
requires review before Apply. Only a server-confirmed result says applied; interruption remains
unknown even after current values are reviewed.

## Repository map

- `src/simulation` — validated values, world ownership, the deterministic tick kernel, snapshots
- `src/gameplay` — the games: each mode's declared systems, contact rules, policies, and objective
- `src/controllers` — the in-process agents that decide, through the same command sink as a browser
- `src/observability` — injected, bounded, atomic structured JSON-lines events
- `src/runtime` — single-writer clock lifecycle, the command path, and snapshot publication
- `src/protocol` — the only Boost.JSON boundary; protocol v1 and v3 encoders and the command decoder
- `src/server` — bounded Beast HTTP/WebSocket transport and trust-boundary enforcement
- `src/application` — strict CLI/INI/CSV input and the composition root
- `maps` — arenas as data: `map.cfg`, `static_bodies.csv`, and `markers.csv` per directory
- `frontend-react` — strict TypeScript transport boundary and presentational UI
- `tests` — unit, real-process integration, replay fixtures, and browser specifications
- `benchmarks` — deterministic Linux simulation and delivery measurements
- `docs/operations/linux.md` — deployment, shutdown, diagnostics, and incident guidance
- `docs/operations/tailnet.md` — deploying and operating the tailnet game host `cole-ubuntu-pc`
- `deploy/ubuntu-pc` — committed configuration for that host

Commands arrive over protocol v3 (`docs/protocol/v3.md`) on its own versioned route, `GET
/api/v3/lobbies/<lobby_id>/session`, and never over v1's read-only snapshot stream. Network code still holds only
`CommandSink&` and `const SnapshotPublication&`; a further command protocol must do the same again
on a new versioned route rather than widening either capability.

## Legal status

No license has been granted for Blob Royale project code. It remains all rights reserved unless the
owner explicitly publishes a license. Third-party components retain their own licenses; see
`THIRD_PARTY_NOTICES.md`.
