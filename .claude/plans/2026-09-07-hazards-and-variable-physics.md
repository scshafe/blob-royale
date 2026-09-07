# Plan: Zone feedback, hazards, and per-body physics

**Goal:** A player can see they are outside the zone and how long they have; objects fly across the arena, some lethal on touch and some merely heavy; and mass and bounciness are per-body values a designer sets in configuration, with a new hazard kind costing a config section and no code.
**Out of scope:** Changing the baseline two-blob collision, regenerating any accepted fixture horizon, hazard art beyond a drawn disc, hazards in `sandbox` (it stays free play), spawning hazards from anything but the mode's own system, and any protocol major version.

## Context

Three facts decide the shape of this work, all verified in the tree on 2026-09-07.

`PhysicsBody` carries `mass` but **no phase reads it**: the narrow phase is the equal-unit-mass exchange in `src/simulation/physics.cpp`, and ADR 0003 § "Player-pair policy" fixes it that way. There is no restitution concept anywhere. And `ContactRuleTable::first_match` walks rows **in declared order and takes the first match**, which is what makes this feasible without a versioned physics change: a mode declares a general-impulse row *above* the baseline row, predicated on a body actually differing from the baseline, so a world of ordinary blobs never reaches it and every accepted horizon keeps its exact arithmetic. ADR 0003's warning that unequal masses force a versioned change applies to *replacing the default*, which this does not do.

The zone already publishes everything the client needs. `zone_exposure.outside_ticks` is a registered component on the wire, and `entityRendererRegistry.ts:52` deliberately marks it non-visual with the note that it belongs in the HUD, where it was then never added. The owner played on 2026-09-07 and read the resulting silence as a broken elimination rule.

## Execution constraints

- **The baseline collision does not change.** `elastic_disc` keeps calling `resolve_player_pair_collision` unmodified, and the `AcceptedBaselineTick` oracle and every replay fixture must pass untouched. If a change here alters one committed value, stop.
- **A general rule must reduce to the baseline.** Prove by test that the general impulse equation with unit masses and restitution 1 equals the baseline result within the accepted tolerance, and say plainly that it is not bit-identical and therefore never used for baseline bodies.
- **A new hazard kind is configuration.** Adding one must mean a config section and no C++. If that fails, the design is wrong.
- **Only one build at a time.** `scripts/verify-focused` configures and builds the shared `out/build/<preset>` tree, and the Linux toolchain is an emulated Colima VM with 2 CPUs and 2 GiB. Two agents verifying at once corrupt one build directory and can get a compiler OOM-killed. Parallel agents are fine and worth it, but they must partition by path *and* the orchestrator must serialize every build. An agent that cannot build is told so up front and told to compensate by reading real declarations instead of inferring signatures.
- Every other constraint from `.claude/plans/2026-09-06-playable-prototype-tailnet.md` still binds: format with `find`, verify a Clang lane as well as GCC, read CI after every push, stage explicit paths, and re-run every gate a behavior change can reach.

## Steps

### Phase 1 — Make the existing rule visible

- [x] **Step 1: Show zone exposure in the client**
  - Verify: `cd frontend-react && npm run typecheck && npm run lint && npm run test:ci && npm run build`
  - Execution note (2026-09-07): Verified at 132 client tests, up from 123, with typecheck, lint, and build green. An exposed blob gets an amber ring and the local player's gets a heavier double red one, so "someone is in trouble" and "I am in trouble" differ at a glance; a safe blob draws exactly as before. The registry entry was replaced rather than the canvas special-cased, which is what the seam is for. The HUD row exists only while exposed and disappears on the same tick the server resets the counter, so no client timer can disagree with the server. Ring intensity is deliberately not ramped, because without the grace duration a ramp is a guess at a denominator; Step 1b removes that excuse. Two assertions changed because the frame genuinely paints one more circle, and both were strengthened to pin the count to a named cause.
  - Notes: The data already arrives. Draw the danger state on an exposed blob (own blob especially) and put the remaining grace in the HUD, counting down from `elimination_grace_seconds`. The grace period is a tick count the wire does not carry, so derive the remaining fraction from `outside_ticks` against a value the client learns from `/api/v1/config` or treats as unknown — decide and say which, and do not invent a duration. Replace the registry's non-visual entry rather than special-casing the canvas. Tests: an exposed blob renders differently from a safe one, the countdown appears only while exposed, and it clears on re-entry.

- [ ] **Step 1b: Publish the grace duration so the countdown is real**
  - Verify: `./scripts/verify-focused 'unit.protocol|unit.gameplay'` and `cd frontend-react && npm run generate:protocol:check && npm run test:ci && npm run build`
  - Notes: Step 1 established that the client cannot learn `elimination_grace_seconds`: it is a `[royale]` key held by the mode, and no v1 config block, welcome, match section, or mode-state schema carries it, so the HUD honestly shows elapsed exposure rather than a fabricated remainder. That is a worse answer to the owner's actual complaint, which was not knowing why elimination did or did not happen. Add `elimination_grace_ticks` to `royale-mode-state.schema.json`, which exists precisely for non-entity-shaped mode state and is the documented `snapshot_mode_state` extension point, and bump the protocol minor version. Ticks, not seconds, because the mode already stores ticks and nothing else on the wire is in seconds. Then the HUD counts down and the danger ring can ramp with elapsed grace, which Step 1 deliberately refused to do without a denominator. The counter-argument, that publishing it hands clients a balance number, is about churn rather than secrecy and loses to a player who cannot tell how long they have.
  - Route decided 2026-09-07 after reading all three candidates. The value is mode configuration and the encoder can only see the world, so publishing it means a royale system stamps it into `RoyalePlacementsModeState` beside `previous_phase`. The placement recorder already rewrites that whole block at `kLifecycle`, so no new writer is needed, but the constant must be stamped by a system whose name admits what it does rather than smuggled into the recorder. The welcome frame was rejected: `welcome-data.schema.json` is mode-agnostic and names only `mode` and `map`, so a royale balance number there is a wart every future mode inherits. Publishing per frame also carries the value into replays, which is where a countdown has to keep working. Confirmed low risk: no fixture digest covers `mode_state`, and the only reader of the arm today is `royale_mode_state_of`.
  - Sequencing: this sits behind the hazard work in the owner's priority, because Step 1 already answered the complaint that prompted it. Do it after Step 6 unless a hazard step blocks on it.

### Phase 2 — Per-body physics

- [ ] **Step 2: Add restitution to `PhysicsBody` and a general impulse rule**
  - Verify: `./scripts/verify-focused 'unit.simulation|fixtures'` with the accepted oracle and every replay fixture unmodified.
  - Notes: Add `restitution` beside `mass`, defaulting to the perfectly elastic `1.0` so every existing construction keeps its meaning. Add a pure `resolve_general_pair_collision` implementing `j = -(1 + e) * (v_rel · n) / (1/m_a + 1/m_b)` with the written operation order fixed, where `e` is the pair's combined restitution (choose the combination rule — minimum, product, or average — and justify it; minimum is the usual choice because one sticky body should dampen the pair). Reject non-finite and non-positive mass. Add a `variable_impulse` contact rule whose predicate is "either body differs from the baseline", declared **above** `elastic_disc` so baseline pairs never reach it. Test: equal masses with restitution 1 agree with the baseline within tolerance; a heavy body barely deflects off a light one; restitution 0 leaves the pair moving together along the normal; momentum is conserved in every case.

- [ ] **Step 3: Let a body cross the arena bounds**
  - Verify: `./scripts/verify-focused 'unit.simulation|fixtures'`
  - Notes: A hazard that crosses the screen must not fold off the walls, so phase 4 needs to know a body is unbounded, and the commit-time bounds check must stop rejecting it. Add the smallest capability that expresses it — a `bounds_behavior` on `PhysicsBody` or an `UnboundedMotion` component, whichever reads better — defaulting to the current folding so nothing existing changes. Its interaction with the spatial grid is the subtle part: a body outside the arena has no cell, so decide whether it is simply absent from the index while outside (and therefore collides with nothing there) or whether the grid clamps it, and test the boundary crossing either way.

### Phase 3 — Hazards as data

- [ ] **Step 4: Add the hazard components and contact rule**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.gameplay'`
  - Notes: `LethalOnContact` as a marker component in `blob_simulation` (a value the wire can publish so the client can draw a hazard as dangerous), and a `lethal_hazard` contact rule in `blob_gameplay` that emits `EliminationEvent` for the player and leaves the hazard travelling. Declare it **above** the impulse rows so lethality wins over bouncing. Decide whether a lethal hazard eliminates on contact even during the zone grace period — it should, since it is a different rule — and test that a hazard cannot eliminate another hazard.
  - Placement decided 2026-09-07: hazards go in `src/gameplay/shared/`, beside `thrust_steering_system`, not under `royale/`. Objects that shoot across the arena are a mode-agnostic mechanic, and the owner's stated goal is that new games are cheap to build on this engine, so binding it to the first mode that uses it is the wrong seam. Royale declares the rows and the system in its own `contact_rules()` and `systems()`; sandbox stays free play and declares neither, which is also the test that the mechanic is genuinely optional.
  - `RoyaleMode::contact_rules()` returns `ContactRuleTable::built_in()` verbatim today, and `royale_mode.hpp` carries a comment explaining that royale changes no collision equation. That stops being true here, so rewrite the comment rather than leaving it: royale declares `lethal_hazard`, then `variable_impulse`, then the two built-in rows, and the reason for that order belongs beside the list.

- [ ] **Step 5: Spawn hazards from a configured archetype table**
  - Verify: `./scripts/verify-focused 'unit.gameplay|unit.application'`
  - Notes: A `HazardSpawnSystem` at `kPreKernel` drawing from the world's seeded generator, so a replay reproduces every hazard exactly. Archetypes come from configuration, not code: a `[hazards]` section naming an interval and the kinds in play, plus one `[hazard.<name>]` section per kind carrying radius, mass, restitution, speed, lethality, and the edge it enters from. **Adding a hazard kind must be a config section and nothing else** — that is this step's acceptance test, so write a test that loads a configuration with a kind the code has never heard of and asserts it spawns with those properties. Each hazard gets a `Lifetime` sized so it despawns after crossing, and the spawner respects the published entity bound.
  - The loader blocks this today, and clearing it is the real work of the step. `application_config_loader.cpp` holds a closed seven-name section array and a closed field-spec array indexed by a `ConfigField` enum, so the section and every key are fixed at compile time and anything unknown is rejected. That fail-closed behavior is correct and must survive. Add exactly one concept: a **section family**, a declared prefix whose instance names are open, whose key schema is closed and shared by every instance, and whose instances collect into a vector instead of a fixed field. Unknown keys inside an instance stay rejected, an empty family stays legal, and a section that matches neither a known name nor a known family prefix stays rejected, so the only thing that becomes open is the one thing the owner asked to be open. Name the concept for what it is; it is reusable, and a later per-bot roster section is the obvious second customer, which is the two-implementations test the seam has to pass.
  - The acceptance test is the point of the step: a configuration declaring a hazard kind whose name appears in no C++ file must load and spawn a body carrying the declared radius, mass, restitution, and lethality. Have the test name that fact in its own comment so a future reader can grep and confirm it rather than trust it.

- [ ] **Step 6: Draw hazards and publish them**
  - Verify: `./scripts/verify-focused 'unit.protocol'` and `cd frontend-react && npm run generate:protocol:check && npm run test:ci && npm run build`
  - Notes: Wire encoders for the new component kinds, their schemas, and a renderer registration so a hazard is visibly distinct and a lethal one reads as dangerous before it arrives. A new component kind must still fail the build until it has both an encoder and a renderer.

### Phase 4 — Ship

- [ ] **Step 7: Play it locally, then extend the browser flow**
  - Verify: `./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`
  - Notes: Add hazards to the royale end-to-end flow: a lethal hazard eliminates a browser's blob, and a heavy one visibly deflects it without eliminating it. Determinism is the same discipline as the existing flow — a seeded generator makes hazard timing reproducible, so assert on the seeded outcome rather than waiting for a random one.

- [ ] **Step 8: Deploy and playtest the tuned values**
  - Verify: `ssh ubuntu-tailscale 'cd ~/Projects/blob-royale && git pull --ff-only && ./scripts/deploy-tailnet'` then a playtest note under `docs/playtests/`
  - Notes: Ship conservative starting values and expect to change them; `scripts/reconfigure-tailnet` applies a new hazard table in seconds without a rebuild, which is the point of putting archetypes in configuration.

## Done criteria

- A player outside the zone can see it and knows how long they have; elimination no longer reads as arbitrary.
- Objects cross the arena. Touching a lethal one eliminates immediately; a heavy one shoves a blob aside and keeps going.
- Mass and restitution are per-body configuration, and a new hazard kind is a config section with no code, proven by a test using a kind the source has never named.
- Every accepted fixture horizon and the baseline oracle pass unmodified, because baseline pairs never reach the general rule.
- The full pull-request profile passes on a native runner.

**Amended 2026-09-07:** Step 1b was added once Step 1 proved the grace duration is not on the wire, then resequenced behind the hazard work because Step 1 already answered the complaint that prompted it. Steps 4 and 5 gained the two decisions that reading the tree settled: hazards are shared rather than royale-owned, and the closed config loader needs one new section-family concept before a hazard kind can cost no C++.
