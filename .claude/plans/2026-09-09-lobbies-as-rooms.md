# Plan: Lobbies as rooms

**Goal:** A player opens the site, sees the rooms, joins one, plays a full lobby-to-match cycle with people and bots, and leaves back to the directory; the six review findings are closed first, as their own commits, before any room exists.
**Out of scope:** Everything ADR 0006 § "What is deliberately not built" lists: dynamic room creation, naming and reaping; matchmaking, a host role, a ready check; cross-match identity or persistence; one socket per player with a re-targeted stream; per-room maps and modes; spectators and chat; a match time limit; a metrics endpoint; consolidating the room workers into one loop; widening any rate bound.

## Context

The design is `docs/architecture/0006-lobbies-as-rooms.md` (Proposed; the owner agreed all four
decisions on 2026-09-09). The findings are `docs/reviews/2026-09-08-lobby-and-hazard-review.md`,
whose appendix carries three reproduction tests that become regression tests here. This plan also
executes Steps 6 to 10 of `.claude/plans/2026-09-08-operable-lobby-with-seats.md`, which are struck
through there and point here.

Facts the executor needs that the code does not say on its face, all verified on 2026-09-09:

- The replay fixtures assert named invariants, not digests (`tests/fixtures/replay_fixture.cpp`,
  no `digest` anywhere), so changing who sits in a seat changes no recorded value; only phase ticks
  and entity counts are pinned.
- `tests/fixtures/replay_fixture.cpp:305-363` parses `spawn`, `despawn`, `thrust`, and the four
  lobby kinds from a CSV whose columns already include `controller_id` and `seat_index`; `join`
  and `leave` need no new column.
- `GameSimulationSetup::engine_defaults().with_map(map)` (`src/simulation/game_simulation_setup.hpp:58`)
  gives a mode-less simulation a map with spawn markers, which the seat-ceiling test needs because
  `lobby_command_tests.cpp` runs on a bare arena with none.
- `ControllerHost` has `add` and `size` and no `remove` (`src/controllers/controller_host.hpp`);
  bot reconciliation needs one.
- `ServerConfig::create`'s third argument is `snapshots_per_second`, minimum 1; the review's
  session reproduction uses 1 to widen the race window to a second.
- The benchmark runs only on native x86_64 (`scripts/run-benchmarks-linux:44-49`) and there is no
  baseline from `cole-ubuntu-pc` in the tree; the JSON follows schema `blob-royale-benchmark-v1`
  with `nanoseconds.median` per measurement and `name` per scenario.
- `BlobRoyaleApplication` treats a failed runtime as a process-terminal wake reason
  (`src/application/blob_royale_application.cpp:231-233`); rooms change that boundary.
- HEAD is not deployable as a playable game until Phase 5 passes; `deploy/ubuntu-pc/blob-royale.cfg`
  already says `lobby_seat_count=4`.

## Owner decisions (2026-09-09)

- Rooms are a fixed pool of `SimulationRuntime`s, one thread each; a socket per join with an HTTP
  directory; server-issued `join` and `leave` commands; configuration creates rooms and nothing
  reaps them. Protocol 2.4, one bump, in Phase 4.
- **The review fixes land first, as their own commits, before any rooms commit.**

## Execution constraints

- **Phases 0 and 1 change no wire byte.** `join` and `leave` are server-issued (`CommandWireKind`
  is `nullopt`), and `docs/protocol/v2.md` § "Versioning and fail-closed decoding" makes only a
  *client* command kind a minor. The one bump is 2.4 in Step 12; every "one minor ahead" case
  moves 2.4 -> 2.5 on both sides there, following the 2.1 to 2.3 procedure.
- **The baseline does not move.** `AcceptedBaselineTick`, `kMigratedPairFixtureHorizon`, and
  `bodies_are_bit_identical` keep their exact values and `git diff --stat -- maps/` stays empty.
  A replay fixture's phase ticks and entity counts stay where they are or are re-derived with a
  written reason in the fixture's own comment, never regenerated to make a test pass.
- **Command ranks.** `join` ranks after every existing kind and `leave` after `join`, so no
  fixture's canonical order moves and a join and a leave in one batch net to nothing.
- **Verify on both lanes** (`linux-gcc-debug` and `linux-clang-asan-ubsan`), format with `find`,
  read CI after every push, stage explicit paths, serialize builds, and re-run every gate a
  behaviour change can reach: anything that changes who is seated or alive re-runs the browser
  flows, because they are the only lane that counts entities end to end.
- **The tick budget decides `[lobbies] count`, not the design.** If Step 8's measurement fails the
  budget, lower the deployed count and record why; do not consolidate the workers.
- **No deploy at HEAD until Step 16 passes.** Everything before it leaves the tailnet on `2b7ecd2`.

## Steps

### Phase 0 — Review fixes that need no new mechanism

- [x] **Step 1: Gate the lethal row on `running`**
  - Verify: `./scripts/verify-focused 'unit.gameplay|fixtures' && ./scripts/verify-focused 'unit.gameplay|fixtures' linux-clang-asan-ubsan && ./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`
  - Notes: Review finding 2. `body_is_lethal_hazard` (`src/gameplay/shared/lethal_hazard_contact_rule.cpp:15`) also requires `world.match().phase == kRunning`; a predicate may read the world (ADR 0004 § "Contact rules"). The row, not the recorder, because gating `placement_recorder` would leave an `EliminationEvent` nobody consumes, which hides a producer bug, and destroying hazards at `running -> ended` would make a boulder vanish mid-screen. Outside `running` the pair falls through to `variable_impulse`, so a comet shoves during `ended` instead of killing; the test asserts the player survives, no placement is appended, and the impulse row is the one that matched. Turn the review's appendix B into that test. Amend ADR 0005 lines 454 and 476 with a dated note.

- [x] **Step 2: Bound the seat count by the map's spawn markers**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.gameplay|fixtures' && ./scripts/verify-focused 'unit.simulation|unit.gameplay|fixtures' linux-clang-asan-ubsan`
  - Notes: Review finding 3. `GameSimulation::step` passes `map_.spawn_points().size()` into `apply_input_batch`, and `apply_lobby_command` refuses a `set_seat_count` above it as a no-op, exactly like the shrink past an occupant. Not a `MatchState` member: the ceiling is a map fact, and a member would grow every snapshot for a number the map already owns. `validate_map` keeps guarding the configured count at startup. Turn appendix C into the regression test (expect the count unchanged) using `engine_defaults().with_map(gameplay_map(4))`. `welcome.seat_count_maximum` is Step 12's, not this step's.

- [ ] **Step 3: Seed the roster only for a mode with a lobby, and fix the stale runbook lines**
  - Verify: `./scripts/verify-focused 'unit.application' && ./scripts/verify-focused 'unit.application' linux-clang-asan-ubsan && test "$(grep -c 'lobby minimum' docs/operations/tailnet.md scripts/reconfigure-tailnet | awk -F: '{s+=$2} END {print s}')" = 0`
  - Notes: Review finding 5. `blob_royale_application.cpp:300` seeds `SeatRoster::of_size` only when `mode->accepted_command_kinds().contains(kStartMatch)`; `[royale]` stays required for every mode, only the seeding is conditional. The application tests already run `sandbox` on a bare arena, so the test is "the published roster is empty". Rewrite `docs/operations/tailnet.md:65` and `scripts/reconfigure-tailnet:10`, which still say "lobby minimum".

- [ ] **Step 4: Log every accepted lobby command at the session**
  - Verify: `./scripts/verify-focused 'unit.server' && ./scripts/verify-focused 'unit.server' linux-clang-asan-ubsan`
  - Notes: Review finding 6. After the submit in `admit_client_command` (`src/server/session_websocket_session.cpp:308`), one `info` event `session.lobby_command` per lobby kind with the request id, the kind, the closed payload values, and `command_submission_result_name`. Nothing client-chosen reaches the line except a bounded integer or a registered kind name, both already validated. Test with the existing `SessionHarness`: handshake, write one `start_match` frame, run until the event appears.

### Phase 1 — Join and leave: the mechanism, and the two lifecycle findings

- [ ] **Step 5: Add the `leave` command and retire by controller**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.runtime|unit.server|fixtures' && ./scripts/verify-focused 'unit.simulation|unit.runtime|unit.server|fixtures' linux-clang-asan-ubsan`
  - Notes: Review finding 1. `LeaveCommand{controller}` is a variant alternative, enumerator, `CommandKindName` `leave`, `CommandKindOf`, the last application rank, an `addressed_identity_of` arm (controller), `CommandWireKind` `nullopt`, and `true` in `is_entity_lifecycle_command` with its static assertion moved to 8. Phase 0 destroys every entity whose `Controllable` names the controller, pending or seated, and clears any `ControllerSeat` it holds (total over the roster even though nothing writes one until Step 6). `CommandSink::close_session` submits the leave into the mailbox it fronts **before** retiring the directory entry, so a session need not know its entity at all; `leave_match` stops submitting a despawn and `current_controlled_entity_` goes away. The replay loader parses `leave`. Turn appendix A into the regression test: the world holds no entity for the retired controller and the mailbox saw exactly a spawn and a leave. Amend v2.md § "Entities, controllers, and what survives what" so that a close is described as a leave; no version.

- [ ] **Step 6: Add the `join` command: people take seats on admission, bots take their declared seats, a person displaces an NPC**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.runtime|unit.controllers|unit.application|integration' && ./scripts/verify-focused 'unit.simulation|unit.runtime|unit.controllers|unit.application|integration' linux-clang-asan-ubsan`
  - Notes: Old plan Steps 6 and 7. `JoinCommand{controller, std::optional<seat_index>}`, server-issued, ranked after every client kind and **before** `leave`, so a join and a leave in one batch net to nothing rather than seating a retired controller. Phase 0: a join with no seat takes the lowest `EmptySeat`, else displaces the lowest-indexed `NpcSeat` (owner's 2026-09-08 decision), else changes nothing; a join naming a seat fills it only if that seat is an `NpcSeat` with no controller; a controller already seated is a no-op. `CommandSink::open_session` takes a seat request (`any seat` for a person, `declared seat N` for a bot) and enqueues the join. `[match] bots` stops being a startup roster and becomes the initial declarations of the first seats, so `seat_configured_bots` is deleted and bots exist only through seats; the count above the seat count is a startup rejection. Reconciliation runs on the control loop's 25 ms poll: for every `NpcSeat` with no controller and no creation already in flight, open a bot session with the declared seat, create the controller, add it to the host; for every hosted bot whose controller sits in no seat, close its session (Step 5's leave) and remove it from the host, which needs `ControllerHost::remove`. Idempotent because the seat carries the controller; a per-seat "creation in flight" set covers the tick between enqueue and observation. A person whose join changes nothing is admitted and spawns exactly as today; the `lobby_full` close is Step 13's. Integration test: a session's controller appears in a `ControllerSeat` within a second of joining.

- [ ] **Step 7: `can_start` waits for a bot that exists, and the fixtures say who sat down**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.gameplay|fixtures' && ./scripts/verify-focused 'unit.simulation|unit.gameplay|fixtures' linux-clang-asan-ubsan && ./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`
  - Notes: Review finding 4. `seat_is_filled` answers `controller.has_value()` for an `NpcSeat`; the wire already renders the null as "joining". Each replay fixture's `seat_npc` rows become `join` rows for the controllers that spawn on the same tick, so the roster holds the players themselves rather than declarations nobody creates, and the fixture states one fact instead of two. Every phase tick must stay where it is (`royale-spawn-order` keeps its tick-2 start) because a join applied at phase 0 is observed at the same `kLifecycle` a `seat_npc` was; measure, and write the reason beside each row. `BlobRoyaleLobbyDriver` now seats only the empty seats, since persons take seats on admission, and the browser flows' entity and player counts are re-derived for bots that now exist. This is the step most likely to need an amendment; stop and surface rather than regenerate.

### Phase 2 — Measure before multiplying

- [ ] **Step 8: Add a royale benchmark case and measure the tick on `cole-ubuntu-pc`**
  - Verify: `ssh ubuntu-tailscale 'cd ~/Projects/blob-royale && git pull --ff-only && ./scripts/run-benchmarks-linux && jq -e "[.scenarios[] | select(.name == \"royale_deployed_roster\")] | length == 1" out/benchmarks/blob-simulation-benchmark.json'` and the recorded median step time is at or under 250 µs with a p99 at or under 1 ms; adjust the jq path to the document's actual shape rather than the document to the path.
  - Notes: ADR 0006 § "The tick-loop decision". `benchmarks/CMakeLists.txt` links `blob_gameplay`; the case builds `RoyaleMode` from the deployed `[royale]` and `[hazard.*]` values on the 32-marker arena, joins and spawns eight controllers, presses Start, advances through countdown, then times 4,000 running ticks with empty batches so both hazard kinds spawn. Time `step` and `snapshot` separately. Record median and p99 in the ADR as a dated amendment. Decision rule: within budget keeps `[lobbies] count=4` deployed and 8 compiled; over budget lowers the deployed count so `count × p99 ≤ 1.25 ms` and says so in the ADR. A single room over budget means an entity or hazard bound is wrong, not the loop.

### Phase 3 — The runtime learns to be late

- [ ] **Step 9: Bounded catch-up and overrun accounting in `SimulationRuntime`**
  - Verify: `./scripts/verify-focused 'unit.runtime|unit.application' && ./scripts/verify-focused 'unit.runtime|unit.application' linux-clang-asan-ubsan`
  - Notes: Review observation P1. The deadline policy becomes a pure function in `src/runtime/tick_deadline.hpp`, `advance_tick_deadline(previous, now, quantum, kMaximumCatchUpTicks) -> {deadline, rebased, ticks_behind}`, unit-tested with literal time points, and `simulation_runtime.cpp:250` calls it. `kMaximumCatchUpTicks = 4` in `runtime_limits.hpp` with the reason written there: 10 ms, well under a CFS throttle window, and a re-based clock runs slow rather than sprinting. `TickStatistics` (tick count, overruns, rebases, maximum step and lateness in nanoseconds) beside the mailbox statistics; the control loop logs `runtime.tick_overrun` and `runtime.clock_rebased` at warning when they rise, the same shape as `observe_dropped_commands`. Tick numbers never skip; say so in the header.

### Phase 4 — Rooms

- [ ] **Step 10: Configuration: `[lobbies] count`, `[match] lobby_seat_count`, per-client accounting**
  - Verify: `./scripts/verify-focused 'unit.application|unit.gameplay|fixtures|integration' && ./scripts/verify-focused 'unit.application|unit.gameplay|fixtures|integration' linux-clang-asan-ubsan && test "$(grep -rl 'lobby_seat_count' src/gameplay | wc -l)" = 0`
  - Notes: A new fixed section `[lobbies]` with one required key `count` in `[1, kLobbyDirectoryLimit]`, the constant living in `protocol_v2_constants.hpp` (it bounds the directory's `maxItems`) at 8. `lobby_seat_count` moves from `[royale]` to `[match]` because it is a fact about who is playing, not a balance number: `MatchConfiguration` gains it, `RoyaleConfiguration` loses it, and the marker check leaves `RoyaleMode::validate_map` for `match_startup_validation` as `require_lobby_fits_map`, since "a marker per seat" is true of any mode with a lobby. About forty files name the key (grep before editing); this is a rename with no behaviour and lands as one commit. The deployed config gains `[lobbies] count=4` and `trusted_proxy_addresses=127.0.0.1`, keeps four seats, and the runbook's "Limits behind the proxy" is rewritten for per-client accounting with the local-forgery residual stated once more. Every test and e2e configuration gains `[lobbies] count=1`.

- [ ] **Step 11: N runtimes in the composition root, room 1 still the only route**
  - Verify: `./scripts/verify-focused 'unit.application|unit.server|integration' && ./scripts/verify-focused 'unit.application|unit.server|integration' linux-clang-asan-ubsan`
  - Notes: A `Room` owns `lobby_id`, its `SimulationRuntime`, its `ControllerHost`, its reconciliation state, and its `MatchSessionContext`; the application holds a vector sized by `[lobbies] count`, seeded with `seed + (lobby_id - 1)`, each validated against the snapshot bound with `kMaximumLobbySeatCount` bots counted. The server receives a `LobbyDirectory` (per room: id, `const SnapshotPublication&`, context, an atomic session count) instead of one publication and one context; the routes still serve room 1 only. The control loop iterates rooms for dropped commands, overruns, controller passes, reconciliation, and a `match.phase_changed` line whenever a room's committed phase differs from the last one seen. Abandonment: a room with zero sessions in `countdown` or `running` has its bots closed, which ends the match by attrition. A failed runtime marks its room failed and logs `runtime.failed` with the exception; its sessions close themselves within a presentation period because `is_ready()` is already false; the process keeps serving and readiness reports room 1. `StructuredLogEvent` gains `lobby_id`, set on every room-scoped line.

- [ ] **Step 12: Protocol 2.4**
  - Verify: `./scripts/verify-focused 'unit.protocol' && ./scripts/verify-focused 'unit.protocol' linux-clang-asan-ubsan && cd frontend-react && npm run generate:protocol && npm run generate:protocol:check && npm run validate:protocol-examples && npm run test:ci`
  - Specialist: `doddy` — threat cartography rows for the two new targets, the parametric segment, and the room-full oracle, written into v2.md before the code.
  - Notes: `lobby-directory.schema.json` (bounded at `kLobbyDirectoryLimit`, carried in the v2 envelope) with per-room `lobby_id`, `mode`, `map`, `phase`, `phase_started_tick`, `tick_sequence`, `seat_count`, `seat_count_maximum`, `filled_seat_count`, `npc_seat_count`, `session_count`, `healthy`; `welcome.lobby_id` and `welcome.seat_count_maximum`, both required; error codes `LOBBY.NOT_FOUND` (404), `LOBBY.FULL` (409), `LOBBY.UNAVAILABLE` (503); close reason `lobby_full` on 1013; `protocol_version` 2.4; examples; the encoders; v2.md's targets table, limits table, close-code table, versioning history, and the removal of "multiple rooms" from § "Explicitly out of scope"; the client's generated types and welcome validation. Bump once, here.

- [ ] **Step 13: The directory route, the room route, and admission**
  - Verify: `./scripts/verify-focused 'unit.server|integration' && ./scripts/verify-focused 'unit.server|integration' linux-clang-asan-ubsan`
  - Notes: `GET /api/v2/lobbies` reads each room's latest snapshot and session count under the ordinary HTTP bucket. `GET /api/v2/lobbies/<lobby_id>/session` is the router's one parametric target: grammar `[1-9][0-9]{0,2}`, the segments on either side matched exactly, anything else `404 LOBBY.NOT_FOUND`; `/api/v2/session` is room 1 and the v1 routes read room 1. Admission refuses `409 LOBBY.FULL` when the room's session count already equals its seat count and `503 LOBBY.UNAVAILABLE` when its runtime is failed or not ready. A session is constructed with its room's context and counts itself in and out of the room. The one join the tick can still refuse is the race for the last seat: a session that observes a full roster without its own controller after the welcome closes with `lobby_full`. Integration test: two rooms, a session in each, the directory shows both, a join to a full room is 409, to an unknown id 404, and every string that is not a room is 404.

### Phase 5 — The client

- [ ] **Step 14: The directory view, and joining and leaving a room**
  - Verify: `cd frontend-react && npm run typecheck && npm run lint && npm run test:ci && npm run build`
  - Notes: Load the `site-layout-design` skill first: this step decides the shell, where the directory poller and the room socket live so they survive navigation, and how drill-in and back-out work. `SimulationApi` gains `fetchLobbies()` (1 Hz while the directory is visible, stopped when hidden) and a per-room session URL; `useSimulationConnection` takes a `lobbyId`; Leave disposes the socket, returns to the directory, and fetches it once immediately. A `404`, `409`, or `503` envelope renders its message on the directory and does not retry; transport failures keep the existing backoff. Tests: reducer and selectors for the directory, join, and leave; the directory response validated against the generated schema.

- [ ] **Step 15: The lobby view inside a room, and the end of the e2e driver**
  - Verify: `cd frontend-react && npm run typecheck && npm run lint && npm run test:ci && npm run build && cd .. && ./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`
  - Notes: Old plan Step 8, built exactly as its notes describe (seat grid, own seat marked, right-click and keyboard NPC menu from `welcome.npc_controller_kinds`, Escape and outside-click dismissal, no fight with the canvas), with two facts this plan adds: the seat-count control is floored one above the highest filled seat and capped at `welcome.seat_count_maximum`, and `set_seat_count` is debounced because a drag would exhaust the 30-token burst (review finding 8). Start is enabled exactly when every seat is filled and every NPC seat has its controller. Replace the overlay copy at `sessionSelectors.ts:306`. Delete `frontend-react/e2e/BlobRoyaleLobbyDriver.ts` and make the existing browser flows operate the lobby through the UI.

- [ ] **Step 16: Browser proof across two rooms**
  - Verify: `./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`
  - Notes: Old plan Step 9, widened to rooms. One flow: browser A joins room 1 and seats a bot; browser B sees room 1 occupied and joins room 2; each starts its own match and both reach `running` independently; A leaves and lands on a directory that shows room 1 back in `lobby` within the restart delay, which is the abandonment rule observed end to end. Assert Start is disabled while a seat is empty and while a bot is still joining, because those are the rules most likely to regress silently.

### Phase 6 — Ship

- [ ] **Step 17: Accept ADR 0006 and amend the documents it touches**
  - Verify: `grep -q '^\* \*\*Status:\*\* Accepted' docs/architecture/0006-lobbies-as-rooms.md && ! grep -q 'Multiple rooms, matchmaking' docs/protocol/v2.md` and human review of the amendments.
  - Notes: Status to Accepted with Step 8's measured numbers in § "The tick-loop decision". ADR 0002 § "Ownership and lifecycle" gains a dated amendment: the composition root owns N runtimes and the server holds N publications through the directory. ADR 0001's consequences record per-client accounting enabled and the residual. `src/application/README.md`, `src/server/README.md`, and `src/runtime/README.md` describe rooms, the two commands, and the deadline policy. The 2026-09-08 plan's Steps 6 to 10 are already struck through and pointing here; tick them there with an execution note citing this plan.

- [ ] **Step 18: Deploy and playtest with two rooms in use**
  - Verify: `ssh ubuntu-tailscale 'cd ~/Projects/blob-royale && git pull --ff-only && curl -fsSI https://snapshot.ubuntu.com/ >/dev/null && ./scripts/deploy-tailnet'`, then the container, the `blob-royale.deployed-commit` label, readiness, the served bundle's protocol version `2.4`, and `GET /api/v2/lobbies` listing four rooms, each checked independently of the script's own report.
  - Notes: Old plan Step 10. The mirror pre-flight is the one cheap lesson of the 2026-09-08 outage and costs nothing; the release profile still takes about ninety minutes and refuses to publish anything uncertified, so a failure leaves `2b7ecd2` serving. Record the deployed commit and release id in a playtest note under `docs/playtests/` with two rooms in play, and note whether the abandonment rule and the join refusal were observed.

## Done criteria

- `docs/reviews/2026-09-08-lobby-and-hazard-review.md` findings 1 to 6 each have a regression test that fails on `c05b770` and passes at HEAD, landed before the first rooms commit.
- Two browsers in two rooms each play a complete lobby-to-match cycle, and leaving returns to the directory rather than disconnecting.
- A person takes a seat on admission, a bot exists for every declared NPC seat, a person displaces a bot in a full lobby, and Start waits for every bot to exist.
- The accepted physics baseline and every replay horizon are unmodified or re-derived with a written reason; `git diff --stat -- maps/` is empty across the whole plan.
- The tick budget is measured on `cole-ubuntu-pc` and recorded in ADR 0006, and `[lobbies] count` agrees with it.
- The full pull-request profile is green on a native runner for every commit, and the deployment serves protocol 2.4 with four rooms in the directory.
