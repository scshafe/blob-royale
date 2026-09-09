# Plan: King of the hill and race modes

**Goal:** Two more games are registered, configured, mapped, drawn, botted, and proven: a hill that tours the map and scores the players holding it, and a checkpoint race whose fallen return to the last gate; the four framework amendments ADR 0007 names land first, each guarded by the accepted fixtures, and every existing mode plays exactly as it did.
**Out of scope:** Everything ADR 0007 § "What is deliberately not built" lists: laps and closed courses, marker metadata authoring, generated respawn slots, a seeded hill tour, distance-along-course ranking, team variants, per-room modes, and royale on the shared restart wipe unless its fixtures allow it inside Step 5.

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
  decoding" is written in Step 19 when the minor is complete. Schema examples, generated client
  types, and `npm run generate:protocol:check` are part of every wire step.
- **Promotions are pure moves.** A file that moves to `shared/` keeps its arithmetic byte for byte;
  the added predicates ADR 0007 names are the only behaviour change and each has a test that fails
  without it.
- Stage explicit paths, one step per commit with the step name as the subject, execution notes in
  this file for every departure, and every constraint of
  `.claude/plans/2026-09-06-playable-prototype-tailnet.md` still binds.

## Steps

### Phase 1 -- Framework amendments

- [ ] **Step 1: Give the objective the tick context**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.gameplay|fixtures' && ./scripts/verify-focused 'unit.simulation|unit.gameplay|fixtures' linux-clang-asan-ubsan`
  - Notes: `outcome(const GameWorld&, const TickContext&)`; the lifecycle system passes its
    context; the three objectives take and ignore the argument. Amend ADR 0004 § "Game modes and
    the match lifecycle" with a dated entry in the same commit. No test may lose a case.

- [ ] **Step 2: Make `previous_phase` engine state**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol|fixtures' && ./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol|fixtures' linux-clang-asan-ubsan && git diff --stat -- docs/protocol/schema/ | wc -l | grep -qx 0`
  - Notes: `MatchState::previous_phase`, written at the top of `MatchLifecycleSystem::apply`.
    Royale's policy and recorder read the engine field; `placement_recorder` copies it into the
    block's member, which stays on the wire unchanged. A lifecycle test pins the pair a declared
    system sees on the tick after a transition. Amend ADR 0004 § "Game modes and the match
    lifecycle" and ADR 0005 § "Where zone and elimination state live".

- [ ] **Step 3: Export the seating helpers**
  - Verify: `./scripts/verify-focused 'unit.simulation|fixtures' && ./scripts/verify-focused 'unit.simulation|fixtures' linux-clang-asan-ubsan`
  - Notes: `spawn_seating.{hpp,cpp}` with `point_is_occupied(world, position, radius)` and
    `seat_body_at_rest(world, entity, position, configuration)`; `SpawnSystem` calls them. A
    refactor: the seating write and the predicate keep their expressions.

- [ ] **Step 4: Promote the five shared rules**
  - Verify: `./scripts/verify-focused 'unit.gameplay|fixtures' && ./scripts/verify-focused 'unit.gameplay|fixtures' linux-clang-asan-ubsan && test -z "$(ls src/gameplay/royale src/gameplay/sandbox | grep -E 'roster|next_free')"`
  - Notes: `shared/roster.hpp` (plus `participant_entities`, `participant_count`),
    `shared/lobby_start_rule.hpp`, `shared/disc_geometry.hpp` (from `zone_elimination.cpp`),
    `shared/spawn_point_probe.hpp` (the forward probe both policies repeat; royale's ring policy
    stays in `royale/` and calls it), and `shared/next_free_spawn_point_policy.hpp` with the
    post-`ended` lobby deferral added here and a test for it; the `RespawnTimer` deferral is
    Step 5's, once the kind exists. Tests move with their sources; the gameplay README's file
    lists are remeasured by their own stated method.

- [ ] **Step 5: Add `RespawnTimer`, the `respawn` system, and `match_reset`; open protocol 2.5**
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

### Phase 2 -- King of the hill, server

- [ ] **Step 6: Author the `[king_of_the_hill]` section**
  - Verify: `./scripts/verify-focused 'unit.gameplay|unit.application' && ./scripts/verify-focused 'unit.gameplay|unit.application' linux-clang-asan-ubsan`
  - Notes: `king_of_the_hill/king_of_the_hill_configuration.{hpp,cpp}` with the eleven keys and the
    rules in ADR 0007's table, `GameModeConfiguration::king_of_the_hill`, the loader's fields, and
    the section added to every configuration file in the tree with the proposed values. A test
    proves a configuration missing the section is refused naming it, and that `dwell + travel`
    converting to zero ticks is refused.

- [ ] **Step 7: Add `Hill` and `HillPresence`, and `hill_movement`**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol' && ./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol' linux-clang-asan-ubsan && ./scripts/verify-web`
  - Notes: Two kinds with their wire halves and client entries (`hill` visual at the zone layer,
    `hill_presence` non-visual). `hill_center(markers, dwell, travel, elapsed)` is a pure function
    with its own tests: one marker never moves, `travel = 0` hops, the glide's division precedes
    its multiplication, and the phase table's four rows. The entity creation fails hard on an
    empty reservation exactly as `zone_shrink` does.

- [ ] **Step 8: Declare `king_of_the_hill`**
  - Verify: `./scripts/verify-focused 'unit.gameplay|unit.protocol' && ./scripts/verify-focused 'unit.gameplay|unit.protocol' linux-clang-asan-ubsan`
  - Specialist: `rigorous-architect`
  - Notes: `hill_scoring`, `HillObjective`, `hill_rules_publisher`, the `king_of_the_hill`
    mode-state block with its schema and encoder, `KingOfTheHillMode`, and the registry row.
    Tests pin the scoring table line by line -- leaving loses, contest freezes, respawn forgets,
    `I = 0` scores on the first tick -- and the objective's order: a field of one before the
    scoreboard, a same-tick threshold is a draw, the clock ranks the leader. The mode's line count
    is measured for the README by the README's method.

- [ ] **Step 9: Map and replay fixtures for the hill**
  - Verify: `./scripts/verify-focused 'fixtures' && ./scripts/verify-focused 'fixtures' linux-clang-asan-ubsan && git diff --stat -- maps/arena-960x640 | wc -l | grep -qx 0`
  - Specialist: `testineer`
  - Notes: `maps/hills-960x640` with four `hill` markers and eight `spawn` markers, authored as
    literals with a README deriving them. The fixture reader gains `[king_of_the_hill]`; the
    bit-identity harness becomes mode-generic through `GameModeRegistry::create` and keeps every
    royale fixture. New fixtures: `hill-scripted-match` (first move tick, first point tick,
    threshold decision tick), `hill-contested` (no point while two hold it, a point once one
    leaves), `hill-time-limit` (a draw at the clock with level scores, then a win with one thrust
    more). Each runs 100 times bit-identical.

### Phase 3 -- King of the hill, client and bot

- [ ] **Step 10: Draw and score the hill in the client**
  - Verify: `./scripts/verify-web`
  - Notes: The hill renderer, `sessionSelectors` for the scoreboard join and the local progress
    fraction, the HUD's mode-aware section keyed by `match.mode`, and the results overlay reading
    the hill block. Every countdown is a tick difference from the frame; no timer in the client.
    Tests: the scoreboard order, a frame with no hill block renders the royale HUD unchanged, a
    `point_interval_ticks` of zero saturates rather than divides.

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
  - Verify: `./scripts/verify-focused 'fixtures' && ./scripts/verify-focused 'fixtures' linux-clang-asan-ubsan && git diff --stat -- maps/arena-960x640 | wc -l | grep -qx 0`
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
