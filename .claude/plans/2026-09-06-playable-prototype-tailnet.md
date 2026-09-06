# Plan: Playable Blob Royale prototype on the tailnet

**Goal:** Two or more people on the tailnet open `https://cole-ubuntu-pc.colobus-stargazer.ts.net:8444`, each steers one blob, a shrinking safe zone eliminates blobs until one wins, and the server is the authoritative release image running on `cole-ubuntu-pc`.
**Out of scope:** Accounts or passwords (Tailscale is the perimeter), Funnel or any internet exposure, multiple rooms or matchmaking, persistence or leaderboards, client-side prediction or interpolation, growth or unequal-mass mechanics, parallel physics, retiring protocol v1, and any weakening of the Linux-authoritative gates.

## Context

The foundation is complete but has zero gameplay. Protocol v1 is read-only (`docs/protocol/v1.md`), the simulation has no input parameter (`src/simulation/game_simulation.hpp`), the roster is fixed from a CSV at startup (`src/application/scenario_loader.cpp`), and the client only renders (`frontend-react/src/features/simulation/SimulationCanvas.tsx`). The documented seam is `@extension-point simulation_input` in `docs/architecture/0002-simulation-architecture.md:88`, with commands explicitly excluded by `docs/architecture/0003-deterministic-simulation-contract.md:154`.

Local `main` is 19 commits ahead of `origin/main` and has never been pushed, so `.github/workflows/quality.yml` has never run. Nine steps in `.claude/plans/2026-08-04-feature-ready-foundation.md` are unchecked only because no native Linux/x86_64 host ever executed the gate. Every local stamp under `out/verification/linux/` says `verification_authority=advisory`.

Deployment host facts observed over `ssh ubuntu-tailscale` on 2026-09-06: `cole-ubuntu-pc`, Ubuntu 24.04.4 LTS, x86_64 Ryzen 5 5600G, 12 CPUs, 125 GiB RAM, Docker 29.6.1 native linux/x86_64, user `cole` in the `docker` group with passwordless sudo, Tailscale 1.102.2, MagicDNS `cole-ubuntu-pc.colobus-stargazer.ts.net`. Because both the Docker client and daemon are native Linux/x86_64, `./scripts/verify-linux release` on that host is **authoritative** under ADR 0001 and `scripts/publish-release-linux:175`. HTTPS ports 443 and 8443 are already claimed by other `tailscale serve` handlers on that host, so Blob Royale uses `--https=8444`. Port 8000 is free. No repository clone exists there yet.

`tailscale serve` facts from the Tailscale source (`ipn/ipnlocal/serve.go`): the mount prefix is stripped with `http.StripPrefix`, so the backend target must carry `/api`; the client `Host` header is preserved for TCP backends; `X-Forwarded-For`, `X-Forwarded-Host`, `X-Forwarded-Proto`, `Tailscale-User-Login`, `Tailscale-User-Name`, and `Tailscale-User-Profile-Pic` are set (identity headers are absent for tagged devices); a directory target uses Go's `http.FileServer`, which serves `index.html`. The proxy connects to the backend from `127.0.0.1`, so today every tailnet player collapses into one loopback accounting principal capped at 8 WebSocket sessions and 4 upgrades per 20 seconds by `src/server/server_limits.hpp`.

Two defects to fix early: `config/blob-royale.cfg` has an empty `allowed_origins`, and `src/server/game_api_router.cpp:365` rejects any present Origin not on the list, so the README `npm run dev` flow fails with 403 on the WebSocket upgrade. The local Colima VM is 2 CPUs / 2 GiB with QEMU emulation, which kills sanitizer, fuzz, browser, and scanner lanes; a QEMU `core` dump sits untracked at the repository root.

## Execution constraints

- **Design accepted on approval.** Approving this plan accepts the decisions in Steps 10, 11, 12, and 13 as written: host networking with a loopback listener behind `tailscale serve`; Tailscale as the only authentication; connection-scoped ownership with proxy-supplied identity; the thrust/drag/shrinking-zone core loop. Record them as ADRs and continue without another design pause unless verification contradicts them.
- **Authority.** GitHub Actions on `ubuntu-24.04` and native runs on `cole-ubuntu-pc` are authoritative. Mac runs, including Colima after resizing, remain advisory. Never skip, retry, or relax a gate to get green.
- **Deploy only through `scripts/deploy-tailnet`.** It runs the release profile first, so nothing unverified can be served. Never enable Funnel.
- **Single writer stays.** Only the runtime worker thread calls `GameSimulation::step`; network code receives a write-only command sink and `const SnapshotPublication&`, never the simulation or runtime.
- **Zero drag preserves the baseline.** All existing fixtures and tests run with `drag_per_second=0` so ADR 0003 horizons stay bit-identical; the deployment config sets nonzero drag.
- **Commit hygiene.** `test:`, `fix:`, `feat:`, `refactor:`, `build:`, `docs:`, `deploy:` prefixes; every commit stays green on the pull-request profile. Phases 3 to 5 may land as reviewed commits before deployment because the v1 client keeps working against a server that also speaks v2.

## Steps

### Phase 0 — Establish authority and a fast loop

- [x] **Step 1: Push `main` and obtain the first authoritative CI result**
  - Verify: `git push origin main && gh run watch --exit-status $(gh run list --workflow quality.yml --branch main --limit 1 --json databaseId --jq '.[0].databaseId')`
  - Notes: Fix forward with separate `fix:` commits if a never-exercised lane fails (sanitizers, fuzz regressions, Chromium, Syft/Grype). Fresh Grype databases may flag the 2026-08-04 pins; bump pins deliberately in `ci/linux/Dockerfile` or `frontend-react/package-lock.json`, never suppress findings. Enable branch protection requiring `quality/linux-authoritative` once the run is green.
  - Execution note (2026-09-06): Run `34061268911` at `57deb18` is the first green `quality/linux-authoritative`. Runs one to three failed only in dependency evidence (graph-driver archive layout, `fast-uri` advisories, evidence-directory move) and every build, test, sanitizer, browser, and fuzz lane passed from the first run. Branch protection was deliberately not enabled yet: required status checks also reject direct pushes, and Steps 18, 26, and 28 push to `main` directly. Enable it when work moves to pull requests.

- [x] **Step 2: Repair the documented browser dev flow**
  - Verify: `rg -n '^allowed_origins=http://127.0.0.1:5173, http://localhost:5173$' config/blob-royale.cfg && test ! -e core && rg -n 'allowed_origins' README.MD`
  - Notes: Add both dev origins to `config/blob-royale.cfg`, delete the untracked `core` dump, and add one README sentence explaining that browsers always send Origin on WebSocket upgrades. No test pins the file's contents; `scripts/assemble-release-linux:125` copies it into the image as an example, which is fine.

- [x] **Step 3: Resize Colima and switch amd64 emulation to Rosetta**
  - Verify: `colima list | rg 'default\s+Running\s+aarch64\s+8\s+12GiB' && ./scripts/run-linux-toolchain -- bash -c 'cmake --preset linux-clang-asan-ubsan && cmake --build --preset linux-clang-asan-ubsan && ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:strict_string_checks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --preset linux-clang-asan-ubsan -R unit.simulation --output-on-failure --no-tests=error'`
  - Notes: `colima stop && colima start --cpu 8 --memory 12 --vz-rosetta`. The sanitizer lane previously died with exit 137 under QEMU. Results remain advisory; the point is a usable local loop.

- [x] **Step 4: Add `scripts/verify-focused` for one-command focused native checks**
  - Verify: `./scripts/verify-focused 'unit.simulation' && ./scripts/verify-focused 'unit.runtime' linux-clang-asan-ubsan`
  - Notes: Usage `verify-focused <ctest-regex> [preset]`, default preset `linux-gcc-debug`. ThreadSanitizer cannot execute under Rosetta because Rosetta rejects `personality(ADDR_NO_RANDOMIZE)`, so the `linux-clang-tsan` preset is native-only: run it on `cole-ubuntu-pc` or in CI. It re-enters `./scripts/run-linux-toolchain` and runs `cmake --preset`, `cmake --build --preset`, then `ctest --preset -R <regex> --output-on-failure --no-tests=error`. It is a development convenience and must print the same advisory/authoritative host classification the gate prints; it never replaces `verify-linux`.

- [x] **Step 5: Bootstrap `cole-ubuntu-pc` as the authoritative release runner**
  - Verify: `ssh ubuntu-tailscale 'cd ~/Projects/blob-royale && grep -q "^verification_authority=authoritative$" out/release/current/publication.env && docker image inspect --format "{{.Id}}" "$(grep -o "^runtime_image_reference=.*" out/release/current/publication.env | cut -d= -f2)"'`
  - Execution note (2026-09-06): The first authoritative publication landed at `57deb18` after four native runs exposed defects no emulated run had reached: the TSan lane needs `vm.mmap_rnd_bits=28` on the host (`/etc/sysctl.d/60-blob-royale-sanitizers.conf`) because Docker's seccomp blocks TSan's `personality(ADDR_NO_RANDOMIZE)`; the archive identity check assumed the graph-driver `docker save` layout; `fast-uri` 3.1.5 carried four high advisories; and the evidence directory was made read-only before a cross-parent move. Each was fixed forward in its own commit.
  - Notes: Clone `git@github.com:scshafe/blob-royale.git` to `~/Projects/blob-royale` with the host's own GitHub credentials, check out the pushed `main`, and run `./scripts/verify-linux release`. The release profile refuses a modified checkout. Expect the first run to take a while; it builds the pinned toolchain image and Catch2 once. This closes the nine open steps of the 2026-08-04 plan; tick them there with an execution note citing this run.

### Phase 1 — Deploy the existing read-only build to the tailnet

- [x] **Step 6: Commit deployment assets and the idempotent deploy script**
  - Verify: `bash -n scripts/deploy-tailnet && ./scripts/verify-focused 'fixtures.deployment'`
  - Notes: Add `deploy/ubuntu-pc/blob-royale.cfg` with `bind_address=127.0.0.1`, `port=8000`, `allowed_hosts=127.0.0.1:8000, cole-ubuntu-pc.colobus-stargazer.ts.net:8444`, `allowed_origins=https://cole-ubuntu-pc.colobus-stargazer.ts.net:8444`, `trusted_proxy_addresses=` (empty until Step 20), the current world/grid values, and `deploy/ubuntu-pc/scenario.csv` as a copy of `tests/fixtures/player-on-player-collision-test.csv` so the read-only deployment shows moving discs. Add a fixture test that loads both files through `ApplicationConfigLoader` and `ScenarioLoader`. `scripts/deploy-tailnet` runs on the host from the repository root and, in order: refuses non-Linux/x86_64 or a dirty tree; runs `./scripts/verify-linux release`; reads `release_id` and `runtime_image_reference` from `out/release/current/publication.env`; installs `/srv/blob-royale/config/{blob-royale.cfg,scenario.csv}` mode 0444 and rsyncs `out/release/current/deployable/web/` to `/srv/blob-royale/web/`; replaces container `blob-royale` with `docker run -d --name blob-royale --restart unless-stopped --network host --read-only --cap-drop ALL --security-opt no-new-privileges:true --memory 1g --cpus 2 -v /srv/blob-royale/config:/run/blob-royale:ro <image> --config /run/blob-royale/blob-royale.cfg --scenario /run/blob-royale/scenario.csv`; waits up to 10 seconds for `http://127.0.0.1:8000/api/v1/health/ready`; then applies Step 8's two `tailscale serve` commands (they are idempotent). Host networking keeps the listener on loopback, which is the safest configuration the server accepts and what the proxy connects to.

- [x] **Step 7: Run the server container on `cole-ubuntu-pc`**
  - Verify: `ssh ubuntu-tailscale 'cd ~/Projects/blob-royale && git pull --ff-only && ./scripts/deploy-tailnet && curl -fsS http://127.0.0.1:8000/api/v1/health/ready | grep -q "\"status\":\"ready\"" && docker ps --filter name=^blob-royale$ --format "{{.Status}}" | grep -q Up'`
  - Notes: Logs are JSON lines on stderr; read them with `docker logs blob-royale`. A nonzero startup exit means a configuration error, not something to retry.
  - Execution note (2026-09-06): `deploy-tailnet` ran under `nohup` with its log at `~/blob-royale-deploy.log` on the host and ended with `deploy_tailnet.status=passed` at `57deb18`, release `release-e4a99f98f062f99aa5c841449cd520a225d6acea0da5a0c1c612088db139ef66`. The first successful deployment exited nonzero only in its final DNS-name pipeline, fixed in `57deb18` before the rerun.

- [x] **Step 8: Publish the site through `tailscale serve` on port 8444**
  - Verify: `curl -fsS https://cole-ubuntu-pc.colobus-stargazer.ts.net:8444/api/v1/health/live | rg -q '"status":"alive"' && curl -fsS https://cole-ubuntu-pc.colobus-stargazer.ts.net:8444/ | rg -q '<script'`
  - Notes: On the host: `sudo tailscale serve --bg --https=8444 --set-path=/api http://127.0.0.1:8000/api` and `sudo tailscale serve --bg --https=8444 /srv/blob-royale/web`. The target path `/api` restores the prefix that `--set-path` strips. Tailnet only; never `tailscale funnel`. Fallback if the proxied path or Host does not match exactly: run `caddy:2-alpine` on `127.0.0.1:8081` with `reverse_proxy /api/* 127.0.0.1:8000` plus `root * /srv/blob-royale/web` and `file_server`, and point one `tailscale serve --https=8444 http://127.0.0.1:8081` at it. Record whichever is used in Step 9's runbook.

- [ ] **Step 9: Confirm the read-only client renders over the tailnet and write the tailnet runbook**
  - Verify: human review — the page shows "Connected to the read-only snapshot stream." and moving discs on two tailnet devices, and `docs/operations/tailnet.md` documents deploy, rollback (rerun `deploy-tailnet` at the previous commit), logs, and the serve configuration.
  - Notes: Also link the runbook from `README.MD` and `docs/operations/linux.md`.

- [x] **Step 10: Amend ADR 0001 with the observed deployment host**
  - Verify: `rg -n 'cole-ubuntu-pc' docs/architecture/0001-linux-runtime-contract.md && rg -n '^\*\*Amended 2026-' docs/architecture/0001-linux-runtime-contract.md`
  - Specialist: `rigorous-architect`
  - Notes: Record distro/kernel/Docker versions, Docker restart policy as the supervisor, `tailscale serve` with Tailscale-issued certificates as the TLS boundary, tailnet-only exposure on 8444, host networking with a loopback listener, and that the same host is the authoritative release runner. Replace "No specific orchestrator is selected yet" and "proxy product and certificate source remain deployment facts to record later".

### Phase 2 — Accept the game and protocol contracts

- [ ] **Step 11: Write ADR 0004, the Blob Royale core loop**
  - Verify: human review — `docs/architecture/0004-core-loop.md` has status `Accepted` and defines every rule below with units from ADR 0003.
  - Specialist: `rigorous-architect`
  - Notes: Thrust command sets a unit-clamped direction scaled by `thrust_max_world_units_per_second_squared` into stored acceleration. Linear drag `v ← v × max(0, 1 − drag_per_second × dt)` applied after acceleration. Circular safe zone centered on the arena with radius shrinking linearly from covering the arena to `zone_minimum_radius_world_units` over `zone_shrink_seconds` after `running` begins. A blob whose center is outside the zone for `elimination_grace_seconds` is eliminated, leaves the physics world, and is recorded with its placement. Phases: `lobby` (fewer than `lobby_minimum_players` alive), `countdown` (`countdown_seconds`), `running`, `ended` (`restart_delay_seconds`, winner or draw), then back to `lobby` with every connected player respawned. Spawn requests during `running` wait as pending until the next `lobby`. Spawn positions come from a fixed ring of slots indexed by a spawn counter; an occupied slot advances to the next; a full ring defers the spawn one tick. All ties resolve by ascending `EntityId`. Proposed initial values for a 960×640 arena: thrust 400, drag 2.0, zone minimum 60, shrink 90 s, grace 3 s, lobby minimum 2, countdown 5 s, restart 8 s.

- [ ] **Step 12: Amend ADR 0003 for input, drag, dynamic roster, and gameplay phases**
  - Verify: human review — `rg -n '^\*\*Amended 2026-' docs/architecture/0003-deterministic-simulation-contract.md` and the fixture table gains drag-decay, spawn-order, zone-elimination, and match-transition cases.
  - Specialist: `rigorous-architect`
  - Notes: Phase 0 becomes "apply the tick's validated `InputBatch`": despawns, then spawns, then thrust into stored acceleration, all in ascending `EntityId`. Drag is part of phase 1. New phases after the grid rebuild and before commit: zone radius, elimination, match transition. State that `drag_per_second=0` and an empty batch reproduce the accepted baseline bit-for-bit, which keeps every existing fixture horizon valid. Replace the "Excluded player commands" section with the accepted input contract.

- [ ] **Step 13: Specify protocol v2 with schemas and golden examples**
  - Verify: `cd frontend-react && npm run validate:protocol-examples` covers `docs/protocol/schema/v2/examples/*.json` and human review of the trust-boundary section in `docs/protocol/v2.md`.
  - Specialist: `doddy`
  - Notes: One new route `GET /api/v2/session` upgraded with subprotocol `blob-royale.session.v2`. Connecting is joining; closing is leaving. Server messages: `welcome` (`entity_id`, `display_name`) then `snapshot` frames carrying `tick_sequence`, `match` (`phase`, `phase_started_tick`, `zone` center/radius, `alive_count`, `winner_entity_id` or null, bounded `placements`), and `players` (`entity_id`, `display_name`, position, velocity, acceleration). Client message: `set_thrust` with `x`, `y` in `[-1, 1]`, at most 1,024 bytes, admitted by a per-session token bucket (burst 30, refill 20/s; excess closes `1008 command_rate_exceeded`). The server stamps the session's own `entity_id` on every command; a client cannot address another entity. Identity: authentication is the tailnet; ownership is the connection. When the socket peer is in `trusted_proxy_addresses`, the accounting principal is the single canonical `X-Forwarded-For` address and `display_name` is a sanitized, 64-byte-bounded `Tailscale-User-Name`; malformed or missing forwarding data closes the connection. Direct loopback peers keep the socket principal and the name `player-<entity_id>`. State plainly that a loopback trusted proxy means any local process on the host can forge identity, which is accepted for a single-operator host. v1 stays unchanged.

### Phase 3 — Deterministic gameplay in `blob_simulation`

- [ ] **Step 14: Add `GameplayConfig` and the strict `[gameplay]` configuration section**
  - Verify: `./scripts/verify-focused 'unit.application|unit.simulation|fixtures'`
  - Notes: Keys named exactly as in Step 11 with units in the names. The loader rejects a missing section, unknown keys, non-finite or negative values, and `lobby_minimum_players` below 1. Add `[gameplay]` to `config/blob-royale.cfg`, `deploy/ubuntu-pc/blob-royale.cfg`, `frontend-react/e2e/fixtures/blob-royale-browser-e2e.cfg`, and the fuzz corpus configs, with `drag_per_second=0` everywhere except the deployment file. Fixture and test configurations must also set `lobby_minimum_players` above their CSV roster size so a seeded match stays in `lobby` and the ADR 0003 horizons remain bit-identical (ADR 0003 amendment, bit-identity paragraph).

- [ ] **Step 15: Add `InputBatch` and command values**
  - Verify: `./scripts/verify-focused 'unit.simulation'`
  - Notes: `ThrustCommand`, `SpawnCommand`, `DespawnCommand`, and `InputBatch::create` that canonicalizes to ascending `EntityId`, keeps the last thrust per entity, rejects an entity that both spawns and despawns in one batch, and rejects non-finite or out-of-range components. Pure values, no allocation surprises, no dependency additions to `blob_simulation`.

- [ ] **Step 16: Extend `GameSimulation::step` with the input phase and drag**
  - Verify: `./scripts/verify-focused 'unit.simulation|fixtures'` with the pre-existing tests unmodified except for the added empty-batch argument.
  - Notes: Signature `step(FixedDelta, const InputBatch&)`. Roster changes rebuild `GameWorld` before physics; spawn placement follows Step 11. Existing fixture expectations must pass unchanged with zero drag and empty batches, proving the baseline is preserved. Add tests for thrust integration, drag decay, spawn slot order, occupied-slot advance, and despawn of an entity referenced by a pending pair.

- [ ] **Step 17: Add zone, elimination, and match lifecycle phases plus snapshot fields**
  - Verify: `./scripts/verify-focused 'unit.simulation'` including a test that 100 fresh runs of a scripted multi-player match produce bit-identical ordered snapshots, then `./scripts/run-benchmarks-linux` still passes.
  - Notes: `MatchState` and `SafeZone` are world-owned values; `WorldSnapshot` gains them and the bounded placement list. Eliminated players are despawned in the same tick they are eliminated. Pending spawns are world state so `lobby` entry is deterministic.

### Phase 4 — Runtime, protocol, server, and application

- [ ] **Step 18: Add the runtime command mailbox and write-only `CommandSink`**
  - Verify: `./scripts/verify-focused 'unit.runtime' linux-clang-asan-ubsan && git push origin main && ssh ubuntu-tailscale 'cd ~/Projects/blob-royale && git pull --ff-only && ./scripts/verify-focused unit.runtime linux-clang-tsan'`
  - Notes: `CommandMailbox` is a bounded, mutex-protected buffer inside `SimulationRuntime`; the worker swaps it out once per tick, builds one `InputBatch`, and calls `step`. `CommandSink` exposes `open_session() -> EntityId`, `set_thrust(EntityId, Vector2)`, and `close_session(EntityId)` and nothing else. Overflow drops the oldest thrust for the same entity, never a spawn or despawn. TSan runs are advisory on the Mac and authoritative on `cole-ubuntu-pc` and CI.

- [ ] **Step 19: Implement v2 encoding and decoding in `blob_protocol` and regenerate client types**
  - Verify: `./scripts/verify-focused 'unit.protocol' && cd frontend-react && npm run generate:protocol && npm run generate:protocol:check`
  - Notes: Extend `frontend-react/scripts/generateProtocolV1Types.mjs` to walk `docs/protocol/schema/v2` too, or add a v2 sibling, producing `protocolV2Types.generated.ts` and `protocolV2Schemas.generated.ts`. Golden bytes for every v2 example and rejection cases for extra members, non-finite values, oversized frames, and a thrust outside `[-1, 1]`.

- [ ] **Step 20: Add the `/api/v2/session` route, session class, and trusted-proxy identity**
  - Verify: `./scripts/verify-focused 'unit.server|integration'`
  - Notes: `GameApiRouter` admits the v2 upgrade under the same host, origin, rate, and connection policies. `SessionWebSocketSession` sends `welcome`, decodes inbound frames, applies the per-session command bucket, forwards stamped commands to `CommandSink`, pushes v2 snapshots at presentation cadence, and calls `close_session` exactly once on any close path. Implement the Step 13 principal and display-name rules for peers in `trusted_proxy_addresses`. Integration test: two sessions join, a thrust from one moves only that entity, a disconnect despawns it, and a forged entity id in a frame is ignored.

- [ ] **Step 21: Wire the application, then pass the full pull-request profile**
  - Verify: `./scripts/verify-linux pr`
  - Notes: `BlobRoyaleApplication` passes the runtime's `CommandSink&` into `GameServer` alongside `const SnapshotPublication&`. Update `src/*/README.md` extension-point text, the `simulation_input` bullet in `docs/architecture/0002-simulation-architecture.md` (it still says a future batch "may become an argument"), and `docs/operations/linux.md` for the new route. The profile is advisory on the Mac; Step 28 makes it authoritative.

### Phase 5 — Client

- [ ] **Step 22: Speak protocol v2 in `SimulationApi` and the connection hook**
  - Verify: `cd frontend-react && npm run typecheck && npm run lint && npm run test:ci`
  - Notes: Open the v2 session socket instead of v1, handle `welcome`, expose `myEntityId` and `match` in `useSimulationConnection` state, and add a `sendThrust` capability that is a no-op unless connected. Keep the reconnect budget; a reconnect is a new join by contract.

- [ ] **Step 23: Add thrust input**
  - Verify: `cd frontend-react && npm run test:ci -- useThrustInput`
  - Notes: WASD and arrow keys map to a unit direction; sends only on change and at most every 50 ms; releases send zero thrust. Ignore input while the own entity is absent from the snapshot.

- [ ] **Step 24: Render the game**
  - Verify: `cd frontend-react && npm run test:ci && npm run build`
  - Notes: Draw the zone circle, highlight the own blob, label blobs with `display_name`, show phase, countdown, alive count, and placement, and show overlays for waiting, eliminated, winner, and draw. Keep components presentational and the debug panel available behind a toggle.

- [ ] **Step 25: Add a two-player Chromium end-to-end flow**
  - Verify: `./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`
  - Specialist: `testineer`
  - Notes: Two browser contexts join, both blobs appear with names, thrust from one moves only that blob, the zone radius decreases, and the existing server-loss recovery spec still passes. Use the e2e config with a header-only scenario.

### Phase 6 — Ship and play

- [ ] **Step 26: Redeploy to the tailnet with an empty starting world**
  - Verify: `ssh ubuntu-tailscale 'cd ~/Projects/blob-royale && git pull --ff-only && ./scripts/deploy-tailnet' && curl -fsS https://cole-ubuntu-pc.colobus-stargazer.ts.net:8444/api/v1/health/ready | rg -q '"status":"ready"'`
  - Notes: Switch `deploy/ubuntu-pc/scenario.csv` to header-only and set `trusted_proxy_addresses=127.0.0.1` so tailnet identity headers are honored. The release profile inside the script is authoritative on this host.

- [ ] **Step 27: Playtest with at least two tailnet devices**
  - Verify: human review — `docs/playtests/2026-MM-DD.md` records participants, device types, a completed match with a winner, latency feel, and every defect found.
  - Notes: Do not fix balance or feel inside this plan; file follow-ups.

- [ ] **Step 28: Certify the shipped commit**
  - Verify: `gh run list --workflow quality.yml --branch main --limit 1 --json conclusion --jq '.[0].conclusion' | rg -q success && ssh ubuntu-tailscale 'cd ~/Projects/blob-royale && test "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" && grep -q "^verification_authority=authoritative$" out/release/current/publication.env'`
  - Specialist: `proofreader`
  - Notes: The deployed commit, `origin/main`, and the authoritative publication record must agree. Tick the nine remaining steps of the 2026-08-04 plan with a pointer to this evidence.

## Done criteria

- Two people on the tailnet complete a match at `https://cole-ubuntu-pc.colobus-stargazer.ts.net:8444` with one winner, and the playtest note exists.
- `quality/linux-authoritative` is green on `origin/main`, and `out/release/current/publication.env` on `cole-ubuntu-pc` says `authoritative` for the same commit that is running.
- ADR 0001 records the real host; ADR 0004 and protocol v2 are `Accepted`; ADR 0003 is amended; all existing fixtures still pass with zero drag.
- `blob_simulation` still depends only on the standard library, only the runtime worker mutates the simulation, and the server holds only `CommandSink&` and `const SnapshotPublication&`.
- No gate was skipped or weakened, Funnel is off, and the deployment is reproducible by rerunning `scripts/deploy-tailnet` at the certified commit.

**Amended 2026-09-06:** Swapped Steps 3 and 4 so the Colima resize precedes the first sanitizer verify; the original order asked a TSan lane to pass under the 2 GiB QEMU VM that the resize replaces.

**Amended 2026-09-06:** Steps 4 and 18 no longer run ThreadSanitizer on the Mac. Rosetta rejects the `personality(ADDR_NO_RANDOMIZE)` call TSan needs (`tsan_platform_linux.cpp:282` CHECK failure), so the local loop covers GCC and ASan/UBSan and the TSan lane runs natively on `cole-ubuntu-pc` and in CI.

**Amended 2026-09-06:** Host-side verifies in Steps 5, 7, and 28 use `grep` instead of `rg` because `cole-ubuntu-pc` has no ripgrep installed.
