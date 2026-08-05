# Plan: Make Blob Royale feature-ready

**Goal:** Replace the broken prototype with a Linux-authoritative, deterministic, test-proven server/client foundation whose C++ design uses cohesive objects, value ownership, RAII, constructor injection, and one canonical production implementation, so gameplay feature work can begin safely.
**Out of scope:** New gameplay rules, player-command semantics, accounts/authentication, matchmaking, persistence, speculative parallel physics, choosing a project license for the owner, and treating a native macOS result as release evidence.

## Context

The evidence and recovery rationale are in [`docs/PROJECT_DEEP_DIVE.md`](../../docs/PROJECT_DEEP_DIVE.md). At the reviewed head, native declarations conflict, the React build and test harness fail, the scheduler has correctness and lifetime defects, mutable simulation state races network readers, and there is no Linux CI or executable C++ test suite. The racy scheduler and defective physics are not compatibility requirements; only explicitly documented fixture, input, and protocol contracts are preserved.

Linux is the release authority. Until the actual deployment host is recorded, the plan adopts a digest-pinned Ubuntu 24.04 LTS `linux/amd64` OCI environment, GCC as the primary compiler, and Clang/sanitizers as an independent check. Authoritative release evidence must run on a real Linux runner; a Linux container hosted by macOS is useful feedback but remains advisory. If the production distro, architecture, libc, or container policy differs, amend Step 1 before producing a release artifact—never by weakening Linux gates or adding Apple-specific behavior to production code.

Create React App is deprecated, and the React team explicitly supports migrating a client-only application to a build tool such as Vite. The frontend therefore gets one Vite/Vitest path, followed by a strict-TypeScript application cutover once the protocol is stable. See [React's CRA deprecation notice](https://react.dev/blog/2025/02/14/sunsetting-create-react-app), the [Vite guide](https://vite.dev/guide/), and the [Vitest guide](https://vitest.dev/guide/).

## Execution constraints

- **Professional OOP, not maximum class count.** Encapsulate mutable state and invariants in small cohesive objects; prefer composition, value semantics, RAII, explicit ownership, and concrete constructor injection. Use inheritance only for demonstrated substitutability. Keep stateless vector, integration, and collision equations as named pure functions.
- **One canonical production path.** The replacement core may coexist with the old engine only inside the unmerged migration sequence in Steps 11–21. Do not release or merge an intermediate dual-engine state; Step 21 cuts over and deletes every legacy implementation. No compatibility layer survives.
- **Single-writer simulation.** Only `SimulationRuntime` mutates `GameSimulation`; network code reads immutable, versioned snapshots. Parallel simulation work is deferred until native-Linux profiling proves a need and it reproduces the serial reference.
- **Hard, visible failures.** Invalid config, scenarios, protocol data, unavailable tools, sanitizer findings, timeouts, and zero-test runs fail explicitly. Do not add silent defaults, catch-all continuation, flaky-test retries, or platform fallbacks.
- **Reviewable history.** Keep mechanical renames, behavior corrections, and architecture changes in separate commits. Use `test:`, `fix:`, `refactor:`, `build:`, and `docs:` prefixes; once the baseline gate exists, every commit must remain Linux-green.
- **Autonomous execution after approval.** Approval to execute this plan accepts the decisions stated in Steps 1, 2, 10, and 18. Record them as accepted ADRs and continue without another design pause unless observed deployment facts materially conflict with those decisions.

## Steps

### Phase 1 — Establish authority and recover build signals

- [x] **Step 1: Accept the Linux runtime contract**
  - Verify: human review — `docs/architecture/0001-linux-runtime-contract.md` has status `Accepted` and records distro, architecture, compiler/libc, OCI or direct execution, TLS boundary, process supervisor, ports, signals, and the native-Linux release authority.
  - Specialist: `rigorous-architect`
  - Notes: Record the real deployment host when known; otherwise accept the reference baseline above. Approval to execute this plan accepts that baseline until superseded by observed production facts.

- [x] **Step 2: Accept the target architecture**
  - Verify: human review — `docs/architecture/0002-simulation-architecture.md` has status `Accepted`, a complete ownership/dependency diagram, considered alternatives, consequences, extension points, and all decisions named below.
  - Specialist: `rigorous-architect`
  - Notes: Select one-way targets `blob_simulation` (no Boost/JSON/logging/threads), `blob_runtime`, `blob_protocol`, `blob_server`, and `blob-royale`. `BlobRoyaleApplication` owns `SimulationRuntime` before `GameServer`; the runtime owns `GameSimulation`, one `std::jthread`, and `SnapshotPublication`; `GameSimulation` owns `GameWorld` and `SpatialGrid` and calls pure physics functions. `GameWorld` owns `Player` values in stable ID order; `Player` composes `PhysicsBody`; the grid stores `EntityId`. Reject a generic ECS, class-per-function strategies, ambient singletons, and a repaired dependency-graph scheduler.

- [x] **Step 3: Pin the canonical Linux toolchain**
  - Verify: `./scripts/run-linux-toolchain -- ./scripts/report-toolchain`
  - Notes: Add a digest-pinned `ci/linux/Dockerfile`, checksum/version-pinned CMake, Ninja, GCC, Clang, Boost, Node, and npm, plus a non-interactive wrapper usable from Linux and macOS. Add `CMakePresets.json` presets for GCC Debug/Release, Clang ASan+UBSan, and Clang TSan with isolated `out/build/<preset>` directories. The inner verifier must fail outside Linux; only CI executing on a native Linux host may publish the authoritative status.

- [x] **Step 4: Delete the incomplete benchmark surfaces**
  - Verify: `test ! -e benchmark-test.cpp && test ! -e frontend-react/src/Benchmark.js && ! rg "run_benchmark|Benchmark-Test|Benchmark\.js|benchmark-test\.cpp" CMakeLists.txt main.cpp src frontend-react/src frontend-react/package.json`
  - Notes: Remove the malformed native entry point, unused engine benchmark method, broken React component/route/import, and React Router if the benchmark was its only consumer. Do not replace them yet; Step 26 establishes a benchmark only after the canonical engine is correct.

- [ ] **Step 5: Restore the native executable baseline**
  - Verify: `./scripts/run-linux-toolchain -- ./scripts/verify-native-smoke`
  - Notes: Reconcile the two declaration/definition conflicts and make normal SIGINT/SIGTERM shutdown return success without attempting to repair scheduler behavior. The smoke script must clean-configure, build, run `blob-royale --help`, start with checked-in config/fixture on loopback, send SIGTERM, enforce a bounded deadline, and require exit code zero.

- [ ] **Step 6: Make the native build deterministic**
  - Verify: `./scripts/run-linux-toolchain -- ./scripts/verify-native-build`
  - Notes: Replace CMake globs and `target_link_directories` with explicit sources, imported targets, target-scoped C++20 features, and first-party warnings-as-errors. Pin test-only dependencies by immutable revision, mark vendored/system includes correctly, add `.clang-format` and a focused `.clang-tidy`, and expose compile commands. GCC and Clang must build every target from a clean tree without developer-installed dependencies or warnings.

- [ ] **Step 7: Add the native test foundation**
  - Verify: `./scripts/run-linux-toolchain -- ctest --preset linux-gcc-debug --output-on-failure --no-tests=error`
  - Specialist: `testineer`
  - Notes: Enable CTest, pin Catch2, create mirrored `tests/unit`, `tests/integration`, and `tests/fixtures` targets, and add initial vector, fixture-readability, test-discovery, and process-smoke assertions without pinning defective scheduler behavior. Rename the three current scenario files with `.csv` and retain them as inputs, not truth about the broken scheduler. A zero-test or skipped-test run must fail.

- [ ] **Step 8: Replace CRA and Jest with Vite and Vitest**
  - Verify: `./scripts/run-linux-toolchain -- ./scripts/verify-web`
  - Notes: Hard-cut the existing client build to Vite, Vitest, React Testing Library, ESLint flat config, and Prettier; delete `react-scripts`, Jest configuration, stock CRA assets, and unused dependencies. Keep the small existing application in JavaScript during this build-tool-only change; Step 22 performs one strict-TypeScript/domain-boundary rewrite after the protocol is fixed. Verification runs `npm ci`, lint with zero warnings, non-watch tests with at least one executed test, and a production build.

- [ ] **Step 9: Install the Linux-authoritative quality gate**
  - Verify: `./scripts/verify-linux pr`
  - Specialist: `testineer`
  - Notes: Add `.github/workflows/quality.yml` with one aggregate status named `quality/linux-authoritative`, executed on a real `linux/amd64` runner through the same pinned image and named scripts used locally. Cache dependencies, never test results; fail on unavailable tools or skipped checks; and keep optional macOS-native checks outside the aggregate. Require the Linux aggregate in branch protection when repository settings access is available. Add ASan+UBSan immediately; add TSan to the aggregate when Step 17 introduces owned concurrency.

### Phase 2 — Build and atomically cut over the deterministic core

Steps 11–21 are one expand/cutover/delete migration unit. They may be separate local commits for review, but must not be merged, released, or used for feature work until Step 21 removes the old engine, scheduler, server path, and configuration globals.

- [ ] **Step 10: Specify the simulation contract**
  - Verify: human review — `docs/architecture/0003-deterministic-simulation-contract.md` has status `Accepted` and defines units, fixed timestep, tick phase order, wall/pair tie-breaking, floating-point tolerance, fixture expectations, and explicitly excludes player commands.
  - Specialist: `rigorous-architect`
  - Notes: Use world-units/second, world-units/second-squared, exact fixed `dt`, stable `EntityId` ordering, and stored acceleration only. Define the simplest correct collision/wall policy needed for this game and expected outcomes for head-on, oblique, separating, simultaneous/equal-distance, wall-overshoot, and partition-boundary cases. The old racy interleavings and per-tick unit accident are not preserved.

- [ ] **Step 11: Build validated configuration and scenario boundaries**
  - Verify: `./scripts/verify-linux pr`
  - Notes: Add immutable `ApplicationConfig`, `ServerConfig`, and `SimulationConfig` plus one `ApplicationConfigLoader` and one `ScenarioLoader`. Parse CLI/INI/CSV into typed values before object construction; reject missing files, short rows, duplicate IDs, non-finite numbers, invalid bounds, zero/negative rates or dimensions, and unsafe allocations with descriptive typed errors. Use semantic lower-case keys in the replacement path and migrate checked-in files during the final cutover; do not add legacy aliases.

- [ ] **Step 12: Build the value-owned world model**
  - Verify: `./scripts/verify-linux pr`
  - Notes: Add regular values `Vector2`, `EntityId`, `PhysicsBody`, `Player`, and `GameWorld` under `src/simulation`. `GameWorld` owns players in deterministic ID order; players contain physics data and no mutex, scheduler, partition, JSON, or back-reference. Constructors/factories create only valid states. Do not add an entity base class or speculative `MapObject` replacement.

- [ ] **Step 13: Build the deterministic spatial grid**
  - Verify: `./scripts/verify-linux pr`
  - Notes: Add `CellCoord` and `SpatialGrid` whose value-owned cells store non-owning `EntityId` values with one canonical coordinate ordering. Prefer a deterministic rebuild per tick over incremental bidirectional membership until profiling proves it costly. Test corners, edges, exact boundaries, neighboring cells, out-of-range positions, non-divisible dimensions, stable candidate pairs, and no duplicate pairs.

- [ ] **Step 14: Implement the physics specification**
  - Verify: `./scripts/verify-linux pr`
  - Notes: Implement named pure functions for fixed-step velocity/position integration, wall resolution, collision detection, and two-body response. Preserve equal-distance candidates, reject separating overlaps, use stable pair ordering, correct tangential terms, handle overshoot, and reject non-finite results. Assert the accepted contract's wall, head-on, oblique, simultaneous, conservation, and high-speed cases; do not introduce strategy hierarchies without multiple real policies.

- [ ] **Step 15: Implement the canonical simulation tick**
  - Verify: `./scripts/verify-linux pr`
  - Notes: `GameSimulation::step(FixedDelta)` is the only mutable-world entry point. Its documented phases operate on stable state: apply stored acceleration, build canonical pairs, resolve each pair once, resolve walls, integrate, rebuild the grid, and commit the tick. Convert all current fixtures plus malformed/boundary cases into fixed-tick specifications. Require 100 identical ordered runs per toolchain; across compilers assert documented tolerances and physical invariants rather than accidental bit identity.

- [ ] **Step 16: Publish immutable simulation snapshots**
  - Verify: `./scripts/verify-linux pr && ! rg "boost::|json|mutex|condition_variable|jthread|thread" src/simulation --glob '*.{hpp,cpp}'`
  - Notes: Add immutable `WorldSnapshot` and `PlayerSnapshot` values with tick sequence and canonical ordering. Snapshot creation occurs only after a complete tick. Tests prove older snapshots do not change after later ticks and that every snapshot contains one coherent tick. Keep `blob_simulation` free of Boost, JSON, logging, network, and concurrency dependencies.

- [ ] **Step 17: Own the simulation runtime and publication**
  - Verify: `./scripts/verify-linux nightly`
  - Notes: `SimulationRuntime` owns one persistent `std::jthread`, one `GameSimulation`, and one stateful `SnapshotPublication` containing the atomically published `shared_ptr<const WorldSnapshot>`. Only the runtime thread mutates the simulation; readers receive retained immutable snapshots. Use `steady_clock`, exact chrono durations, stop tokens, and an explicit idempotent lifecycle; no transition allocates another clock. Test with latches/barriers and bounded deadlines under TSan, including repeated start/pause/stop, concurrent readers, and destruction while active.

- [ ] **Step 18: Specify the versioned network protocol**
  - Verify: human review — `docs/protocol/v1.md` and its machine-readable schemas have status `Accepted` and define every route, envelope, field, limit, cadence, error, and trust-boundary decision.
  - Specialist: `doddy`
  - Notes: Choose one canonical state path before creating server classes: bounded server-push WebSocket snapshots at an independent presentation rate, with at most one write in flight and stale pending state replaced by the latest. Expose only versioned config, liveness, readiness, and snapshot routes. Remove public start/pause and duplicate HTTP state; process lifecycle is internal. Use stable `data/error/meta` envelopes and request IDs. Player commands/authentication remain a future protocol version or extension.

- [ ] **Step 19: Implement the isolated protocol target**
  - Verify: `./scripts/verify-linux pr`
  - Notes: Create `blob_protocol` as the only Boost.JSON encoding/decoding boundary. Encode snapshots/config/errors against the accepted schemas with deterministic ordering and explicit version metadata; validate golden examples and rejection cases. Domain values must not serialize themselves, and frontend types later derive from the same machine-readable schemas rather than hand-maintained duplicates.

- [ ] **Step 20: Rebuild the server against read-only publication**
  - Verify: `./scripts/verify-linux pr`
  - Notes: Build `blob_server` around `GameServer`, `TcpListener`, `HttpSession`, `SnapshotWebSocketSession`, and `GameApiRouter`, constructor-injected with `const SnapshotPublication&` and immutable server config—never a mutable simulation/runtime capability. Preserve the useful Beast body, timeout, strand, and response-queue bounds; return immediately after errors; own all timers/sessions; and prove bounded shutdown. Default to loopback, enforce route/method/origin policy, cap connections/messages/rate, reject unexpected application frames, and document reverse-proxy TLS.

- [ ] **Step 21: Cut production to the new composition root**
  - Verify: `./scripts/verify-linux release && ! rg "dependency_graph_queue|CycleDependency|LockedDependencyQueue|GameEngine|get_instance|GamePiece|MapObject|Partition|QueueOperationResults|game_engine_parameters|my_|helpers\.(hpp|cpp)|new std::thread|\.detach\(" CMakeLists.txt main.cpp src`
  - Specialist: `makeover`
  - Notes: `BlobRoyaleApplication` becomes the composition root, owning config, `SimulationRuntime`, then `GameServer` in destruction-safe order; `main()` only loads config, constructs, translates top-level errors, and runs. Start the runtime as process policy, coordinate SIGINT/SIGTERM shutdown, and return success. Switch CMake and checked-in config/scenarios to the replacement targets, then delete the singleton engine, global constants, entity hierarchy, partitions, scheduler, Boost umbrella target, old server, hard-coded paths/addresses, and every migration adapter in this same cutover. Git history is the archive.

### Phase 3 — Recover the client and certify the whole product slice

- [ ] **Step 22: Build one typed React connection boundary**
  - Verify: `./scripts/verify-linux pr`
  - Notes: Convert the client once to strict TypeScript and organize functional components under `frontend-react/src/features/simulation`. Centralize config fetching, endpoint validation, schema validation, WebSocket ownership, bounded reconnect/backoff, connection/error state, and cleanup in one `SimulationApi`/`useSimulationConnection` boundary. Derive immutable types from the protocol schemas, reject malformed/oversized frames before rendering, remove Axios/timer polling/class-shaped transport models, and keep canvas/debug components presentational.

- [ ] **Step 23: Add server integration and browser E2E tests**
  - Verify: `./scripts/verify-linux pr`
  - Specialist: `testineer`
  - Notes: A CTest fixture launches the release-layout server on loopback with an ephemeral port, waits on readiness, validates config/snapshot/error schemas and monotonic complete ticks, exercises wrong methods/paths/origins/frames, then sends SIGTERM and proves clean exit. Add one blocking Playwright Chromium flow that loads production assets, connects, renders successive snapshots, handles server loss, and reconnects. Firefox/WebKit remain advisory until stable; do not invent gameplay input.

- [ ] **Step 24: Enforce sanitizer, abuse, and dependency gates**
  - Verify: `./scripts/verify-linux nightly && ./scripts/verify-linux release`
  - Specialist: `doddy`
  - Notes: Keep ASan+UBSan blocking on pull requests and TSan blocking after the runtime cutover. Add deterministic regression corpora/fuzz targets for CLI/INI/CSV/HTTP/protocol parsers, repeated lifecycle/read/shutdown stress, connection churn, slow-client/backpressure checks, and bounded soak tests. Block shipped high/critical dependency findings, generate a release SBOM, and fail when a sanitizer/fuzzer is unavailable. Extended fuzz/soak execution may be nightly; its regression corpus remains a pull-request gate.

- [ ] **Step 25: Add observability, operations, and legal hygiene**
  - Verify: `./scripts/verify-linux pr && git diff --check`
  - Notes: Emit structured JSON logs with severity, event, request/connection ID, lifecycle state, and tick where relevant; remove colorized hot-loop chatter and unsafe `std::cout` ownership. Update root, simulation, runtime, server, frontend, protocol, and Linux operations READMEs with canonical build/run/shutdown/debug flows and explicitly marked extension points. Restore RapidCSV's required license text, add `THIRD_PARTY_NOTICES`, and state that project code remains all-rights-reserved until the owner explicitly chooses a license.

- [ ] **Step 26: Establish a truthful Linux performance baseline**
  - Verify: `./scripts/run-benchmarks-linux`
  - Specialist: `wolf`
  - Notes: Add a registered `blob_simulation_benchmarks` target against `GameSimulation::step()` only. Measure deterministic tick throughput by player count/density, candidate pairs, grid rebuild, snapshot creation, JSON encoding, peak memory, and delivery backpressure; every case verifies its final snapshot hash. Store native-Linux hardware/toolchain metadata, keep shared-runner comparisons advisory, and require a dedicated Linux runner before enforcing regression budgets. Never reintroduce a second engine or clock-driven benchmark path.

- [ ] **Step 27: Remove residue and certify feature readiness**
  - Verify: `./scripts/verify-linux release && git diff --check && ! rg "dependency_graph_queue|CycleDependency|LockedDependencyQueue|GameEngine|get_instance|GamePiece|MapObject|Partition|QueueOperationResults|option1|option2|my_|helpers\.(hpp|cpp)|react-scripts|Benchmark-Test|new std::thread|\.detach\(" CMakeLists.txt main.cpp src frontend-react/package.json frontend-react/src`
  - Specialist: `proofreader`
  - Notes: Delete obsolete sources, CMake entries, scripts, dependencies, stock assets, comments, and migration adapters; update every ADR/README to final names; and verify a clean clone through the release tier. Record genuinely deferred gameplay work as product/design backlog, not code TODOs. Run optional native macOS checks only after Linux passes, and treat discrepancies as portability reports rather than authority to weaken Linux behavior.

## Done criteria

- `quality/linux-authoritative` and `./scripts/verify-linux release` pass on a real Linux runner from a clean checkout and produce a runnable Linux server artifact plus frontend production assets. Warnings, unavailable tools, zero tests, skips, retries, sanitizer findings, and untriaged shipped high/critical dependencies are failures.
- `blob_simulation` has no Boost, JSON, logging, network, mutex, condition-variable, or thread dependency. One `GameSimulation::step()` owns deterministic mutation; one `SimulationRuntime` owns and joins the clock and snapshot publication; every reader sees a complete immutable snapshot.
- No mutable engine globals, singleton access, raw owning pointer, detached thread, recursive worker, entity-level lock, shared-ownership cycle, custom scheduler, alternate production engine, incomplete benchmark, or public global-lifecycle endpoint remains.
- Fixed inputs produce stable ordered snapshots across repeated native-Linux runs; configuration, parsing, physics, spatial indexing, lifecycle, protocol, server integration, and Chromium E2E behavior have executable assertions and applicable sanitizer coverage.
- The repository has one documented Linux build/run/shutdown path, a versioned state protocol, structured observability, current domain READMEs/ADRs, third-party notices, and a clearly documented seam for the first gameplay/player-command feature.
- Changes are organized into bisectable local commits; Steps 11–21 land as one reviewed migration unit. Pushing or opening a pull request remains a separate explicit publication action.
