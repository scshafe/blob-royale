# Plan: King of the hill and race modes

**Goal:** Two more games are registered, configured, mapped, drawn, botted, and proven: a hill that tours the map and scores the players holding it, and a checkpoint race whose fallen return to the last gate; the four framework amendments ADR 0007 names land first, each guarded by the accepted fixtures, and every existing mode plays exactly as it did.
**Out of scope:** Runtime implementation of the newly documented client camera (required follow-up before large-map playability); the other items in ADR 0007 § "What is deliberately not built": laps and closed courses, marker metadata authoring, generated respawn slots, a seeded hill tour, distance-along-course ranking, team variants, and per-room modes; royale on the shared restart wipe was tried inside Step 5 and kept.

## Context

The design is `docs/architecture/0007-king-of-the-hill-and-race-modes.md` (Accepted). This plan
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

- [x] **Step 11: Add the `hill_seeker` bot**
  - Verify: `./scripts/verify-focused 'unit.controllers' && ./scripts/verify-focused 'unit.controllers' linux-clang-asan-ubsan`
  - Notes: Seeded, decides from the snapshot's `hill` entity, one registry row and one CMake
    line. A test proves two seekers with different seeds do not submit identical directions.
  - Execution note (2026-09-09): `HillSeekerController` reads the first `Hill` entity of the snapshot and heads for its centre: a unit heading outside the hill and, inside it, the offset over the radius, so the pull reaches zero at the centre and the mode's drag holds the body there rather than a unit heading carrying it through. Its personality is an approach weight and a jitter weight (both in `[0, 1]`, refused otherwise under two new `CONTROLLERS.HILL_SEEKER_WEIGHT_*` codes), and every decision takes exactly two seeded draws so the draw count is a function of decisions. A world with no hill -- sandbox, royale -- gets no command. One registry row (the table is now three), one CMake line, the registry tests' pins moved to three names, and the controllers README's table and count. The tests seat a seeker in a real `king_of_the_hill` world through the gameplay stepping fixture and pin the outside heading, the proportional pull at forty units, rest at the centre, the approach weight, the clamp under full jitter, that two seeds differ and one seed repeats, and the two refusals. Verified at 56 of 56 on both lanes.

- [x] **Step 12: Prove the hill in a browser**
  - Verify: `./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`
  - Notes: `blob-royale-browser-e2e-hill.cfg` and `blobRoyaleBrowserHill.spec.ts`: join, seat a
    `hill_seeker` through the lobby menu, start, see the hill drawn and a score rise, see the
    match decided. The four existing flows stay green.
  - Execution note (2026-09-09): `blob-royale-browser-e2e-hill.cfg` on its own map `e2e-hill-1920x1280`: one hill marker at the centre with a 150 wu radius, the browser's spawn 510 wu clear of the edge, and the seeker's spawn 45 wu outside it, under the royale fixture's 9 wu/s cap; a two-seat lobby with no configured bot, a point a second, and three to win. `blobRoyaleBrowserHill.spec.ts` joins, reads the hill's HUD rows in the lobby (Score `0 of 3`, Hill `Off the hill`, no Placement row, its own scoreboard line), sees the hill drawn as exactly one disc of the hill fill at canvas (480, 320) radius 75, seats `hill_seeker` from the seat's menu, sees the seeker drawn moving toward the hill, presses Start, reads Time left while running, sees the seeker's scoreboard line reach 1 and then the match end at 3 with the overlay naming the winner and its points, and finds the winner drawn inside the still-frozen hill. Verified by the browser gate at 6 of 6 specs on the first attempt, the five existing flows among them.

### Phase 4 -- Race, server

- [x] **Step 13: Author the `[race]` section, the course value, and `RaceProgress`**
  - Verify: `./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol|unit.application' && ./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol|unit.application' linux-clang-asan-ubsan && ./scripts/verify-web`
  - Notes: `race/race_configuration.{hpp,cpp}` with the eight keys, the section in every
    configuration file, `race/race_course.{hpp,cpp}` with `distance_to_centreline` written out as
    ADR 0007 states it and the six map rejections, and the `race_progress` kind with its wire half
    and non-visual client entry. Geometry tests: a point beyond an endpoint clamps, a point on the
    line reads zero, a bend takes the nearer segment.
  - Execution note (2026-09-09): Added the eight-key `RaceConfiguration`, its required loader section in all thirteen complete configuration files and the application test template, and the validated `RaceCourse` with authored marker order, the ADR's written-out distance arithmetic, and all six map rejections. Geometry tests also pin the exact centre-only corridor boundary the ADR's map requirements specify. `RaceProgress` joins the component registry, schema, encoder, examples, generated client, and non-visual renderer under the open 2.5; the one-minor-ahead tests remain at 2.6. The focused C++ selection was widened with `fixtures` to guard the accepted replays: 802 of 802 passed on both GCC and Clang ASan/UBSan. The first GCC compile caught an unqualified nested `ApplicationConfigLoader::RunRequest` in the new loader test; it was corrected and reformatted before rerunning. The Linux web gate passed generation drift, schema examples, typecheck, lint, all client tests, and the production build. Every royale replay fixture and `maps/arena-960x640` remain byte-identical to the session's starting commit.

- [x] **Step 14: Declare `race`**
  - Verify: `./scripts/verify-focused 'unit.gameplay|unit.protocol' && ./scripts/verify-focused 'unit.gameplay|unit.protocol' linux-clang-asan-ubsan`
  - Specialist: `rigorous-architect`
  - Notes: `checkpoint_progress`, `track_bounds`, `standings_recorder`, `checkpoint_respawn`,
    `course_publisher`, the `race` block with its schema and encoder, `RaceObjective`, `RaceMode`,
    and the registry row. Tests pin: gates in order and one per tick; an off-road centre is out of
    play that tick; a racer with `next_checkpoint == 0` returns through the policy and one with
    `k >= 1` through `checkpoint_respawn` on the same relative tick; an occupied gate delays by
    one tick; a shared placement; the finish window and the clock ranking.
  - Execution note (2026-09-09): Registered the fourth mode with all ten systems in the ADR's order, the shared-rule-based grid policy, checkpoint return, finish standings, objective, and course publication. `validate_map` binds the course once during setup before the engine requests `systems()`; systems own immutable copies, and an unbound or unsuccessfully rebound mode fails explicitly without changing the engine interface. Tests prove ordered one-gate-per-tick progress, same-tick finish before off-road body removal, both return routes on `N + D + 1` for delays 0/1/3, an occupied gate delaying one tick, shared placements, solo time trials, finish-window precedence, and clock ranking including bodyless participants. The `race` state and closed schema/encoder landed under 2.5; the generic object sink now supports ordered arrays without exposing JSON implementation types. The first GCC run exposed two existing three-mode name-string assertions in addition to the known size assertions; both were updated, reformatted, and the full selection rerun. Verified 740 of 740 on both GCC and Clang ASan/UBSan with the filter widened to simulation and fixtures; the Linux web gate passed all 268 tests, generation drift, examples, typecheck, lint, and build. Existing royale replay data, its fixture test file, and the shipped arena remain byte-identical to the session's starting commit.

- [x] **Step 15: Map and replay fixtures for the race**
  - Verify: `./scripts/verify-focused 'fixtures' && ./scripts/verify-focused 'fixtures' linux-clang-asan-ubsan && git diff --quiet -- maps/arena-960x640`
  - Specialist: `testineer`
  - Notes: `maps/circuit-960x640`: a point-to-point course with one bend, three gates, a grid of
    four, two static obstacles, with a README deriving every literal. Fixtures:
    `race-scripted-course` (each gate's tick and the finish), `race-off-track-return` (the tick the
    body is gone and the tick it is back at the gate), `race-shared-finish` (two on one tick),
    `race-finish-window` (decided by the window, then by the clock in a second scenario).
  - Execution note (2026-09-09): Added the production-loaded `circuit-960x640` course with one bend, three ordered gates, four grid seats, and two colliding static obstacles; its README derives every authored literal. Extended the mode-generic replay reader with the strict `[race]` section and added five replay scenarios, separating the no-finisher clock case into `race-time-limit`. Independent motion/lifecycle oracles pin bent-course gates at ticks 22/62/127, off-road removal at 62 and at-rest gate return at 71, shared first placement at 48, a finish-window decision at 55 despite the nominal clock at 50, and a no-finisher clock decision at 42. Each scenario passes 100 fresh bit-identical runs. After C++ formatting, all 41 fixture tests passed on both GCC and Clang ASan/UBSan; the arena unchanged gate passed, and all existing royale replay data plus its fixture test remain byte-identical to the session's starting commit. Stopped after Step 15; Phase 5 has not begun and nothing has been pushed.

### Phase 5 -- Race, client and bot

- [x] **Step 16: Draw the course and the standings in the client**
  - Verify: `./scripts/verify-web`
  - Notes: The `modeStateRendererRegistry` seam (`@extension-point mode_state_renderer`, keyed by
    schema id, drawn once per frame before the entity layers) with the race entry drawing the
    corridor and the gates and non-visual entries for `none`, `royale`, and `king_of_the_hill`;
    the HUD's race section (gate `k of n`, standings, the return countdown from `respawn_timer`);
    the results overlay reading the race block. A test proves a frame whose block the registry
    does not know fails closed at validation, not in the renderer.
  - Execution note (2026-09-09): Added the exhaustive schema-id-keyed mode-state renderer registry, with the race course drawn once before entity layers using a round world-space stroke and distinct labelled finish. The renderer and HUD share one checked race-block reader. Race HUD and results cover gate counts, recorded standings and shared finishes, durable controller identity after entity removal, the finish window taking precedence over the race clock, and timer-free waiting for an occupied return point. Existing unknown-schema ingress rejection and registry/schema parity tests pin fail-closed dispatch. The pinned Linux web gate passed all 292 client tests, generation drift, schema examples, formatting, typecheck, lint, and the production build. Source review found no defects. Independent bot and browser work was prepared in parallel; commits remain step-scoped and ordered.

- [x] **Step 17: Add the `racer` bot**
  - Verify: `./scripts/verify-focused 'unit.controllers' && ./scripts/verify-focused 'unit.controllers' linux-clang-asan-ubsan`
  - Notes: Reads the course from the observation's mode state and its gate from `race_progress`;
    steers to the next gate, or back to the centreline past the caution fraction. A test proves it
    turns back when placed near the edge and drives on when centred.
  - Execution note (2026-09-09): Registered `racer` with one snapshot-only steering policy: seek the next checkpoint while within the inclusive caution boundary, otherwise seek the nearest clamped centreline point, keeping the earliest segment on ties. It waits through lobby/respawn, requests no duplicate entity, and writes zero thrust after finishing. The factory retains its seed but consumes no random draws. Eighteen new controller tests cover headings, boundaries, bends, endpoints, identity joins, and lifecycle states. Controller/fixture GCC selection passed 115 tests; after the corrective steps below, the full selection passed 1,133 of 1,133 on both GCC and Clang ASan/UBSan, including all controller and accepted replay tests. These pinned Linux runs are local emulated advisory checks, not authoritative deployment validation. Independent source review found no controller defects.

- [x] **Step 17b: Bound configured values before publication**
  - Verify: `./scripts/verify-focused 'unit.gameplay|unit.application|unit.protocol|fixtures' && ./scripts/verify-focused 'unit.gameplay|unit.application|unit.protocol|fixtures' linux-clang-asan-ubsan`
  - Notes: Review found that hill radius, race corridor half-width, and race gate radius accept values above the existing physical publication limit, and hill `points_to_win` accepts values above the protocol-safe integer limit. Reject them in their canonical configuration constructors using simulation-owned bounds and existing typed errors; prove each inclusive limit is accepted and its next value is rejected, and that accepted boundary values pass publication/schema validation. Defaults, schemas, and accepted replay data stay unchanged.
  - Execution note (2026-09-09): Configuration constructors now enforce the existing simulation ceilings: `10^12` for the three dimensions and `2^53 - 1` for hill winning points, preserving zero's specific error and authored-key contexts. Boundary tests accept each exact ceiling and reject its next integer; C++ serialization and actual client schema validation accept maximum-value hill/race frames. Both full C++ lanes passed all 1,133 tests. The pinned Linux web gate passed all 294 tests, generation drift, schema examples, formatting, typecheck, lint, and build. No defaults, schemas, or accepted replay data changed; independent review approved the correction.

- [x] **Step 17c: Clear hill progress on the knockout tick**
  - Verify: `./scripts/verify-focused 'unit.gameplay|fixtures' && ./scripts/verify-focused 'unit.gameplay|fixtures' linux-clang-asan-ubsan`
  - Notes: Review found that checking only already-bodyless entities at `kPostKernel` misses a zero-delay respawn: body removal occurs later at `kLifecycle`, and the next tick seats before scoring. Let hill scoring clear partial presence for this tick's elimination events after scoring, keeping mode-specific hygiene out of shared respawn. Add a composed regression with delay zero, partial presence, and an inside-hill return; prove it fails before the fix and starts the returned player's presence at one afterward. Existing score/point order and replay oracles stay unchanged.
  - Execution note (2026-09-09): Hill scoring now erases partial presence for this tick's `EliminationEvent`s after awarding completed points, then retains its existing bodyless cleanup. Shared respawn is unchanged. The composed two-tick regression drives the real contact, scoring, respawn, and phase-0 seating pipeline: two of four presence ticks before knockout, zero return delay, a clear inside-hill spawn, and a second participant. Its first fixture draft incorrectly assumed swept contacts; placing the hazard within contact range at the pre-integration pass corrected the setup. Against unfixed scoring, knockout and return prerequisites passed while retained knockout presence and the returned counter failed (6 of 8 assertions passed). With the fix, the returned player starts at one and retains exactly its earned score. Both full C++ lanes passed all 1,133 tests with accepted fixtures unchanged; independent review approved the scheduling fix.

- [x] **Step 17d: Complete generated integration configurations**
  - Verify: `./scripts/verify-focused 'integration' && ./scripts/verify-focused 'integration' linux-clang-asan-ubsan`
  - Notes: The first full sanitizer run exposed the configuration builder in `tests/integration/server_process_fixture.cpp`, missed by Steps 6 and 13's file updates. Add both required mode sections to this canonical builder without changing fixture modes or assertions. All three server processes must reach readiness and their dependent contracts must execute, with no skips or relaxed gates.
  - Execution note (2026-09-09): Added the eleven hill keys and eight race keys to `write_fixture_inputs`, using the existing defaults and keeping every selected mode and test assertion unchanged. Both full C++ runs passed 1,133 of 1,133 tests, including all nine integration entries: each server setup, its protocol/abuse-soak/session-reclaim contract, and cleanup. No integration tests were skipped or disabled. The earlier missing-section failure is recorded in the review artifact.

- [x] **Step 18: Prove the race in a browser**
  - Verify: `./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`
  - Notes: `blob-royale-browser-e2e-race.cfg` and `blobRoyaleBrowserRace.spec.ts`: join, seat a
    `racer`, start, see the corridor drawn, drive the local blob off the road and see it return,
    see the bot finish and the standings fill. The six existing flows stay green (seven total).
  - Execution note (2026-09-09): Added the race production-browser flow and a two-seat, three-gate map at 1920x1280, drawn at half scale. Extended the canonical canvas recorder with transformed stroke paths and draw order; the test verifies the corridor's round stroke, gate geometry, distinct finish, and course-before-body ordering. It seats a real `racer`, drives the browser off-road, observes countdown and body disappearance, proves at-rest return at the last gate rather than the starting grid, then sees first place, the finish window, and the recorded winner. The pinned Chromium gate passed all seven flows on its first complete run, with zero retries or skips; strict E2E typecheck passed. Independent source review found no browser defects. Local implementation is complete through this step; nothing has been pushed or deployed.

### Phase 6 -- Documentation, review, and deployment

- [x] **Step 19: Close the documentation**
  - Verify: human review
  - Notes: The 2.5 row in `docs/protocol/v2.md` and its `mode_state` paragraph; ADR 0004's what-if
    row for king of the hill corrected to the true count and its objective and `MatchState`
    sections amended with dated entries; ADR 0005's promotions amended; `src/gameplay/README.md`
    sections for both modes with measured line counts and the `shared/` inventory;
    `src/simulation/README.md` for the seating helpers and `previous_phase`; ADR 0007 to Accepted
    with the amendments execution taught it.
  - Execution note (2026-09-09): The owner approved the documentation with a larger-map/independent-viewport requirement and asked to continue. ADR 0007 is Accepted. The docs close the 2.5 implementation inventory while leaving deployment pending; record course binding, centre-only gate validation, deterministic racer steering, publication bounds, generated integration configurations, and zero-delay hill cleanup; and measure hill at 15 files / 1,160 physical C++ lines (mode 194), race at 20 / 1,188 (mode 168). ADR 0004 now owns the camera contract: world and viewport dimensions decoupled, one transform for all visual layers, strict centred player-follow, and manual panning/return-to-follow to evaluate with input choices left open. ADR 0007, protocol docs, and both frontend READMEs distinguish that accepted direction from the current full-map-fit implementation. Runtime camera work is an explicit follow-up prerequisite to large-map playability, not silently counted as implemented by Steps 16 or 18. This is the requested documentation alignment, not authorization to push or deploy.

- [x] **Step 20: Run every gate and review the diff**
  - Verify: `./scripts/verify-focused '.' && ./scripts/verify-focused '.' linux-clang-asan-ubsan && ./scripts/verify-web && ./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`
  - Specialist: `proofreader`
  - Notes: The review lands under `docs/reviews/` in the existing style; findings that are defects
    become steps inserted above this one, findings that are follow-ups become a list in ADR 0007's
    § "What is deliberately not built".
  - Execution note (2026-09-09): After the owner's documentation approval and camera amendment, reran the complete selection: 1,133 of 1,133 C++ tests on both GCC and Clang ASan/UBSan; all 294 web tests plus formatting, generated-protocol drift, schema examples, strict typecheck, lint, and production build; all seven Chromium flows on their first attempt with zero retries or skips. Web and browser commands ran through the pinned Linux wrapper. The independent review in `docs/reviews/2026-09-09-hill-and-race-review.md` closes all three correctness findings, reviews the camera documentation without claiming a camera implementation, and records the exact scopes and fresh evidence. Arena, existing replay data, and the royale fixture test remain unchanged from `cd5d90f`. These are local emulated advisory results, not authoritative deployment-host certification. Step 21 still needs explicit permission to push and deploy; neither has occurred. Camera implementation and large-map acceptance remain the named follow-up, outside what these compact-map browser flows prove.

- [x] **Step 20b: Bring the command fuzz harness up to date**
  - Verify: `./scripts/verify-fuzz-bounded 30 && ./scripts/verify-focused 'unit.protocol' && ./scripts/verify-focused 'unit.protocol' linux-clang-asan-ubsan`
  - Notes: CI run `34439646011` at `a55dca4` passed all four native builds, all 1,133 tests on GCC, Clang ASan/UBSan, and Clang TSan, the native startup smoke, all 294 web tests, and the browser gate. Its fuzz-target build then failed because `tests/fuzz/protocol_command_fuzzer.cpp` still calls the three-argument command decoder instead of supplying the session controller and available NPC kinds. Update the harness and its accepted-command invariants to the current decoder contract, retaining hostile-input checks and adding regression corpus coverage for supported command shapes. The bounded gate first replays every regression corpus, then mutates disposable copies for 30 seconds per target. The fuzz harness is Clang/libFuzzer-only; existing protocol unit coverage still runs on both required lanes. Do not weaken or skip the fuzz gate. Deployment has not started and Step 21 remains pending.
  - Execution note (2026-09-09): The harness now supplies distinct entity/controller stamps and a fixture-advertised NPC vocabulary, exercises both its original mode mask and the complete command mask, and checks every current client command variant while refusing server-issued alternatives. Eight new corpus inputs cover valid lobby commands and invalid seat bounds, NPC membership, and identity spoofing. Existing byte-limit, exception, exclusive-result, and thrust checks are retained; a future variant requires explicit oracle handling at compile time. Independent read-only review found no defects. After formatting, all six corpus sets passed (15 protocol inputs), all six 30-second fuzz campaigns passed, and all 104 protocol unit tests passed on GCC and Clang ASan/UBSan. Accepted arena and royale replay baselines remain untouched. This local evidence is advisory; the corrected push still requires its own CI result and the native release gate before deployment.

- [ ] **Step 21: Deploy the hill and play it**
  - Verify: `ssh ubuntu-tailscale 'cd ~/Projects/blob-royale && git pull --ff-only && curl -fsSI https://snapshot.ubuntu.com/ >/dev/null && ./scripts/deploy-tailnet'`, then the container, the `blob-royale.deployed-commit` label, readiness, the served bundle's protocol version `2.5`, and `GET /api/v2/lobbies` listing every room as `king_of_the_hill`, each checked independently of the script's own report.
  - Notes: Requires the owner's explicit go-ahead and pushed commits, as every deployment does.
    `deploy/ubuntu-pc/blob-royale.cfg` switches `[match] mode` and `map` to the hill for this
    playtest; the playtest note under `docs/playtests/` uses the template with the hill's balance
    rows added, and the race is played on the following session with the mode switched by
    `./scripts/reconfigure-tailnet`.
    These existing compact-map playtests do not satisfy the new large-map camera requirement;
    large-map acceptance requires the follow-up camera implementation and its own tests first.
  - Preparation note (2026-09-09): The owner explicitly authorized pushing and deploying the compact-map hill playtest. Switched the deployed mode/map to `king_of_the_hill` / `hills-960x640`, retaining four rooms, four seats, `wanderer:1`, and all balance values. The deployment fixture now loads the configured map and validates it through the configured mode, rather than silently checking the old arena; it also pins the intended hill mode/map. After formatting, all 41 fixture tests passed on both GCC and Clang ASan/UBSan. Preflight found the native host clean and reachable, noninteractive sudo available, and the Ubuntu snapshot mirror responding. Before replacement, the live label was `2b7ecd2f3ab881c4c7074c843898e109a71f6a4c`, release `release-ed8bf9e1a88167ab6e81d82239be9de7db865c44653bda2972b1e4ac534f932b`; the checkout was already at `380b5c9`. The old server reports protocol 2.2 and readiness; its absent rooms route is not a failure of the new deployment. Deployment and playtest verification remain pending, so this checkbox stays open.

## Done criteria

Every box above is ticked; `./scripts/verify-focused '.'` passes on both lanes with the accepted
oracle, every royale fixture, and every wall and pair fixture unchanged; `./scripts/verify-web`
and the browser gate pass with seven flows; `GET /api/v2/lobbies` on the tailnet reports the mode
the deployment names; ADR 0007 is Accepted with its execution amendments; and the playtest note
for the hill exists. Commits are pushed only when the owner says so.

**Amended 2026-09-09:** The three verify lines that asserted an empty `git diff --stat` through `wc -l | grep -qx 0` now use `git diff --quiet`, because macOS `wc` pads its count with spaces and the grep never matched even on a clean tree; the check was reporting a change that did not exist.

**Amended 2026-09-09 (continuation review):** Inserted Steps 17b and 17c for the publication-bound mismatch and zero-delay hill-presence bug found by source review. Browser expectations now name the six existing specs plus the new race spec; the gate already discovers the total dynamically. Independent implementation preparation overlaps, while verification notes and commits remain scoped to each step.

**Amended 2026-09-09 (full-gate evidence):** Inserted Step 17d after all three integration setup processes reported `APPLICATION.CONFIG.KEY_MISSING` for the new required sections. Their generated configuration builder, rather than the server or gate, was incomplete. The full sanitizer run's unit and replay suites passed; its integration gate did not.

**Amended 2026-09-09 (owner camera direction):** Accepted Step 19 following the owner's approval with the explicit camera requirement. Canonical documentation now separates world geometry from a movable client viewport, specifies centred follow, preserves manual pan as an option to evaluate, and records that neither camera mode is implemented yet. This mode plan continues through final verification; it does not relabel compact-map tests as large-map acceptance or expand deployment authority.

**Amended 2026-09-09 (authoritative fuzz build):** Inserted Step 20b after the first pushed hill-playtest CI run reached a stale fuzz caller that the focused and browser gates do not compile. The failure is a harness/API mismatch, not a reason to relax the release gate. Preserve the running deployment until the corrected commit passes CI and the native release profile.
