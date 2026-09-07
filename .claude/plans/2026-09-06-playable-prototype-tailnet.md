# Plan: Playable Blob Royale prototype on the tailnet

**Goal:** Two or more people on the tailnet open `https://cole-ubuntu-pc.colobus-stargazer.ts.net:8444`, each steers one blob alongside bots that act through the same input path, a shrinking safe zone eliminates blobs until one wins, the server is the authoritative release image running on `cole-ubuntu-pc`, and the gameplay layer is the extensible framework of ADR 0004 in which modes, maps, entity kinds, mechanics, and controllers are added by new files plus one registration line.
**Out of scope:** Accounts or passwords (Tailscale is the perimeter), Funnel or any internet exposure, multiple rooms or matchmaking, persistence or leaderboards, client-side prediction or interpolation, growth or unequal-mass mechanics, a second game beyond `sandbox` and `royale`, AI-model-driven controllers (the seam ships, not the model), parallel physics, retiring protocol v1, and any weakening of the Linux-authoritative gates.

## Context

The foundation is complete but has zero gameplay. Protocol v1 is read-only (`docs/protocol/v1.md`), the simulation has no input parameter (`src/simulation/game_simulation.hpp`), the roster is fixed from a CSV at startup (`src/application/scenario_loader.cpp`), and the client only renders (`frontend-react/src/features/simulation/SimulationCanvas.tsx`). The documented seam is `@extension-point simulation_input` in `docs/architecture/0002-simulation-architecture.md` § "Extension points", with commands explicitly excluded by `docs/architecture/0003-deterministic-simulation-contract.md:154`.

Local `main` is 19 commits ahead of `origin/main` and has never been pushed, so `.github/workflows/quality.yml` has never run. Nine steps in `.claude/plans/2026-08-04-feature-ready-foundation.md` are unchecked only because no native Linux/x86_64 host ever executed the gate. Every local stamp under `out/verification/linux/` says `verification_authority=advisory`.

Deployment host facts observed over `ssh ubuntu-tailscale` on 2026-09-06: `cole-ubuntu-pc`, Ubuntu 24.04.4 LTS, x86_64 Ryzen 5 5600G, 12 CPUs, 125 GiB RAM, Docker 29.6.1 native linux/x86_64, user `cole` in the `docker` group with passwordless sudo, Tailscale 1.102.2, MagicDNS `cole-ubuntu-pc.colobus-stargazer.ts.net`. Because both the Docker client and daemon are native Linux/x86_64, `./scripts/verify-linux release` on that host is **authoritative** under ADR 0001 and `scripts/publish-release-linux:175`. HTTPS ports 443 and 8443 are already claimed by other `tailscale serve` handlers on that host, so Blob Royale uses `--https=8444`. Port 8000 is free. No repository clone exists there yet.

`tailscale serve` facts from the Tailscale source (`ipn/ipnlocal/serve.go`): the mount prefix is stripped with `http.StripPrefix`, so the backend target must carry `/api`; the client `Host` header is preserved for TCP backends; `X-Forwarded-For`, `X-Forwarded-Host`, `X-Forwarded-Proto`, `Tailscale-User-Login`, `Tailscale-User-Name`, and `Tailscale-User-Profile-Pic` are set (identity headers are absent for tagged devices); a directory target uses Go's `http.FileServer`, which serves `index.html`. The proxy connects to the backend from `127.0.0.1`, so today every tailnet player collapses into one loopback accounting principal capped at 8 WebSocket sessions and 4 upgrades per 20 seconds by `src/server/server_limits.hpp`.

On 2026-09-06, after Steps 1 through 10 were verified, the owner set the gameplay design direction: build the game layer as a DRY, extensible framework so that once the multiplayer engine with inputs works, new games, entity kinds, and interactions are additions rather than edits, and computer-controlled entities act exactly as users do so bots can carry behaviors, later personalities and AI-driven policies. ADR 0002 had reserved this move for when "real gameplay creates that cardinality"; the requirement is that cardinality. The original Phase 2 to 6 steps, which hardcoded Royale as phases inside `GameSimulation`, are struck below and replaced by Phases 2 to 7 built on `docs/architecture/0004-gameplay-architecture.md`.

Two defects to fix early: `config/blob-royale.cfg` has an empty `allowed_origins`, and `src/server/game_api_router.cpp:365` rejects any present Origin not on the list, so the README `npm run dev` flow fails with 403 on the WebSocket upgrade. The local Colima VM is 2 CPUs / 2 GiB with QEMU emulation, which kills sanitizer, fuzz, browser, and scanner lanes; a QEMU `core` dump sits untracked at the repository root.

## Execution constraints

- **Design accepted on approval.** Approving this plan accepts the decisions in Steps 10 through 14 as written: host networking with a loopback listener behind `tailscale serve`; Tailscale as the only authentication; connection-scoped ownership with proxy-supplied identity; the ADR 0004 gameplay framework; the ADR 0005 thrust/drag/shrinking-zone Royale mode. Record them as ADRs and continue without another design pause unless verification contradicts them.
- **Framework discipline.** Values live in `blob_simulation`, rules live in `blob_gameplay`, decisions live in `blob_controllers`. Every seam carries an `@extension-point` tag, is registered in exactly one registry file, and is justified by two named implementations. A new mode, map, entity kind, mechanic, command kind, or bot must not require editing the kernel, another mode, or the server. Bots run outside the tick through the same `CommandSink` as network sessions.
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

- [x] **Step 9: Confirm the read-only client renders over the tailnet and write the tailnet runbook**
  - Verify: human review — the page shows "Connected to the read-only snapshot stream." and moving discs on two tailnet devices, and `docs/operations/tailnet.md` documents deploy, rollback (rerun `deploy-tailnet` at the previous commit), logs, and the serve configuration.
  - Notes: Also link the runbook from `README.MD` and `docs/operations/linux.md`.

- [x] **Step 10: Amend ADR 0001 with the observed deployment host**
  - Verify: `rg -n 'cole-ubuntu-pc' docs/architecture/0001-linux-runtime-contract.md && rg -n '^\*\*Amended 2026-' docs/architecture/0001-linux-runtime-contract.md`
  - Specialist: `rigorous-architect`
  - Notes: Record distro/kernel/Docker versions, Docker restart policy as the supervisor, `tailscale serve` with Tailscale-issued certificates as the TLS boundary, tailnet-only exposure on 8444, host networking with a loopback listener, and that the same host is the authoritative release runner. Replace "No specific orchestrator is selected yet" and "proxy product and certificate source remain deployment facts to record later".

### ~~Phase 2 — Accept the game and protocol contracts~~ (superseded 2026-09-06)

- [ ] ~~**Step 11: Write ADR 0004, the Blob Royale core loop**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
- [ ] ~~**Step 12: Amend ADR 0003 for input, drag, dynamic roster, and gameplay phases**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
- [ ] ~~**Step 13: Specify protocol v2 with schemas and golden examples**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
### ~~Phase 3 — Deterministic gameplay in `blob_simulation`~~ (superseded 2026-09-06)

- [ ] ~~**Step 14: Add `GameplayConfig` and the strict `[gameplay]` configuration section**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
- [ ] ~~**Step 15: Add `InputBatch` and command values**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
- [ ] ~~**Step 16: Extend `GameSimulation::step` with the input phase and drag**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
- [ ] ~~**Step 17: Add zone, elimination, and match lifecycle phases plus snapshot fields**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
### ~~Phase 4 — Runtime, protocol, server, and application~~ (superseded 2026-09-06)

- [ ] ~~**Step 18: Add the runtime command mailbox and write-only `CommandSink`**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
- [ ] ~~**Step 19: Implement v2 encoding and decoding in `blob_protocol` and regenerate client types**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
- [ ] ~~**Step 20: Add the `/api/v2/session` route, session class, and trusted-proxy identity**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
- [ ] ~~**Step 21: Wire the application, then pass the full pull-request profile**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
### ~~Phase 5 — Client~~ (superseded 2026-09-06)

- [ ] ~~**Step 22: Speak protocol v2 in `SimulationApi` and the connection hook**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
- [ ] ~~**Step 23: Add thrust input**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
- [ ] ~~**Step 24: Render the game**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
- [ ] ~~**Step 25: Add a two-player Chromium end-to-end flow**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
### ~~Phase 6 — Ship and play~~ (superseded 2026-09-06)

- [ ] ~~**Step 26: Redeploy to the tailnet with an empty starting world**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
- [ ] ~~**Step 27: Playtest with at least two tailnet devices**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
- [ ] ~~**Step 28: Certify the shipped commit**~~ — original step, superseded 2026-09-06 by the gameplay-framework re-plan (Steps 11 to 33 below).
### Phase 2 — Accept the gameplay framework and protocol contracts

- [x] **Step 11: Accept ADR 0004 (gameplay architecture) and the ADR 0002 amendment**
  - Verify: human review — `docs/architecture/0004-gameplay-architecture.md` has status `Accepted`; `docs/architecture/0002-simulation-architecture.md` carries an `**Amended 2026-09-06:**` note admitting `blob_gameplay`, `blob_controllers`, component composition, staged systems, `GameMode` inheritance, and controllers; both use the existing names `GameWorld` and `PhysicsBody`.
  - Specialist: `rigorous-architect`
  - Notes: The framework is the owner's stated requirement of 2026-09-06: modes, maps, entity kinds, mechanics, and bots are added by new files plus one registration line, and computer-controlled entities act exactly as users do. The eight `@extension-point` seams (`entity_component`, `simulation_system`, `contact_rule`, `game_mode`, `map_definition`, `command_kind`, `controller`, `entity_renderer`) and the "values live in `blob_simulation`, rules live in `blob_gameplay`" rule are the contract every later step implements.

- [x] **Step 12: Accept ADR 0005, the Royale mode, expressed on the framework**
  - Verify: human review — `docs/architecture/0005-royale-mode.md` is titled as ADR 5 with status `Accepted`, expresses thrust, drag, zone, elimination, lifecycle, and ring spawns as a `GameMode` (systems at named stages, a `SpawnPolicy`, a `MatchObjective`), defines the `[royale]` configuration section, and cites ADR 0004 for every interface.
  - Specialist: `rigorous-architect`
  - Notes: Rework the existing draft rather than restarting; its rules and numbers stand. Drag moves to the kernel as `[simulation] drag_per_second` (zero in every fixture and test configuration, `2.0` in deployment), because ADR 0003's phase 1 owns it. Fix the draft's stale citations (`0004-core-loop.md`, ADR 0002 line 88).

- [x] **Step 13: Amend ADR 0003 for the staged kernel and accepted input**
  - Verify: human review and `rg -n '^\*\*Amended 2026-' docs/architecture/0003-deterministic-simulation-contract.md`
  - Specialist: `rigorous-architect`
  - Notes: Redo the in-progress draft: phase 0 applies the `InputBatch` (despawns, spawns through the engine `SpawnSystem` and the mode's `SpawnPolicy`, then commands), phase 1 gains drag, phase 3 evaluates the `ContactRuleTable` (built-in rows reproduce today's elastic and reflect results exactly), and the hook stages `kPreKernel`, `kPostKernel`, `kLifecycle` are where mode systems run; commit clears events. State that zero drag, an empty batch, and a mode with no systems reproduce every accepted fixture bit-for-bit. Cite ADR 0005 for Royale rules (never restate them) and ADR 0004 for interfaces.

- [x] **Step 14: Specify protocol v2 with schemas and golden examples**
  - Verify: `cd frontend-react && npm run validate:protocol-examples` covers `docs/protocol/schema/v2/examples/*.json` and human review of the trust-boundary section in `docs/protocol/v2.md`.
  - Specialist: `doddy`
  - Execution note (2026-09-06): 18 schemas, 4 golden examples, and 14 negative cases; the validator now covers v1 and v2 offline and fails on unmapped examples. Three accepted deviations: the snapshot entity bound is 1,024 rather than v1's 4,096 because 4,096 entities plus placements exceed the unchanged 2 MiB frame ceiling and a schema-legal-but-unencodable snapshot would be a remotely triggerable hard failure (`kMaximumPlayerCount` is unchanged; startup must validate statics plus roster against 1,024 in Step 26); `outcome` carries `winner_entity_id` and `winner_team_id` as distinct members rather than one discriminated `winner`; and `welcome.entity_id` is the session's first body while `controller_id` is its durable identity, because elimination and the lobby wipe destroy entities.
  - Notes: One route `GET /api/v2/session` with subprotocol `blob-royale.session.v2`; connecting joins, closing leaves. Server messages: `welcome` (`entity_id`, `display_name`, `mode`, `map`) then `snapshot` frames carrying `tick_sequence`, `entities` (each `entity_id` plus components keyed by component kind, one closed schema per kind, ascending ids), and `match` (`mode`, `phase`, `phase_started_tick`, `outcome`, bounded `placements`, and mode state by schema id). Mode state that is entity-shaped is published as components, never duplicated in `match`: the Royale zone is the `Zone` component of its zone entity (ADR 0005 § "Match section fields"). Client messages are command envelopes `{kind, payload}` with one closed schema per command kind (`set_thrust` first), 1,024-byte limit, per-session bucket (burst 30, refill 20/s, excess closes `1008 command_rate_exceeded`); the server stamps the session's entity. Adding a component or command kind is a protocol minor version that clients check. Identity: the tailnet authenticates; the connection owns one entity; when the socket peer is in `trusted_proxy_addresses`, the accounting principal is the single canonical `X-Forwarded-For` and `display_name` is a sanitized 64-byte `Tailscale-User-Name`; direct loopback peers keep the socket principal and `player-<entity_id>`. Bots appear as entities whose `Controllable` names their controller kind. State that a loopback trusted proxy lets any local process forge identity, accepted for a single-operator host. v1 stays unchanged.

### Phase 3 — Engine kernel in `blob_simulation`

- [x] **Step 15: Introduce components, stores, and the registry**
  - Verify: `./scripts/verify-focused 'unit.simulation|fixtures'`
  - Execution note (2026-09-06): Verified at 96 tests (`unit.simulation` 91, up from 65, plus 5 fixtures); the full cross-domain run passes 272 and ASan/UBSan 202. `Player` is deleted; a player is an entity carrying `PhysicsBody` and `Controllable`. World equality, `destroy_entity`, and snapshot publication are generated from the registry type list, so a new kind cannot forget to participate. `PhysicsBody` keeps its class shape per the ADR's own "the existing value, extended" and gains radius, mass, collision layer and mask, and `is_static`, none of which any phase reads yet. `GameWorld::EntitySeed` is a transitional construction path that retires when spawn commands seat entities. The forbidden-dependency scan must use `grep -RnE` inside the pinned image: `rg` is absent there, so `! rg ...` fails open on exit 127. No script depends on it.
  - Notes: `ComponentStore<C>` (ascending `EntityId` entries), `ComponentRegistry` as the type list `PhysicsBody, Controllable, Lifetime, Score, Team`, and `GameWorld` becomes ascending entity ids plus one store per registered component. `MatchState` and the bounded `WorldEvent` list move to Step 19 and Step 17 respectively, and `DeterministicRandom` to Step 19, because each is first consumed there; adding them here would be untested structure. `Player` disappears: a player is an entity with `PhysicsBody` and `Controllable`. `WorldSnapshot` is generated from the registry. Existing physics tests and fixtures pass unchanged because the seeded CSV entities carry exactly `PhysicsBody`. `@extension-point entity_component` documented in `src/simulation/README.md`.

- [x] **Step 16: Add commands and the canonical `InputBatch`**
  - Verify: `./scripts/verify-focused 'unit.simulation'`
  - Execution note (2026-09-06): Verified at 141 `unit.simulation` tests (up from 91) and 322 across all domains. Thrust direction is validated and carried verbatim, never clamped at construction, because ADR 0005 § "Steering" gives the magnitude clamp to the pre-kernel system and clamping twice would not be bit-identical. Commands are grouped by phase-0 application rank (despawn, spawn, then remaining kinds) so phase 0 is one forward pass; spawns address no entity and order by `ControllerId`. An unaccepted kind is rejected rather than dropped, and ADR 0003 § "Accepted simulation input" and ADR 0004 § "Commands" were corrected to match: the sink already refuses one at submission, so its arrival means the boundary and engine disagree about the running mode.
  - Notes: `command_registry.hpp` holds the closed variant `Command = SpawnCommand | DespawnCommand | ThrustCommand`, `CommandKind`, and `CommandKindMask`. `InputBatch::create(commands, accepted_kinds, EntityIdReservation)` canonicalizes to ascending id, keeps the last command of a kind per entity, rejects spawn-and-despawn for one entity, rejects unaccepted kinds and non-finite or out-of-range components. `@extension-point command_kind`.

- [ ] **Step 17: Make the tick a fixed kernel with named hook stages**
  - Verify: `./scripts/verify-focused 'unit.simulation|fixtures'` with every pre-existing physics and fixture test unmodified except for the added arguments.
  - Notes: Add the bounded `WorldEvent` list to `GameWorld`, then `SimulationSystem`, `TickContext`, `SystemStage`, and `SystemPipeline`, and give `GameSimulation` a `SystemPipeline` and `step(FixedDelta, const InputBatch&)`. Phase 0 applies despawns and records commands into `Controllable::commands_this_tick`; spawn seating waits for the spawn policy in Step 19 and a spawn command in this step is a recorded no-op with a named test. Phase 1 gains drag from `[simulation] drag_per_second`. The existing pair, wall, integrate, and reindex phases are untouched. `kPreKernel`, `kPostKernel`, and `kLifecycle` systems run in declared order around them, and events clear at commit. An empty pipeline, zero drag, and `InputBatch::empty()` must reproduce every accepted horizon bit-for-bit. The mode-injected form `create(configuration, map, mode)` arrives in Step 19. `@extension-point simulation_system`.

- [ ] **Step 18: Add the contact rule table, static bodies, and maps as values**
  - Verify: `./scripts/verify-focused 'unit.simulation'`
  - Notes: `ContactRule` (predicate and response function pointers), `ContactResponse` (two bodies plus events), `ContactRuleTable::built_in()` with dynamic-dynamic elastic and dynamic-static reflect rows evaluated first-match in row order in phase 3; existing pair tests must pass through the table with identical numbers. `PhysicsBody` gains `mass`, `collision_layer`, and `is_static`. `MapDefinition` (`ArenaBounds`, `static_bodies`, `Marker`s with `spawn` kind, metadata) replaces the world-size configuration as the arena source. `@extension-point contact_rule`, `@extension-point map_definition`.

- [ ] **Step 19: Add the match lifecycle engine and the `GameMode` interface**
  - Verify: `./scripts/verify-focused 'unit.simulation'`
  - Notes: `MatchState`, `MatchPhase`, `MatchOutcome`, `MatchLifecycleDurations`, `MatchObjective`, `SpawnPolicy`, `DeterministicRandom` (SplitMix64 as specified), and the abstract `GameMode` (`name`, `systems`, `contact_rules`, `accepted_command_kinds`, `spawn_policy`, `objective`, `validate_map`) live in `blob_simulation`; the engine-owned lifecycle system runs at `kLifecycle` with at most one transition per tick and writes `MatchSnapshot`. Phase 0 gains spawn seating through the engine `SpawnSystem` and the mode's `SpawnPolicy` over the map's markers, drawing ids from the tick's `EntityIdReservation`, and `GameSimulation::create` takes the map and the mode. Retire `GameWorld::EntitySeed` once spawning seats live entities. Tests use a minimal in-test mode. `@extension-point game_mode`.

### Phase 4 — Games in `blob_gameplay`

- [ ] **Step 20: Create `blob_gameplay` with the registry and the `sandbox` mode**
  - Verify: `./scripts/verify-focused 'unit.gameplay'`
  - Notes: New library depending only on `blob_simulation`; `game_mode_registry.hpp` maps mode names to factories; `SandboxMode` accepts thrust, uses built-in contact rules, seats every spawn at the next marker, never leaves `lobby`-equivalent free play. Its size is the seam's acid test: if it exceeds a few dozen lines, fix the interfaces in ADR 0004 before continuing. Tests mirror `src/gameplay` under `tests/unit/gameplay`.

- [ ] **Step 21: Implement `RoyaleMode` per ADR 0005**
  - Verify: `./scripts/verify-focused 'unit.gameplay|fixtures'` including a test that 100 fresh runs of a scripted multi-entity match replay produce bit-identical ordered snapshots, then `./scripts/run-benchmarks-linux` still passes.
  - Notes: `ZoneSystem` and `EliminationSystem` at `kPostKernel`, a rotating ring `SpawnPolicy`, a `MatchObjective` with the accepted durations, `[royale]` configuration, and the placement rules from ADR 0005. Introduce the replay fixture format `(map, mode configuration, seed, command log)` under `tests/fixtures/replays/` and make it the primary gameplay fixture.

### Phase 5 — Runtime, controllers, protocol, server, and application

- [ ] **Step 22: Add the runtime command mailbox, `CommandSink`, and entity id reservations**
  - Verify: `./scripts/verify-focused 'unit.runtime' linux-clang-asan-ubsan && git push origin main && ssh ubuntu-tailscale 'cd ~/Projects/blob-royale && git pull --ff-only && ./scripts/verify-focused unit.runtime linux-clang-tsan'`
  - Notes: `CommandMailbox` is a bounded, mutex-protected buffer inside `SimulationRuntime`; the worker swaps it out once per tick, hands the tick a contiguous `EntityIdReservation`, builds the `InputBatch` with the mode's accepted kinds, and calls `step`. `CommandSink` exposes `open_session() -> EntityId`, `submit(EntityId, Command)`, and `close_session(EntityId)` and nothing else. Add a runtime-owned `ControllerDirectory` keyed by `ControllerId` holding each controller's kind and display name, written when a session opens or a bot registers and read only at the encoding boundary, so proxy-supplied strings never enter the deterministic core (ADR 0004 amendment of 2026-09-06). Overflow drops the oldest command of the same kind for the same entity, never a spawn or despawn. TSan is native-only.

- [ ] **Step 23: Create `blob_controllers` with `ControllerHost` and the first bots**
  - Verify: `./scripts/verify-focused 'unit.controllers|unit.runtime'`
  - Notes: `Controller` (`kind`, `entity`, `decide(const Observation&)`), `Observation`, and `ControllerHost`, which at presentation cadence reads `const SnapshotPublication&` and submits each controller's decisions to `CommandSink&`, so the simulation cannot tell a bot from a human. First controllers: `wanderer` (seeded random thrust), `chaser` (thrust toward the nearest other controllable entity), and `scripted_replay` (drives a recorded command log for fixtures). `controller_registry.hpp` maps kinds to factories; personalities are constructor configuration. A runtime-level test seats two bots in `sandbox` and asserts both move. `@extension-point controller`.

- [ ] **Step 24: Implement protocol v2 encoding and decoding and regenerate client types**
  - Verify: `./scripts/verify-focused 'unit.protocol' && cd frontend-react && npm run generate:protocol && npm run generate:protocol:check`
  - Notes: Component encoders keyed by component kind (one per registered component, registered beside the component); the wire `controllable` object publishes `controller_kind` and `display_name` joined from the `ControllerDirectory`, not from the component. The `match` section, `welcome`, and command decoders per kind with golden bytes for every v2 example and rejection cases for extra members, non-finite values, oversized frames, unknown kinds, and a thrust outside `[-1, 1]`. Extend the generator to `docs/protocol/schema/v2`, producing `protocolV2Types.generated.ts` and `protocolV2Schemas.generated.ts`.

- [ ] **Step 25: Wire match configuration, maps, modes, and bots in the application**
  - Verify: `./scripts/verify-focused 'unit.application|fixtures' && ./scripts/run-linux-toolchain -- ./scripts/assert-native-process-smoke`
  - Notes: `[match]` section with `mode`, `map`, and `bots` (for example `wanderer:2, chaser:1`); `[simulation] drag_per_second`; `[royale]` per ADR 0005. `MapLoader` reads `maps/<name>/map.cfg` (bounds, metadata), `static_bodies.csv`, and `markers.csv` with the strict INI/CSV loaders; ship `maps/arena-960x640` reproducing today's arena, and make `--scenario` optional (a scenario still seeds extra entities for fixtures). `BlobRoyaleApplication` resolves the mode from `game_mode_registry`, validates the map through the mode, constructs `ControllerHost` from `controller_registry`, and passes `CommandSink&` and `const SnapshotPublication&` to the server. Update every checked-in configuration, the e2e fixture, and the fuzz corpus.

- [ ] **Step 26: Add the `/api/v2/session` route, session class, and trusted-proxy identity**
  - Verify: `./scripts/verify-focused 'unit.server|integration'`
  - Notes: `GameApiRouter` admits the v2 upgrade under the same host, origin, rate, and connection policies. `SessionWebSocketSession` sends `welcome`, decodes command envelopes, applies the per-session command bucket, forwards stamped commands to `CommandSink`, pushes v2 snapshots at presentation cadence, and calls `close_session` exactly once on any close path. Implement the Step 14 principal and display-name rules for peers in `trusted_proxy_addresses`, with trusted-proxy classification taking precedence over loopback classification so the proxy peer never inherits the direct-peer Origin relaxation. Validate at startup that static entities plus the roster ceiling cannot exceed the 1,024-entity snapshot bound. Integration test: two sessions and one bot join a `royale` match, a thrust from one session moves only that entity, a disconnect despawns it, and a forged entity id in a frame is ignored.

- [ ] **Step 27: Pass the full pull-request profile and update domain documentation**
  - Verify: `./scripts/verify-linux pr`
  - Notes: Update `src/*/README.md` (including the two new domains), the `simulation_input` bullet in ADR 0002 if any residue remains, `docs/operations/linux.md`, and `benchmarks/README.md` for the new construction path. Advisory on the Mac; Step 33 makes it authoritative.

### Phase 6 — Client

- [ ] **Step 28: Speak protocol v2 in `SimulationApi` and the connection hook**
  - Verify: `cd frontend-react && npm run typecheck && npm run lint && npm run test:ci`
  - Notes: Open the v2 session socket instead of v1, handle `welcome`, expose `myEntityId`, `match`, and `entities` in `useSimulationConnection` state, and add a `sendCommand` capability that is a no-op unless connected. Keep the reconnect budget; a reconnect is a new join by contract.

- [ ] **Step 29: Add the renderer registry, game renderers, and thrust input**
  - Verify: `cd frontend-react && npm run test:ci && npm run build`
  - Notes: `entityRendererRegistry` keyed by component kind (`@extension-point entity_renderer`): bodies as discs with `display_name` labels and own-entity highlight, static bodies as obstacles, the Royale zone from its `Zone` component; HUD for phase, countdown, alive count, placement; overlays for waiting, eliminated, winner, and draw. `useThrustInput` maps WASD and arrows to a unit direction, sends on change at most every 50 ms, sends zero on release, and ignores input while the own entity is absent.

- [ ] **Step 30: Add a two-player Chromium end-to-end flow with a bot present**
  - Verify: `./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`
  - Specialist: `testineer`
  - Notes: Two browser contexts join a `royale` match configured with one `wanderer` bot; all three entities render with names, thrust from one context moves only that entity, the zone radius decreases, and the existing server-loss recovery spec still passes.

### Phase 7 — Ship and play

- [ ] **Step 31: Redeploy to the tailnet as a Royale match with bots**
  - Verify: `ssh ubuntu-tailscale 'cd ~/Projects/blob-royale && git pull --ff-only && ./scripts/deploy-tailnet' && curl -fsS https://cole-ubuntu-pc.colobus-stargazer.ts.net:8444/api/v1/health/ready | rg -q '"status":"ready"'`
  - Notes: `deploy/ubuntu-pc/blob-royale.cfg` gains `[match] mode=royale map=arena-960x640 bots=wanderer:2`, `[simulation] drag_per_second=2.0`, `[royale]`, and `trusted_proxy_addresses=127.0.0.1`; drop the seeded scenario. The release profile inside the script is authoritative on this host.

- [ ] **Step 32: Playtest with at least two tailnet devices and the bots**
  - Verify: human review — `docs/playtests/2026-MM-DD.md` records participants, device types, a completed match with a winner, how the bots read, latency feel, and every defect found.
  - Notes: Do not fix balance or feel inside this plan; file follow-ups.

- [ ] **Step 33: Certify the shipped commit**
  - Verify: `gh run list --workflow quality.yml --branch main --limit 1 --json conclusion --jq '.[0].conclusion' | rg -q success && ssh ubuntu-tailscale 'cd ~/Projects/blob-royale && test "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" && grep -q "^verification_authority=authoritative$" out/release/current/publication.env'`
  - Specialist: `proofreader`
  - Notes: The deployed commit, `origin/main`, and the authoritative publication record must agree. Tick the nine remaining steps of the 2026-08-04 plan with a pointer to this evidence.

## Done criteria

- Two people on the tailnet complete a match at `https://cole-ubuntu-pc.colobus-stargazer.ts.net:8444` with one winner, and the playtest note exists.
- `quality/linux-authoritative` is green on `origin/main`, and `out/release/current/publication.env` on `cole-ubuntu-pc` says `authoritative` for the same commit that is running.
- ADR 0001 records the real host; ADR 0004 and protocol v2 are `Accepted`; ADR 0003 is amended; all existing fixtures still pass with zero drag.
- `blob_simulation` still depends only on the standard library, only the runtime worker mutates the simulation, and the server holds only `CommandSink&` and `const SnapshotPublication&`.
- No gate was skipped or weakened, Funnel is off, and the deployment is reproducible by rerunning `scripts/deploy-tailnet` at the certified commit.
- `sandbox` and `royale` both exist as `GameMode` implementations, at least two bot controllers play through the same `CommandSink` as browsers, and every ADR 0004 seam is tagged `@extension-point` with two implementations or a data example.
- Adding a mode, map, component, contact rule, command kind, or controller touches only new files plus its registry line; the kernel, other modes, and the server are unchanged by the additions made in this plan after Step 19.

**Amended 2026-09-06:** Swapped Steps 3 and 4 so the Colima resize precedes the first sanitizer verify; the original order asked a TSan lane to pass under the 2 GiB QEMU VM that the resize replaces.

**Amended 2026-09-06:** Steps 4 and 18 no longer run ThreadSanitizer on the Mac. Rosetta rejects the `personality(ADDR_NO_RANDOMIZE)` call TSan needs (`tsan_platform_linux.cpp:282` CHECK failure), so the local loop covers GCC and ASan/UBSan and the TSan lane runs natively on `cole-ubuntu-pc` and in CI.

**Amended 2026-09-06:** Host-side verifies in Steps 5, 7, and 28 use `grep` instead of `rg` because `cole-ubuntu-pc` has no ripgrep installed.

**Amended 2026-09-06:** Corrected the Phase 3 order. Step 17 referenced the map, mode, and spawn policy that Steps 18 and 19 create, so spawn seating and the mode-injected `GameSimulation::create` move to Step 19, and Step 17 keeps the staged tick, the event list, phase 0 despawns and command recording, and drag.

**Amended 2026-09-06:** Re-planned Phases 2 to 6 as Phases 2 to 7 on the ADR 0004 gameplay framework at the owner's request (extensible modes, maps, entity kinds, mechanics, and bots that act as users). The superseded steps remain struck through above; Steps 1 to 10 and their execution notes are unchanged.
