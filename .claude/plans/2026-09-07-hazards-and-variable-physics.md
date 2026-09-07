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

- [x] **Step 2: Add restitution to `PhysicsBody` and a general impulse rule**
  - Verify: `./scripts/verify-focused 'unit.simulation|fixtures'` with the accepted oracle and every replay fixture unmodified.
  - Notes: Add `restitution` beside `mass`, defaulting to the perfectly elastic `1.0` so every existing construction keeps its meaning. Add a pure `resolve_general_pair_collision` implementing `j = -(1 + e) * (v_rel · n) / (1/m_a + 1/m_b)` with the written operation order fixed, where `e` is the pair's combined restitution (choose the combination rule — minimum, product, or average — and justify it; minimum is the usual choice because one sticky body should dampen the pair). Reject non-finite and non-positive mass. Add a `variable_impulse` contact rule whose predicate is "either body differs from the baseline", declared **above** `elastic_disc` so baseline pairs never reach it. Test: equal masses with restitution 1 agree with the baseline within tolerance; a heavy body barely deflects off a light one; restitution 0 leaves the pair moving together along the normal; momentum is conserved in every case.

- [x] **Step 3: Let a body cross the arena bounds**
  - Verify: `./scripts/verify-focused 'unit.simulation|fixtures'`
  - Notes: A hazard that crosses the screen must not fold off the walls, so phase 4 needs to know a body is unbounded, and the commit-time bounds check must stop rejecting it. Add the smallest capability that expresses it — a `bounds_behavior` on `PhysicsBody` or an `UnboundedMotion` component, whichever reads better — defaulting to the current folding so nothing existing changes. Its interaction with the spatial grid is the subtle part: a body outside the arena has no cell, so decide whether it is simply absent from the index while outside (and therefore collides with nothing there) or whether the grid clamps it, and test the boundary crossing either way.
  - Execution note (2026-09-07), Steps 2 and 3 together, commit `d9ea038`. Verified at 362 tests, 342 simulation and all 20 fixtures, then again at 787 unit tests across every library. `git diff --stat` over `tests/fixtures/`, `docs/protocol/schema/`, and `maps/` is empty and the accepted oracle symbols have no diff hits, checked independently rather than taken from the agent's report. Restitution combines by minimum. Bounds: a crossing body is skipped by the commit-time check and stays bounded by `Vector2`'s component limit; the grid **clamps** it into edge cells rather than dropping it, because a body one step outside a wall already overlaps one just inside and absence would be a missed contact rather than a deferred one. A static body may now carry zero mass, which nothing divides by, rather than regenerating the accepted protocol golden that publishes a wall that way.
  - Scope grew once, deliberately. The impulse alone would not have fixed the symptom: phase 3's gate and the grid's coverage box still sized every body at `2 * player_radius`, so a 26-unit hazard was admitted and indexed as if it were blob-sized and a player would have sunk into the drawn rock before anything happened. The grid one was a genuine defect against ADR 0003's superset requirement -- the pair was never offered at separations where the discs overlap, so the contact never happened at all. Both now measure at `r_a + r_b`, bit-identical today because every body's effective radius equals the configured one and `x + x` is exactly `2 * x`. A falsification test builds the exhaustive all-pairs reference the ADR names and fails when the coverage is reverted. Phase 4's fold and the commit-time interval deliberately keep the configured radius: contact is a question about a pair, the arena is a question about configuration.

### Phase 3 — Hazards as data

### Blockers found on 2026-09-07 and the decisions that clear them

A reconnaissance pass proved three things the plan had assumed away. All three are verified in the tree, not inferred.

**1. A new component forces its wire encoding in the same commit.** Adding `LethalOnContact` to `ComponentRegistry` fires three static asserts in `src/protocol/component_encoding_registry.hpp` (no `ComponentWireEncoding` specialization, the kind name missing from `kV2ComponentKindNames`, and the kind-count equality). That coupling is deliberate and documented. **Decision: fold Step 6's wire half into Step 4.** They are one commit and cannot be sequenced apart. Only the client renderer can lag, because `entityRendererRegistry.ts` fails the build a version later, not in the same build.

**2. `LethalOnContact` is the first zero-field component.** Every existing encoder writes at least one member. **Decision: it publishes `{}` with `additionalProperties: false` and no `required`.** A marker's presence is the whole message; a synthetic `{"lethal": true}` would be a field that can only ever hold one value, which is a lie the client would have to trust. Confirm `entity-snapshot.schema.json` admits an empty object before writing it.

**3. A system may create exactly one entity per tick, and royale already spends it.** `kSystemCreatedEntityHeadroom = 1` in `src/runtime/runtime_limits.hpp` sizes every tick's reservation at `spawn_count + 1`, and royale's `zone_shrink_system` draws it on the first running tick. **Decision: the headroom does not change.** Raising it advances the monotonic cursor every tick and renumbers every simulation-created id from tick 2, and that is not a theoretical cost: `tests/fixtures/replays/*/commands.csv` hardcode explicit entity ids in 19 rows across six fixtures, all of which would need regenerating. Verified by counting them. A feature that needs at most one spawn per 2.5 ms does not justify invalidating accepted data.

  So the spawner lives within the existing budget: it runs at **`kLifecycle`**, which is stage 2 and therefore after the `kPostKernel` zone systems that also create, it draws only what `GameWorld::entity_id_reservation()` still holds, and it defers the rest to the next tick in a deterministic order. Creating an entity is roster bookkeeping, which is what `kLifecycle` is for, so this is the right stage on its own merits rather than a workaround. Two archetypes due on the same tick, or a hazard due on the tick the zone appears, cost one tick of delay and nothing else.

**4. `Lifetime` is inert.** `ticks_remaining` is written by tests and published by the encoder, and nothing in `src/` decrements it or despawns on expiry, so "give each hazard a `Lifetime`" despawns nothing and hazards would accumulate against the 4,096 seat bound. **Decision: a lifetime expiry system is its own step, ahead of the spawner.** Nothing carries `Lifetime` today, so adding it changes no existing behavior.

**5. `kSystemCreatedEntityHeadroom` is declared twice** — in `src/runtime/runtime_limits.hpp` and again in `tests/fixtures/replay_fixture.hpp`, whose comment says the two must agree. That is the same defect the kind-name grammar had. Unify it if the test target can reach the runtime header; if it cannot, put a `static_assert` somewhere that sees both, so "must agree" is enforced rather than requested.

**On what `tests/fixtures/` means.** The earlier instruction that its diff must stay empty was a proxy for "do not regenerate accepted horizons", and it is too broad. The accepted baseline is the recorded data — `tests/fixtures/replays/**`, the `.csv` scenarios, and the oracle values. `replay_fixture.{hpp,cpp}` is the loader, and editing it is allowed when the recorded data and the observed behavior both stay put.

- [x] **Step 4: Add the hazard components and contact rule**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.gameplay'`
  - Notes: `LethalOnContact` as a marker component in `blob_simulation` (a value the wire can publish so the client can draw a hazard as dangerous), and a `lethal_hazard` contact rule in `blob_gameplay` that emits `EliminationEvent` for the player and leaves the hazard travelling. Declare it **above** the impulse rows so lethality wins over bouncing. Decide whether a lethal hazard eliminates on contact even during the zone grace period — it should, since it is a different rule — and test that a hazard cannot eliminate another hazard.
  - Placement decided 2026-09-07: hazards go in `src/gameplay/shared/`, beside `thrust_steering_system`, not under `royale/`. Objects that shoot across the arena are a mode-agnostic mechanic, and the owner's stated goal is that new games are cheap to build on this engine, so binding it to the first mode that uses it is the wrong seam. Royale declares the rows and the system in its own `contact_rules()` and `systems()`; sandbox stays free play and declares neither, which is also the test that the mechanic is genuinely optional.
  - `RoyaleMode::contact_rules()` returns `ContactRuleTable::built_in()` verbatim today, and `royale_mode.hpp` carries a comment explaining that royale changes no collision equation. That stops being true here, so rewrite the comment rather than leaving it: royale declares `lethal_hazard`, then `variable_impulse`, then the two built-in rows, and the reason for that order belongs beside the list.
  - Execution note (2026-09-07), Steps 4, 4b and 5b together. Verified at **830 unit tests on
    `linux-gcc-debug`** across the full filter and again on `linux-clang-asan-ubsan`. The recorded
    baseline did not move: `git diff --stat -- tests/fixtures/replays/ maps/` is empty, the only
    `tests/fixtures/` change is the loader's constant reference, and `AcceptedBaselineTick`,
    `kMigratedPairFixtureHorizon` and `bodies_are_bit_identical` have zero diff hits.
    - **The wire went to 2.1.** Adding a component kind is a minor version by
      `docs/protocol/v2.md` § "Versioning and fail-closed decoding", which says in as many words
      that it "republishes this schema set with the new kind registered and the `protocol_version`
      const bumped". Shipping eight kinds under a const promising seven would defeat the client's
      own version-first rule, which is the mechanism that lets a client fail closed on a kind it
      does not know. The bump moved the const, `common.schema.json`, three published examples, the
      encoder goldens, two server-router assertions, and the frame-size budget table (911 -> 934
      bytes for a maximal entity; 1,024 entities still fit at 51% of the ceiling, 2,048 still do
      not). The one "a minor ahead" rejection case moved from 2.1 to 2.2 so it still tests what its
      name says. A version-history table now records what each minor added.
    - **The marker publishes `{}`**, as decided. `entity-snapshot.schema.json` admits it: the
      `minProperties: 1` there is on the `components` map, not on each component object, so an
      entity carrying only this kind still has one property.
    - **`with_rows_above_built_in`** is the "these rows, then the built-in ones" seam, on
      `ContactRuleTable` itself. It calls `built_in()` rather than reconstructing it, so an
      inheriting mode cannot end up with a drifted copy of the baseline predicates, and it routes
      through `create`, so redeclaring `elastic_disc` is a startup rejection rather than a silently
      shadowed row.
    - **`lethal_hazard`'s second predicate is `Controllable` presence**, not the absence of the
      marker. One test rules out three failures: a hazard cannot eliminate another hazard, a wall,
      or the zone. It also matches what `placement_recorder` already demands, which throws for an
      eliminated entity with no controller.
    - **`lifetime_expiry` runs at `kLifecycle`** and emits `DespawnEvent` rather than destroying:
      phase 10 owns removal and the index-rebuild decision. `kPreKernel` would remove a body before
      the kernel moved it and a hazard would vanish a tick early; `kPostKernel` would race royale's
      own `zone_elimination`. A zero counter is treated as already spent, which is the branch that
      keeps an unsigned countdown from wrapping to 2^64-1.
    - **`hazard_spawn` runs at `kLifecycle`** as decided, after the zone systems. Three draws per
      hazard in a fixed order -- entry edge, point along it, point on the opposite edge -- and both
      budget checks precede every draw, so a tick that seats nothing leaves `draw_count` untouched.
      The `Lifetime` is `ceil((crossing_length + 2 * clearance) / speed / seconds_per_tick)` with
      `clearance = 2 * radius`, so no number in it is tuned.
    - **The headroom now has one definition**, in `simulation/simulation_limits.hpp`. It had
      **four** live references, not two: `runtime_limits.hpp`, `replay_fixture.hpp`,
      `gameplay_test_fixture.hpp`, and `application/match_startup_validation.cpp`, which the
      compiler found and a grep had missed. Neither test target links `blob_runtime`, so
      `blob_simulation` is the only library all four can reach -- the same resolution the kind-name
      grammar got.
    - Divergence worth naming: the spawner **skips** a kind that loses the one-id race rather than
      deferring it. A true deferral needs per-kind state between ticks and a system may hold only
      immutable configuration; nothing drifts, because due-ness is a pure function of the tick, so
      a kind that loses one appearance is back on schedule at its next multiple.
    - Known gaps, stated rather than hidden: `require_match_fits_snapshot_bound` does not count
      standing hazards toward `kSnapshotEntityLimit` because the archetype table is not reachable
      from its signature, so a deployment authoring many or very slow kinds can exceed the bound at
      run time instead of at startup -- named in a comment where it lives, and Step 8's business.
      And the lethal rule and the spawner are each tested thoroughly but not *together*: no test
      drives a spawned hazard into a player, because the crossing geometry is drawn and arranging a
      collision would be timing-dependent. Step 7's browser flow is where that meets.


- [x] **Step 5a: Make a hazard kind a configured archetype**
  - Verify: `./scripts/verify-focused 'unit.gameplay|unit.application'` — done, 787 unit tests pass, commit `641f8be`.
  - Notes: A `HazardSpawnSystem` at `kPreKernel` drawing from the world's seeded generator, so a replay reproduces every hazard exactly. Archetypes come from configuration, not code: a `[hazards]` section naming an interval and the kinds in play, plus one `[hazard.<name>]` section per kind carrying radius, mass, restitution, speed, lethality, and the edge it enters from. **Adding a hazard kind must be a config section and nothing else** — that is this step's acceptance test, so write a test that loads a configuration with a kind the code has never heard of and asserts it spawns with those properties. Each hazard gets a `Lifetime` sized so it despawns after crossing, and the spawner respects the published entity bound.
  - The loader blocks this today, and clearing it is the real work of the step. `application_config_loader.cpp` holds a closed seven-name section array and a closed field-spec array indexed by a `ConfigField` enum, so the section and every key are fixed at compile time and anything unknown is rejected. That fail-closed behavior is correct and must survive. Add exactly one concept: a **section family**, a declared prefix whose instance names are open, whose key schema is closed and shared by every instance, and whose instances collect into a vector instead of a fixed field. Unknown keys inside an instance stay rejected, an empty family stays legal, and a section that matches neither a known name nor a known family prefix stays rejected, so the only thing that becomes open is the one thing the owner asked to be open. Name the concept for what it is; it is reusable, and a later per-bot roster section is the obvious second customer, which is the two-implementations test the seam has to pass.
  - The acceptance test is the point of the step: a configuration declaring a hazard kind whose name appears in no C++ file must load and spawn a body carrying the declared radius, mass, restitution, and lethality. Have the test name that fact in its own comment so a future reader can grep and confirm it rather than trust it.
  - Step 5a execution note (2026-09-07, unverified — no build was run, the toolchain was held by the concurrent Step 2 work). The configuration half is written: the section-family concept in the loader, `gameplay::HazardArchetype` in `src/gameplay/shared/`, and the tests. The spawner (5b) is still ahead. Four decisions diverged from the sketch above and each is stated in a comment where it lives.
    - **The concept is a section family**, spelled `ConfigSectionFamily` and its friends in `application_config_loader.cpp`. It is one declared prefix list, one flat key-spec list tagged by family, and one vector of instances — the same three shapes the fixed schema already has, one nesting level down. `[bot.wanderer]` costs one prefix, one enumerator, and its keys.
    - **`kinds=` is dropped, and so is `[hazards]`.** A list of kinds beside the sections that declare them is two sources of truth for one list, and its failure mode is silent: a kind declared and not listed never spawns. Removing it empties `[hazards]` down to the spawn interval, and the interval is better per kind (`[hazard.comet] spawn_interval_seconds=6`) because a designer wants a comet every six seconds and a boulder every twenty. Removing the section also removes the contradiction between "do not make `[hazards]` required" and "this tree has no optional sections and no silently defaulted keys". The result is that adding a kind is exactly one section and no edit anywhere else, which is a purer form of the acceptance bar than the sketch. A family-wide density knob can be added later as its own section without invalidating anything authored today.
    - **The archetype names no entry edge.** A hazard needs a reproducible entry point *and* direction, and both have to come from the seeded stream for a replay to reproduce the crossing; naming the edge would make one component of that geometry authored and the rest drawn. It would also need a closed edge vocabulary in C++, so a designer wanting a corner entry would be back to writing code — the exact bar this step exists to clear. And a fixed edge per kind reads as a pattern to memorize rather than a hazard. The spawner draws the edge, the point along it, and the direction from one seeded stream; adding `entry_edge=` later is additive because the key schema is closed.
    - **`duration_ticks` moved to `src/gameplay/shared/duration_ticks.{hpp,cpp}`.** Reusing it was required and reusing it in place was not possible: a `shared/` mechanic may not include `royale/`, and the helper prefixed every context with `royale.` and threw `GAMEPLAY.ROYALE_*` codes, so a hazard interval would have been refused under another mode's name. `royale_configuration.hpp` had already pre-committed to this move. The parameter is now the full context and the three codes are owner-neutral `GAMEPLAY.DURATION_*`; the arithmetic is untouched, and `royale_configuration_tests.cpp` pins `royale.zone_shrink_seconds` to prove the contexts an operator reads did not change. `kRoyaleDurationTickOverflow` was removed as it became unreachable.
    - Known weaknesses, stated rather than hidden: the kind-name grammar now exists twice in the tree (`match_configuration.cpp` for `[match] mode`, `hazard_archetype.cpp` for a hazard kind) because neither library may depend on the other, and a third publisher of kind names should push it into `blob_simulation`; a family instance carries one value slot per key of *every* family, which is a handful of wasted optionals per instance today and grows with each family added; and `[hazard.*]` sections are accepted whatever `[match] mode` names, so a `sandbox` deployment can declare hazards nothing will read — the same property `[royale]` already has and defensible for the same reason, but worth knowing.
  - Orchestrator note (2026-09-07). Verified independently: `plaid_meteorite` and `velvet_boulder` appear in zero files under `src/`, and the only occurrences of `comet` and `boulder` are illustrative prose in two READMEs, so the acceptance bar genuinely holds. Weakness 2 was fixed rather than deferred, in commit `042c5c6`: the kind-name grammar existed **three** times, not two — `ContactRuleName`, `match_configuration.cpp`, and the new hazard code — all citing the same schema definition and free to drift apart without one test noticing. It now lives in `blob_simulation`, the one library all three callers can reach, with length left out of the predicate so no caller's bound is imposed on the others. Weaknesses 3, 4 and 5 are accepted as stated and not worth their fix today.

- [x] **Step 4b: Make `Lifetime` mean something**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.gameplay|fixtures'`
  - Notes: A system that decrements `Lifetime::ticks_remaining` and emits `DespawnEvent` at zero. Nothing carries `Lifetime` today, so this changes no existing behavior and no fixture; it is the prerequisite that keeps hazards from accumulating against `kMaximumEntityCount`. Decide which stage owns it and say why. Test that an entity with a lifetime of one tick is gone on the next tick, that a despawn frees its seat, and that an entity with no `Lifetime` is untouched.

- [x] **Step 5b: Spawn hazards from the archetype table**
  - Verify: `./scripts/verify-focused 'unit.gameplay|unit.simulation'`
  - Notes: A `HazardSpawnSystem` at `kPreKernel` drawing the entry edge, the point along it, and the direction from `GameWorld::random()`, so a replay reproduces every crossing exactly. Each hazard gets a `Lifetime` sized so it despawns after crossing, and the spawner respects the published entity bound. It reads `GameModeConfiguration::hazards`, which Step 5a already validates, so this step adds no configuration and no new failure mode. Live in `src/gameplay/shared/` beside the archetype; royale declares the system, sandbox does not. Two constraints from Step 2 bind here: `resolve_general_pair_collision` requires two **dynamic** bodies, so any row reusing it must have predicates that guarantee that, and a hazard body must declare the crossing bounds behavior or it will fold off the walls instead of leaving.

- [x] **Step 6: Draw hazards and publish them**
  - Verify: `./scripts/verify-focused 'unit.protocol'` and `cd frontend-react && npm run generate:protocol:check && npm run test:ci && npm run build`
  - Notes: Wire encoders for the new component kinds, their schemas, and a renderer registration so a hazard is visibly distinct and a lethal one reads as dangerous before it arrives. A new component kind must still fail the build until it has both an encoder and a renderer.

### Phase 4 — Ship

  - Execution note (2026-09-07), Step 6 with Step 4 in commit `f3628ba`, because the compiler makes them one commit. Verified at 830 C++ tests on GCC **and** Clang ASan/UBSan, and 134 client tests with typecheck, lint, schema-example validation and the production build.
  - The wire moved to **2.1**, correctly and per the rule already written in `docs/protocol/v2.md` § "Versioning and fail-closed decoding": registering a component fires three static asserts demanding an encoder and a published name, and shipping eight kinds under a const promising seven would defeat the client's own version-first rule. Every "one minor ahead" rejection case moved 2.1 → 2.2 on both sides, because a test named for a newer minor that names the current one tests nothing.
  - The lethal marker is the first zero-field component and publishes `{}`. `entity-snapshot.schema.json` admits it: its `minProperties: 1` is on the `components` map, not on each component object. A synthetic `{"lethal": true}` was rejected as a field that can only hold one value.
  - The hazard ring is **dashed** where the zone-exposure ring is solid, so the two warnings are separable by a player who cannot distinguish red from amber. They never share an entity today but they share a frame constantly. A heavy but non-lethal hazard gets no ring at all, because marking a positional threat and a lethal one identically teaches a player to ignore both. The client's debug-panel test now reads `SUPPORTED_PROTOCOL_VERSION` rather than a literal, so the next minor is not an unrelated test edit.

- [ ] **Step 6b: Decide whether hazards collide with each other**
  - Verify: human review, then whatever the decision implies
  - Notes: Two hazards currently deflect each other through `variable_impulse`, because both carry the default collision layer and mask. It is harmless and never lethal, but comets knock each other off course and a hazard can be batted back out of the arena it just entered. **This is a feel question and the playtest is where it should be answered**, so it is deliberately left open rather than guessed. If they should pass through one another the shape is a hazard collision layer that the player mask includes and the hazard mask excludes — one constant and one line in the spawner — and per-kind layers become an obvious later config key. Recommend playing it first: emergent hazard collisions may be better than the tidy answer.

- [ ] **Step 7: Play it locally, then extend the browser flow**
  - Verify: `./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`
  - Notes: Add hazards to the royale end-to-end flow: a lethal hazard eliminates a browser's blob, and a heavy one visibly deflects it without eliminating it. Determinism is the same discipline as the existing flow — a seeded generator makes hazard timing reproducible, so assert on the seeded outcome rather than waiting for a random one.

  - **Prerequisite found during Step 5b:** `require_match_fits_snapshot_bound` does not count standing hazards toward `kSnapshotEntityLimit`, because the archetype table is not reachable from its signature — it takes `MatchConfiguration` and `MapDefinition` while archetypes hang off `GameModeConfiguration`. A deployment authoring many kinds, or very slow ones, therefore exceeds 1,024 live entities at run time instead of being refused at startup. Fix before shipping a hazard table to the tailnet: the failure mode is a match that degrades under load rather than a configuration that fails closed, which is the opposite of how everything else here behaves. The ceiling is a product of each kind's interval and its lifetime, both of which the archetype already carries.

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

**Amended 2026-09-07 (second):** Reconnaissance for Step 4 found three blockers the plan had assumed away — the component/wire coupling, the one-entity-per-tick system budget, and an inert `Lifetime`. The decisions clearing them are recorded above Step 4; Step 6's wire half folds into Step 4, a new Step 4b makes `Lifetime` real, and the spawner moves to `kLifecycle` rather than raising an entity-id constant that would invalidate six recorded fixtures.
