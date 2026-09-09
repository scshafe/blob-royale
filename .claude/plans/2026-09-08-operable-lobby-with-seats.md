# Plan: An operable lobby with seats

**Goal:** Before a match, players see a lobby of seats — count selectable, default 4 — can right-click an empty seat to fill it with an NPC, and press **Start Game** once every seat is full.
**Out of scope:** Spectators, a host or ready-check (the owner chose "anyone in the lobby" on 2026-09-08), rejoining a match in progress, per-seat teams or colours, changing the seat count while a match runs, and any lobby in `sandbox` (it stays free play with no match at all).

## Context

Four facts from the tree on 2026-09-08 decide the shape.

**There is no seat concept today.** A session connects, `CommandSink::open_session` issues a `ControllerId`, and the mode's `SpawnPolicy` seats a blob. Who is "in the lobby" is derived — `RoyaleObjective::can_start` is "the alive count is at or above `[royale] lobby_minimum_players`". Bots come from `[match] bots=wanderer:1`, seated once at startup by `BlobRoyaleApplication::seat_configured_bots`.

**The command vocabulary is a narrowing, and it is well guarded.** `CommandKind` is a bitmask of `kSpawn`, `kDespawn`, `kThrust`. `CommandWireKind` in `src/protocol/command_wire_kind.hpp` maps each to an *optional* wire name, `nullopt` meaning server-issued; its primary template is declared and never defined, so a new kind fails to compile until someone decides whether a client may send it. Exactly one kind is client-sendable today, and `welcome-data.schema.json` pins `accepted_command_kinds` at `maxItems: 1` to match.

**The simulation cannot create a controller.** Controllers live in `blob_controllers` and are constructed by the application through `ControllerRegistry::create`; the tick has no access to them and must not. So "fill this seat with a wanderer" cannot be an action the tick performs — it can only be a *declaration the tick records* and the runtime reconciles against, which is the same observe-committed-state pattern `placement_recorder_system` already uses.

**The map bounds the seat count.** `RoyaleMode::validate_map` refuses a map with fewer spawn markers than `lobby_minimum_players`, so the seat maximum is the map's spawn-marker count and that check has to move with it.

## Owner decisions (2026-09-08)

- **Anyone in the lobby** may change the seat count, seat an NPC, clear a seat, and press Start. No host, no ready-check. Accept the accidental-start risk.
- **A human who connects to a full lobby takes an NPC's seat.** A person displaces a bot. If no seat holds an NPC, the lobby is genuinely full and the connection is refused with a diagnostic.

## The fixture problem, found while planning

**Making Start explicit changes when every royale match begins, including the seven recorded replays.** All seven `tests/fixtures/replays/*/match.ini` declare `lobby_minimum_players`, and several spawn exactly enough players to cross it and then rely on the match progressing — `royale-scripted-match`, `royale-simultaneous-draw`, `royale-spawn-order`, `royale-transition-per-tick` and `royale-elimination-timing` all set `countdown_seconds=0` so the transition is immediate, and five assertions in `tests/fixtures/` name `kCountdown` or `kRunning`.

Under this plan none of them would ever leave `lobby`, because nothing presses Start. So this is a **versioned gameplay change** and the fixtures need deliberate migration, not a compatibility escape hatch.

The escape hatch was considered and rejected: a `[royale] auto_start` flag would keep the fixtures untouched at the price of two start behaviours to maintain forever, one of which exists only so tests need not be edited. This tree refuses that kind of thing elsewhere and should refuse it here.

The migration also carries a real risk worth naming in advance. A `start_match` command applies at **phase 0**, while the old threshold was observed by the lifecycle machinery. If the transition lands one tick later than it used to, every expected value after it shifts. Do not assume it does not — measure it, and if a horizon moves, re-derive the new value and say in the fixture's own comment why it moved.

## Execution constraints

- **Seats are engine state, not royale's.** Argued in Step 1; if that argument fails under contact with the code, stop and re-plan rather than quietly moving them into royale's mode state.
- **The physics baseline does not move, but the replay fixtures must.** `AcceptedBaselineTick` keeps its exact values and `maps/` stays empty: adding commands and match state must not change one committed velocity. **`tests/fixtures/replays/` is a different matter and the earlier plans' "must stay empty" rule is explicitly lifted here** — see "The fixture problem" below. Every changed fixture value must be re-derived and explained, never regenerated to make a test pass.
- Every constraint from the 2026-09-07 plan still binds: format with `find` (never `git ls-files`), verify a Clang lane as well as GCC, read CI after every push, stage explicit paths, serialize builds across parallel agents, and re-run every gate a behaviour change can reach.
- **One protocol minor for the whole feature.** Commands, match fields and the welcome all land together at 2.3 rather than bumping three times.

## Steps

### Phase 1 — Seats exist

- [ ] **Step 1: Add the seat roster to match state**
  - Verify: `./scripts/verify-focused 'unit.simulation|fixtures'` with the accepted oracle unmodified
  - Notes: A seat is `empty`, `held by a controller` (human), or `declared for an NPC kind`. The roster is an ordered, bounded list with a count that is itself state. Put it beside `MatchPhase` in engine-owned `MatchState`, **not** in royale's `ModeMatchState` arm.
  - The argument for engine ownership: the four-phase machine is already engine-owned and generic, and seats are the *input* to `lobby -> countdown`. A mode supplies predicates, not lifecycle machinery (ADR 0004 § "Game modes and the match lifecycle"). Putting seats in royale's arm means the second competitive mode duplicates the roster, its wire schema, and the entire client lobby. The cost, stated honestly: `MatchState` grows, so world equality and the snapshot grow with it, and `sandbox` carries a roster it never uses — acceptable only because an empty roster is inert and costs one integer on the wire.
  - The seat count is bounded below by 1 and above by the map's spawn-marker count. Decide where the initial value comes from: this tree has no silently defaulted keys, so it is a required configuration key (`[royale] lobby_seat_count=4`) that the runtime may then change. Say so.

- [ ] **Step 2: Make starting a match a decision rather than a threshold**
  - Verify: `./scripts/verify-focused 'unit.gameplay|fixtures'`
  - Notes: `RoyaleObjective::can_start` becomes "every seat is filled **and** a start has been requested". The start request is a one-shot flag in the seat roster, cleared when the match leaves `lobby`, so a match cannot restart itself into `running` without someone pressing the button again.
  - `[royale] lobby_minimum_players` is superseded by the seat count. Decide and state whether it is deleted (and every fixture and deployment config edited) or retained as a floor on the seat count. Deleting it is cleaner and touches more; retaining it is two sources of truth for one idea, which this tree elsewhere refuses. Recommend deleting, and note that `royale_configuration.hpp`, the deployment config, `application_input_test_fixture.hpp`, `server_process_fixture.cpp`, and several replay `match.ini` files name it.
  - `validate_map` must now bound the seat count rather than the player minimum.

### Phase 2 — Operating the lobby over the wire

- [ ] **Step 3: Add the four lobby command kinds**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.protocol'`
  - Notes: `set_seat_count`, `seat_npc`, `clear_seat`, `start_match`. Each is a `CommandKind` bit, a `Command` variant alternative, and a `CommandWireKind` specialization naming it client-sendable. The declared-never-defined primary template means the build fails until each is decided, which is the seam working — do not defeat it.
  - Every one is refused outside `lobby`, and every one is validated the way `set_thrust` is: a seat index in range, a seat count in range, and an NPC kind that `ControllerRegistry` actually knows. An unknown bot kind from a client is a rejected command, never a rejected connection.

- [ ] **Step 4: Apply lobby commands at phase 0**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.runtime|fixtures'`
  - Notes: They are ordinary commands in the tick's `InputBatch`, applied in the existing phase-0 order, so determinism and replay are inherited rather than reinvented. Two clients seating the same seat on one tick resolve by the existing command order, and the second is a no-op rather than an error.
  - Anyone may send any of them, per the owner's decision. Say in a comment that this is a deliberate trust choice for a private tailnet and name what would have to change for a public deployment, because that is the question a reader will have.

- [ ] **Step 5: Publish the lobby and bump to 2.3**
  - Verify: `./scripts/verify-focused 'unit.protocol'` and `cd frontend-react && npm run generate:protocol:check`
  - Notes: The match section gains the seat roster; the welcome gains the NPC kinds a client may name, **read from `ControllerRegistry`**, so registering a new bot costs no client change — that is the acceptance test for this step. `accepted_command_kinds` grows from one entry to five, so `welcome-data.schema.json`'s `maxItems: 1` and its comment both move.
  - Follow the 2.1 and 2.2 bumps exactly (`git show f3628ba` and `e2083dc`), including moving every "one minor ahead" rejection case from 2.3 to 2.4 on both sides.

- [ ] **Step 5b: Migrate the seven replay fixtures to an explicit start**
  - Verify: `./scripts/verify-focused 'fixtures'` with every horizon either unchanged or re-derived and explained
  - Notes: Each `match.ini` swaps `lobby_minimum_players` for `lobby_seat_count`, and each `commands.csv` that relies on the match running gains a `start_match` row once the seats it needs are filled. `royale-transition-per-tick` is the one to do first and read hardest — it exists to pin one transition per tick, so it is the fixture most likely to expose a one-tick shift, and it is the cheapest place to discover one.
  - Do this **before** Phase 3, so the fixtures are honest while the runtime work lands rather than being retrofitted to whatever the runtime turned out to do.
  - `tests/fixtures/replay_fixture.cpp` reads `lobby_minimum_players` by name; it moves with the key. `application_input_test_fixture.hpp`, `server_process_fixture.cpp` and `deploy/ubuntu-pc/blob-royale.cfg` all name it too.

### Phase 3 — NPCs and late arrivals

- [ ] ~~**Step 6: Reconcile bot sessions to the seat roster**~~ *(superseded 2026-09-09: executed as Step 6 of `2026-09-09-lobbies-as-rooms.md`, where bots exist only through seats and a server-issued `join` command fills a declared seat)*
  - Verify: `./scripts/verify-focused 'unit.runtime|unit.application|integration'`
  - Notes: The tick records that seat 3 wants a `chaser`; the runtime observes committed seat state after each tick and opens or closes bot sessions so the live controllers match. This is the only place a controller is constructed, and it stays in the application layer where `ControllerRegistry` lives.
  - The reconciliation must be idempotent and must not thrash: a seat already holding the right kind is left alone. Deleting `seat_configured_bots` and letting `[match] bots` seed the initial roster instead is the likely shape — decide and say which.

- [ ] ~~**Step 7: A human joining a full lobby displaces an NPC**~~ *(superseded 2026-09-09: falls out of the `join` command in Step 6 of `2026-09-09-lobbies-as-rooms.md`; the refusal of a full lobby is that plan's Step 13)*
  - Verify: `./scripts/verify-focused 'unit.runtime|unit.application|integration'`
  - Notes: On session admission with no empty seat, take the lowest-indexed seat holding an NPC, close that bot's session, and seat the human. Lowest-indexed because it must be deterministic and explicable, not because it is fairest. With no NPC seated the lobby is genuinely full and the upgrade is refused with a diagnostic naming the reason.
  - Decide what happens to a human who connects while a match is `running` — this plan does not add spectators, so the honest answer is a refusal with a clear message, and it should say when to come back.

### Phase 4 — The client

- [ ] ~~**Step 8: Build the lobby view**~~ *(superseded 2026-09-09: Step 15 of `2026-09-09-lobbies-as-rooms.md`, which keeps these notes as its specification)*
  - Verify: `cd frontend-react && npm run typecheck && npm run lint && npm run test:ci && npm run build`
  - Notes: A seat grid rendered from the match section: each seat empty, a named human, or a named NPC, with the session's own seat marked. A seat-count control, and **right-click an empty seat** for a context menu of the NPC kinds the welcome published. Start Game is enabled exactly when every seat is full, and disabled with a reason when it is not.
  - Right-click means `contextmenu`, which must be prevented from opening the browser menu, must be dismissible with Escape and an outside click, and needs a keyboard-reachable equivalent — a context menu that only a mouse can open is a control some people cannot use at all. The arena canvas already owns pointer input; make sure the lobby's handler does not fight it.

- [ ] ~~**Step 9: Prove it in a browser**~~ *(superseded 2026-09-09: Step 16 of `2026-09-09-lobbies-as-rooms.md`, widened to two rooms)*
  - Verify: `./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`
  - Notes: One flow: two browsers join a four-seat lobby, one right-clicks a seat and fills it with a bot, the second fills the last seat, Start Game becomes enabled, one presses it, and both observe the match reach `running`. Assert that Start is disabled while a seat is empty, because that is the rule most likely to regress silently.

### Phase 5 — Ship

- [ ] ~~**Step 10: Deploy and playtest**~~ *(superseded 2026-09-09: Step 18 of `2026-09-09-lobbies-as-rooms.md`; HEAD is not deployable until that plan's Phase 5 passes)*
  - Verify: `ssh ubuntu-tailscale 'cd ~/Projects/blob-royale && git pull --ff-only && ./scripts/deploy-tailnet'`, then confirm the container, the deployed-commit label, readiness, and the served bundle's protocol version
  - Notes: The release profile takes roughly ninety minutes and refuses to publish anything it has not certified, so a failure leaves the previous arena serving. Record the deployed commit and release id in a playtest note this time.

## Done criteria

- A lobby of four seats appears before a match, the count is selectable, and the arena waits.
- Right-clicking an empty seat offers the registered NPC kinds and fills it; the bot appears in that seat and plays when the match starts.
- Start Game is disabled until every seat is full and starts the match when pressed.
- A person joining a full lobby takes a bot's seat rather than being turned away.
- Registering a new bot kind puts it in the client's menu with no client change.
- The baseline oracle passes unmodified. Every replay-fixture value that changed is re-derived and carries a written reason.
- The full pull-request profile passes on a native runner.

**Amended 2026-09-08:** Planning found that an explicit Start breaks all seven recorded replays, which cross `lobby_minimum_players` and then rely on the match progressing. That makes this a versioned gameplay change rather than an additive one; a compatibility flag was considered and rejected, and Step 5b migrates the fixtures deliberately ahead of the runtime work.

**Amended 2026-09-09:** The review in `docs/reviews/2026-09-08-lobby-and-hazard-review.md` and the agreed design in `docs/architecture/0006-lobbies-as-rooms.md` moved Steps 6 to 10 into `2026-09-09-lobbies-as-rooms.md`, where bot reconciliation and human seating become one server-issued `join` command beside a `leave` that closes the review's orphan finding. Steps 1 to 5b remain the record of what `ecc8e95` landed.
