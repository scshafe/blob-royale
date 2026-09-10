# Plan: King of the hill and race modes

**Goal:** Two more games are registered, configured, mapped, drawn, botted, and proven: a hill that tours the map and scores the players holding it, and a checkpoint race whose fallen return to the last gate; the four framework amendments ADR 0007 names land first, each guarded by the accepted fixtures, and every existing mode plays exactly as it did.
**Out of scope:** Everything ADR 0007 § "What is deliberately not built" lists: laps and closed courses, marker metadata authoring, generated respawn slots, a seeded hill tour, distance-along-course ranking, team variants, and per-room modes; royale on the shared restart wipe was tried inside Step 5 and kept.

## Context

The design is `docs/architecture/0007-king-of-the-hill-and-race-modes.md` (Proposed). This plan
executes it in the order that keeps the tree green at every commit: framework first, the hill
second because it exercises every shared piece with the smaller surface, the race third. The hill
is playable at the end of Phase 3 and that is a clean place to stop and playtest before the race.

Facts the executor needs that the code does not say on its face, all verified in the tree on
2026-09-09:

- `MatchObjective::outcome(const GameWorld&)` has no way to know the current tick
  (`src/simulation/match_objective.hpp`); `MatchLifecycleSystem::apply` holds the `TickContext` it
  does not pass (`src/simulation/match_lifecycle_system.cpp`). Three implementations exist:
  `idle_match_objective.hpp`, `sandbox/free_play_objective.hpp`, `royale/royale_objective.hpp`.
- `previous_phase` exists only inside royale (`mode_states/royale_placements_mode_state.hpp`,
  read through `royale/royale_mode_state.hpp` by `rotating_ring_spawn_policy.hpp` and
  `placement_recorder_system.cpp`). `royale-mode-state.schema.json` requires it on the wire, so the
  member stays and becomes a copy of the engine field.
- `SpawnSystem` offers every entity carrying a `Controllable` and no `PhysicsBody`
  (`src/simulation/spawn_system.hpp`), so erasing a body is the whole of "this entity is coming
  back"; the occupancy predicate and the at-rest write are private to `spawn_system.cpp:59-115`.
- Adding a component kind fires three static asserts in
  `src/protocol/component_encoding_registry.hpp` until its wire encoding exists, so a kind and its
  schema, encoder, and `kV2ComponentKindNames` entry are one commit
  (`.claude/plans/2026-09-07-hazards-and-variable-physics.md` § "Blockers found on 2026-09-07").
  The client's `entityRendererRegistry.ts` fails the build one regeneration later, not the same one.
- `kSystemCreatedEntityHeadroom = 1` (`src/simulation/simulation_limits.hpp:83`) and it does not
  move: 19 replay rows hardcode simulation-created ids. The hill takes the one id on its first
  tick, as the zone does; the race creates no entity.
- The replay fixtures assert named invariants, not digests (`tests/fixtures/replay_fixture.cpp`),
  and their reader requires `[royale]` (`tests/fixtures/replay_fixture.hpp`); the harness in
  `tests/fixtures/royale_replay_fixture_tests.cpp` is royale-shaped.
- `[royale]` is required in every configuration whatever `[match] mode` names
  (`src/application/application_config_loader.cpp:71`), so `[king_of_the_hill]` and `[race]`
  are too, in `deploy/ubuntu-pc/blob-royale.cfg`, the five `frontend-react/e2e/fixtures/*.cfg`,
  and every configuration the application tests author.
- `common.schema.json#/$defs/mode_state_schema_id` is a closed enum and `mode_name` is the open
  `kind_name` grammar, so a new block is a minor and a new mode name is not.
- `sessionSelectors.ts` pins the royale schema id and reads `elimination_grace_ticks`;
  `SimulationHud.tsx` and `MatchOverlay.tsx` are the two mode-facing client surfaces.
- The map loader publishes only `display_name` as map metadata (`src/application/map_loader.cpp:413`)
  and `markers.csv` has no metadata column, which is why the corridor half-width is a `[race]` key.

## Execution constraints

- **The accepted baseline does not move.** The `AcceptedBaselineTick` oracle, every wall and pair
  fixture, and every royale replay fixture pass untouched after every step. If a step changes one
  committed value, stop.
- **Both lanes, every step.** `./scripts/verify-focused '<filter>'` and the same filter on
  `linux-clang-asan-ubsan`, one build at a time; the client through `./scripts/verify-web`; the
  browser gate through `./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`.
- **Protocol 2.5 is one minor.** The version const moves once, in Step 5, and every later kind and
  block is added under 2.5; the row in `docs/protocol/v2.md` § "Versioning and fail-closed
  decoding" is opened in Step 5 with the first kind, extended by every wire step, and completed
  in Step 19 when the minor ships, so the document never pins a version its table does not name. Schema examples, generated client
  types, and `npm run generate:protocol:check` are part of every wire step.
- **Promotions are pure moves.** A file that moves to `shared/` keeps its arithmetic byte for byte;
  the added predicates ADR 0007 names are the only behaviour change and each has a test that fails
  without it.
- Stage explicit paths, one step per commit with the step name as the subject, execution notes in
  this file for every departure, and every constraint of
  `.claude/plans/2026-09-06-playable-prototype-tailnet.md` still binds.

## Steps

### Phase 1 -- Framework amendments

- [x] **Step 1: Give the objective the tick context**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.gameplay|fixtures' && ./scripts/verify-focused 'unit.simulation|unit.gameplay|fixtures' linux-clang-asan-ubsan`
  - Notes: `outcome(const GameWorld&, const TickContext&)`; the lifecycle system passes its
    context; the three objectives take and ignore the argument. Amend ADR 0004 § "Game modes and
    the match lifecycle" with a dated entry in the same commit. No test may lose a case.
  - Execution note (2026-09-09): `outcome(const GameWorld&, const TickContext&)`; the lifecycle system passes its context; the idle, free-play, and royale objectives and the two test objectives take and ignore it. Royale's tests call through a `TickHarness`, and a new case pins that attrition ignores the tick. ADR 0004's objective block and a dated amendment at its foot record the change. Verified at 507 of 510 on both lanes, the three failures being the deployment fixtures that had been failing since the rooms plan's Step 10 for a reason unrelated to this step (the deploy launch still passed a scenario to a four-room configuration); that is fixed as Step 17b of `2026-09-09-lobbies-as-rooms.md`, after which the `fixtures` filter is 21 of 21 on both lanes. `unit.protocol`, which holds one of the test objectives, was compiled and run separately on linux-gcc-debug before this commit.

- [x] **Step 2: Make `previous_phase` engine state**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol|fixtures' && ./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol|fixtures' linux-clang-asan-ubsan && git diff --quiet -- docs/protocol/schema/`
  - Notes: `MatchState::previous_phase`, written at the top of `MatchLifecycleSystem::apply`.
    Royale's policy and recorder read the engine field; `placement_recorder` copies it into the
    block's member, which stays on the wire unchanged. A lifecycle test pins the pair a declared
    system sees on the tick after a transition. Amend ADR 0004 § "Game modes and the match
    lifecycle" and ADR 0005 § "Where zone and elimination state live".
  - Execution note (2026-09-09): `MatchState::previous_phase`, written at the top of `MatchLifecycleSystem::apply` before the switch; `MatchSnapshot::previous_phase()` exposes it. Royale's ring policy and `placement_recorder` steps 1 and 3 read the engine field; step 4 keeps writing the block's member as a published mirror, and the two tests that set `previous_phase` by hand now set the engine field and leave the block at its default, so a reader that still consulted the mirror would fail. A lifecycle test walks the machine and pins the (phase, previous_phase) pair a declared system sees on each of five ticks. ADR 0004 and ADR 0005 each gained a dated amendment. Verified at 608 of 608 on both lanes with `docs/protocol/schema/` unchanged.

- [x] **Step 3: Export the seating helpers**
  - Verify: `./scripts/verify-focused 'unit.simulation|fixtures' && ./scripts/verify-focused 'unit.simulation|fixtures' linux-clang-asan-ubsan`
  - Notes: `spawn_seating.{hpp,cpp}` with `point_is_occupied(world, position, radius)` and
    `seat_body_at_rest(world, entity, position, configuration)`; `SpawnSystem` calls them. A
    refactor: the seating write and the predicate keep their expressions.
  - Execution note (2026-09-09): `spawn_seating.{hpp,cpp}` hold `point_is_occupied(point, bodies, player_radius)` and `seat_body_at_rest(world, entity, position, player_radius)`, moved out of `spawn_system.cpp` with their expressions intact -- the predicate keeps its `std::hypot` and its tolerance call -- and `SpawnSystem` composes them. The signatures take the radius and the body span rather than the configuration and the world the plan sketched, because that is exactly what each reads. Four direct tests pin the contact-range boundary, the empty arena, the at-rest write with an ordinary blob's physics, and replacement of a body the entity already carried. Verified at 412 of 412 on both lanes.

- [x] **Step 4: Promote the five shared rules**
  - Verify: `./scripts/verify-focused 'unit.gameplay|fixtures' && ./scripts/verify-focused 'unit.gameplay|fixtures' linux-clang-asan-ubsan && test -z "$(ls src/gameplay/royale src/gameplay/sandbox | grep -E 'roster|next_free')"`
  - Notes: `shared/roster.hpp` (plus `participant_entities`, `participant_count`),
    `shared/lobby_start_rule.hpp`, `shared/disc_geometry.hpp` (from `zone_elimination.cpp`),
    `shared/spawn_point_probe.hpp` (the forward probe both policies repeat; royale's ring policy
    stays in `royale/` and calls it), and `shared/next_free_spawn_point_policy.hpp` with the
    post-`ended` lobby deferral added here and a test for it; the `RespawnTimer` deferral is
    Step 5's, once the kind exists. Tests move with their sources; the gameplay README's file
    lists are remeasured by their own stated method.
  - Execution note (2026-09-09): `shared/roster.hpp` (royale's alive functions plus `participant_count` and `participant_entities`), `shared/lobby_start_rule.hpp`, `shared/disc_geometry.hpp`, `shared/spawn_point_probe.hpp`, and `shared/next_free_spawn_point_policy.hpp` with the post-`ended` lobby deferral; royale's ring policy stays in `royale/` and calls the probe. Every moved expression is byte for byte what it was, and the five new test files pin the two populations, the start rule, the rim boundary, the probe's wrap, and the one tick the open-field policy defers in. One authoring slip fixed under this step: a test built a zero-seat lobby through `SeatRoster::of_size`, which refuses zero; the no-lobby world is the default roster. Verified at 135 of 135 on both lanes.

- [x] **Step 5: Add `RespawnTimer`, the `respawn` system, and `match_reset`; open protocol 2.5**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol|fixtures' && ./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol|fixtures' linux-clang-asan-ubsan && ./scripts/verify-web`
  - Specialist: `testineer`
  - Notes: The kind, its schema, encoder, name-table entry, and the client's non-visual registry
    entry are one commit; the version const goes to 2.5 here and the "one minor ahead" rejection
    cases move with it, as 2.1 and 2.2 did. `respawn`'s tests pin the tick arithmetic ADR 0007
    § "Where the framework has to move" states for `D = 0`, `D = 1`, and an entity eliminated on
    the tick its timer expires; `match_reset`'s pin that a respawning entity and a pending joiner
    are both destroyed and a seat is not. Then try royale on `match_reset` in place of
    `placement_recorder` step 3: keep it only if every royale fixture's pinned count is unchanged,
    and record the outcome either way in an execution note.
  - Execution note (2026-09-09): The kind, its schema, encoder, name-table entry, `entity-snapshot` property, `common.schema.json` enum member, and the client's non-visual registry entry landed together with the version const at 2.5 on both ends; the four golden examples, the C++ goldens, the router and integration version pins, and the three ahead-by-one cases (two of them in the client, one of which the first pass missed) all moved. The 2.5 row of `docs/protocol/v2.md` is opened with this kind, per the amended constraint. `respawn` and `match_reset` are `shared/` systems with the tests the notes asked for, and the open-field policy defers a timer-carrying entity. Verified at 746 of 746 on linux-gcc-debug with the filter widened to `unit.server` and `integration` because both pin the protocol version, 737 of 737 on linux-clang-asan-ubsan with `unit.server` added, and 225 of 225 client tests through `verify-web` with generation drift, schema examples, typecheck, lint, and the build green. One hiccup outside the change: a 2.6 GB gitignored core dump from this morning's vitest crash sat in `frontend-react/` and broke prettier's walk; it was deleted. The royale adoption of `match_reset` is the next commit and its own note. Royale adoption (2026-09-09): royale now declares `match_reset` right after `placement_recorder`, whose step 3 is gone; the recorder's two wipe tests became one that pins it no longer wipes, `royale_mode_tests` pins five lifecycle systems, and ADR 0005 carries a dated amendment. Every replay fixture's pinned count and the 100-run bit-identity harness were unchanged, which was the gate: royale never held a participant without a body on a tick it wiped. Verified at 142 of 142 on both lanes, and the follow-up bullet in ADR 0007 is removed.

### Phase 2 -- King of the hill, server

- [x] **Step 6: Author the `[king_of_the_hill]` section**
  - Verify: `./scripts/verify-focused 'unit.gameplay|unit.application' && ./scripts/verify-focused 'unit.gameplay|unit.application' linux-clang-asan-ubsan`
  - Notes: `king_of_the_hill/king_of_the_hill_configuration.{hpp,cpp}` with the eleven keys and the
    rules in ADR 0007's table, `GameModeConfiguration::king_of_the_hill`, the loader's fields, and
    the section added to every configuration file in the tree with the proposed values. A test
    proves a configuration missing the section is refused naming it, and that `dwell + travel`
    converting to zero ticks is refused.
  - Execution note (2026-09-09): `KingOfTheHillConfiguration` with the eleven keys and the rules of ADR 0007's table, four `GAMEPLAY.KING_OF_THE_HILL_*` codes, the member on `GameModeConfiguration`, the loader's enum entries, spec rows, a fixed-section boolean parser, and the section in every configuration file: the shipped and deployed configurations, five browser fixtures, five fuzz corpus files, and the unit template. The loader tests pin the converted values, that the section is required (refused as its eleven missing keys, which is how a fixed section's absence has always been reported), and that the boolean has one spelling; the configuration tests pin the defaults, every rejection's key, the dwell-plus-travel rule, the hop and the never-stopping glide, and `points_to_win` at least one. Verified at 230 of 230 on both lanes.

- [x] **Step 7: Add `Hill` and `HillPresence`, and `hill_movement`**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol' && ./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol' linux-clang-asan-ubsan && ./scripts/verify-web`
  - Notes: Two kinds with their wire halves and client entries (`hill` visual at the zone layer,
    `hill_presence` non-visual). `hill_center(markers, dwell, travel, elapsed)` is a pure function
    with its own tests: one marker never moves, `travel = 0` hops, the glide's division precedes
    its multiplication, and the phase table's four rows. The entity creation fails hard on an
    empty reservation exactly as `zone_shrink` does.
  - Execution note (2026-09-09): Both kinds with their schemas, encoders, kind-name entries, the entity-snapshot properties, and the client entries -- `hill` visual at the zone layer with an amber disc, `hill_presence` non-visual -- under the open 2.5. `hill_geometry` holds the tour as a pure function and the marker projection; `hill_movement` creates the hill entity from the reservation and writes `Hill` every tick by the four-row phase table. Its tests pin one marker never moving, the hop, the exact midpoint of a glide, the cycle, the phase table, the empty-reservation rejection, and the missing-marker rejection; the creation path is proven through the mode in Step 8, because a hand-built world holds no reservation. Verified at 623 of 623 on both lanes and 226 of 226 client tests with the registry's kind set and visual order pinned.

- [x] **Step 8: Declare `king_of_the_hill`**
  - Verify: `./scripts/verify-focused 'unit.gameplay|unit.protocol' && ./scripts/verify-focused 'unit.gameplay|unit.protocol' linux-clang-asan-ubsan`
  - Specialist: `rigorous-architect`
  - Notes: `hill_scoring`, `HillObjective`, `hill_rules_publisher`, the `king_of_the_hill`
    mode-state block with its schema and encoder, `KingOfTheHillMode`, and the registry row.
    Tests pin the scoring table line by line -- leaving loses, contest freezes, respawn forgets,
    `I = 0` scores on the first tick -- and the objective's order: a field of one before the
    scoreboard, a same-tick threshold is a draw, the clock ranks the leader. The mode's line count
    is measured for the README by the README's method.
  - Execution note (2026-09-09): `hill_scoring`, `HillObjective`, `hill_rules_publisher`, the `king_of_the_hill` block (three declared constants) with its arm, schema id, wire encoding, schema, enum member, and `if/then` row under the open 2.5, `KingOfTheHillMode`, and the registry's third row. The scoring tests pin the table line by line and the hill-absent rejection; the objective tests pin the order (a field of one before the scoreboard, a same-tick threshold is a draw, the clock ranks the leader, participants not bodies); the mode tests pin the eight systems in order, the map rejections, the registry, the hill entity created on the first tick with the block stamped beside it, and a scripted two-seat match decided by the threshold on the derived tick. Two background lanes were killed by host memory pressure at their last objects and were finished incrementally in the foreground. Verified at 644 of 644 on both lanes and 226 of 226 client tests.

- [x] **Step 9: Map and replay fixtures for the hill**
  - Verify: `./scripts/verify-focused 'fixtures' && ./scripts/verify-focused 'fixtures' linux-clang-asan-ubsan && git diff --quiet -- maps/arena-960x640`
  - Specialist: `testineer`
  - Notes: `maps/hills-960x640` with four `hill` markers and eight `spawn` markers, authored as
    literals with a README deriving them. The fixture reader gains `[king_of_the_hill]`; the
    bit-identity harness becomes mode-generic through `GameModeRegistry::create` and keeps every
    royale fixture. New fixtures: `hill-scripted-match` (first move tick, first point tick,
    threshold decision tick), `hill-contested` (no point while two hold it, a point once one
    leaves), `hill-time-limit` (a draw at the clock with level scores, then a win with one thrust
    more). Each runs 100 times bit-identical.
  - Execution note (2026-09-09): `maps/hills-960x640` with four `hill` markers on the arena's horizontal midline and eight `spawn` markers on a ring, derived in its README; the shipped-map test loads it through the production loader. The fixture reader keeps the whole mode section as an opaque `[king_of_the_hill]` table and builds the mode through `GameModeRegistry::create`, so the harness is mode-generic and every royale fixture is byte-for-byte unchanged (the arena map is untouched by the verify's diff check). Four hill fixtures: `hill-scripted-match` (hill from the first tick, the tour's hop and glide on derived ticks, the freeze at `ended`, a point every interval, the clock ranking 27 to 19, the lobby wipe), `hill-threshold` (decided on the scoring tick), `hill-contested` (no point while two hold it, a point once one is pushed off), and `hill-time-limit-draw` (level scoreboard at the clock). Two corrections while authoring: a zero restart delay returns the ended match to the lobby, so the hill returns to marker 0 rather than staying frozen; and the tick that commits `ended -> lobby` at kLifecycle still wrote the frozen hill because `hill_movement` observed `ended` at kPostKernel, so the lobby's hill is first seen one tick later. Verified at 29 of 29 fixture tests on both lanes, the scripted hill match bit-identical over 100 fresh runs.

### Phase 3 -- King of the hill, client and bot

- [x] **Step 10: Draw and score the hill in the client**
  - Verify: `./scripts/verify-web`
  - Notes: The hill renderer, `sessionSelectors` for the scoreboard join and the local progress
    fraction, the HUD's mode-aware section keyed by `match.mode`, and the results overlay reading
    the hill block. Every countdown is a tick difference from the frame; no timer in the client.
    Tests: the scoreboard order, a frame with no hill block renders the royale HUD unchanged, a
    `point_interval_ticks` of zero saturates rather than divides.
  - Execution note (2026-09-09): The hill renderer landed in Step 7; this step adds the reading of the frame. `sessionSelectors` gains `kingOfTheHillRules` (the block's three constants, read once and correlated with the schema id the way the grace is), `scoreboard` (every `controllable` entity ranked by `score`, ties by ascending entity id, an absent score as zero, a knocked-out participant kept with its line), `hillProgressFraction` and `hillPresenceReport` (a zero interval saturates), `runningTimeRemainingSeconds` (clamped at zero), `respawnCountdownSeconds`, and `hillHudReport`, which composes the section once per frame. The HUD is keyed on that report rather than on `match.mode` -- the block's schema id is the closed enum the client fails closed on and the block carries every denominator -- and adds Time left, Score, Hill (progress ring beside the words; "Back in" while a respawn timer runs) and a Scoreboard table, hiding royale's Placement row; a royale frame renders its six rows exactly as before, pinned by name. The overlay says how the winner won in the hill's words and the countdown names the hill. The viewer now derives "in play" from the body rather than from the entity, because a knocked-out player keeps its entity. A built hill frame (`hillSnapshotDocument`) is validated by the 2.5 schemas before any test reads it. Verified by `verify-web` at 237 of 237.

- [ ] **Step 11: Add the `hill_seeker` bot**
  - Verify: `./scripts/verify-focused 'unit.controllers' && ./scripts/verify-focused 'unit.controllers' linux-clang-asan-ubsan`
  - Notes: Seeded, decides from the snapshot's `hill` entity, one registry row and one CMake
    line. A test proves two seekers with different seeds do not submit identical directions.

- [ ] **Step 12: Prove the hill in a browser**
  - Verify: `./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`
  - Notes: `blob-royale-browser-e2e-hill.cfg` and `blobRoyaleBrowserHill.spec.ts`: join, seat a
    `hill_seeker` through the lobby menu, start, see the hill drawn and a score rise, see the
    match decided. The four existing flows stay green.

### Phase 4 -- Race, server

- [ ] **Step 13: Author the `[race]` section, the course value, and `RaceProgress`**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol|unit.application' && ./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol|unit.application' linux-clang-asan-ubsan && ./scripts/verify-web`
  - Notes: `race/race_configuration.{hpp,cpp}` with the eight keys, the section in every
    configuration file, `race/race_course.{hpp,cpp}` with `distance_to_centreline` written out as
    ADR 0007 states it and the six map rejections, and the `race_progress` kind with its wire half
    and non-visual client entry. Geometry tests: a point beyond an endpoint clamps, a point on the
    line reads zero, a bend takes the nearer segment.

- [ ] **Step 14: Declare `race`**
  - Verify: `./scripts/verify-focused 'unit.gameplay|unit.protocol' && ./scripts/verify-focused 'unit.gameplay|unit.protocol' linux-clang-asan-ubsan`
  - Specialist: `rigorous-architect`
  - Notes: `checkpoint_progress`, `track_bounds`, `standings_recorder`, `checkpoint_respawn`,
    `course_publisher`, the `race` block with its schema and encoder, `RaceObjective`, `RaceMode`,
    and the registry row. Tests pin: gates in order and one per tick; an off-road centre is out of
    play that tick; a racer with `next_checkpoint == 0` returns through the policy and one with
    `k >= 1` through `checkpoint_respawn` on the same relative tick; an occupied gate delays by
    one tick; a shared placement; the finish window and the clock ranking.

- [ ] **Step 15: Map and replay fixtures for the race**
  - Verify: `./scripts/verify-focused 'fixtures' && ./scripts/verify-focused 'fixtures' linux-clang-asan-ubsan && git diff --quiet -- maps/arena-960x640`
  - Specialist: `testineer`
  - Notes: `maps/circuit-960x640`: a point-to-point course with one bend, three gates, a grid of
    four, two static obstacles, with a README deriving every literal. Fixtures:
    `race-scripted-course` (each gate's tick and the finish), `race-off-track-return` (the tick the
    body is gone and the tick it is back at the gate), `race-shared-finish` (two on one tick),
    `race-finish-window` (decided by the window, then by the clock in a second scenario).

### Phase 5 -- Race, client and bot

- [ ] **Step 16: Draw the course and the standings in the client**
  - Verify: `./scripts/verify-web`
  - Notes: The `modeStateRendererRegistry` seam (`@extension-point mode_state_renderer`, keyed by
    schema id, drawn once per frame before the entity layers) with the race entry drawing the
    corridor and the gates and non-visual entries for `none`, `royale`, and `king_of_the_hill`;
    the HUD's race section (gate `k of n`, standings, the return countdown from `respawn_timer`);
    the results overlay reading the race block. A test proves a frame whose block the registry
    does not know fails closed at validation, not in the renderer.

- [ ] **Step 17: Add the `racer` bot**
  - Verify: `./scripts/verify-focused 'unit.controllers' && ./scripts/verify-focused 'unit.controllers' linux-clang-asan-ubsan`
  - Notes: Reads the course from the observation's mode state and its gate from `race_progress`;
    steers to the next gate, or back to the centreline past the caution fraction. A test proves it
    turns back when placed near the edge and drives on when centred.

- [ ] **Step 18: Prove the race in a browser**
  - Verify: `./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`
  - Notes: `blob-royale-browser-e2e-race.cfg` and `blobRoyaleBrowserRace.spec.ts`: join, seat a
    `racer`, start, see the corridor drawn, drive the local blob off the road and see it return,
    see the bot finish and the standings fill. Five flows stay green.

### Phase 6 -- Documentation, review, and deployment

- [ ] **Step 19: Close the documentation**
  - Verify: human review
  - Notes: The 2.5 row in `docs/protocol/v2.md` and its `mode_state` paragraph; ADR 0004's what-if
    row for king of the hill corrected to the true count and its objective and `MatchState`
    sections amended with dated entries; ADR 0005's promotions amended; `src/gameplay/README.md`
    sections for both modes with measured line counts and the `shared/` inventory;
    `src/simulation/README.md` for the seating helpers and `previous_phase`; ADR 0007 to Accepted
    with the amendments execution taught it.

- [ ] **Step 20: Run every gate and review the diff**
  - Verify: `./scripts/verify-focused '.' && ./scripts/verify-focused '.' linux-clang-asan-ubsan && ./scripts/verify-web && ./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`
  - Specialist: `proofreader`
  - Notes: The review lands under `docs/reviews/` in the existing style; findings that are defects
    become steps inserted above this one, findings that are follow-ups become a list in ADR 0007's
    § "What is deliberately not built".

- [ ] **Step 21: Deploy the hill and play it**
  - Verify: `ssh ubuntu-tailscale 'cd ~/Projects/blob-royale && git pull --ff-only && curl -fsSI https://snapshot.ubuntu.com/ >/dev/null && ./scripts/deploy-tailnet'`, then the container, the `blob-royale.deployed-commit` label, readiness, the served bundle's protocol version `2.5`, and `GET /api/v2/lobbies` listing every room as `king_of_the_hill`, each checked independently of the script's own report.
  - Notes: Requires the owner's explicit go-ahead and pushed commits, as every deployment does.
    `deploy/ubuntu-pc/blob-royale.cfg` switches `[match] mode` and `map` to the hill for this
    playtest; the playtest note under `docs/playtests/` uses the template with the hill's balance
    rows added, and the race is played on the following session with the mode switched by
    `./scripts/reconfigure-tailnet`.

## Done criteria

Every box above is ticked; `./scripts/verify-focused '.'` passes on both lanes with the accepted
oracle, every royale fixture, and every wall and pair fixture unchanged; `./scripts/verify-web`
and the browser gate pass with six flows; `GET /api/v2/lobbies` on the tailnet reports the mode
the deployment names; ADR 0007 is Accepted with its execution amendments; and the playtest note
for the hill exists. Commits are pushed only when the owner says so.

**Amended 2026-09-09:** The three verify lines that asserted an empty `git diff --stat` through `wc -l | grep -qx 0` now use `git diff --quiet`, because macOS `wc` pads its count with spaces and the grep never matched even on a clean tree; the check was reporting a change that did not exist.
