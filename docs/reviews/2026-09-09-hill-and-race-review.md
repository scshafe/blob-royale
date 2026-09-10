<!-- canonical: hill_and_race_review -- independent review of ADR 0007 implementation and its corrective verification -->

# Hill and race review, 2026-09-09

Independent source review against `docs/architecture/0007-king-of-the-hill-and-race-modes.md`
and `.claude/plans/2026-09-09-king-of-the-hill-and-race-modes.md`. The review passes cover:

- **Steps 16–17:** the initially uncommitted client course renderer, HUD, results selectors, racer controller,
  and their tests against `cd5d90ff2077ccd499d9d486c874a304b5070e75`.
- **Steps 1–15:** server framework, shared respawn/reset/seating, hill/race gameplay, configuration,
  and publication in `6e697b3..cd5d90f`. An independent child review covered hill rules and their
  publication; the main review covered shared framework and race rules.
- **Steps 18–19 follow-up:** the race browser recorder/specification/configuration/map and the
  documentation edits available after the earlier passes, including the final correction-related
  documentation and refreshed inventory counts.

**Status: scoped mode implementation approved; all fresh Step 20 gates passed.**
The client/controller pass found no concrete defect. The owner's acceptance requires documentation
of larger worlds and an independent client viewport; camera implementation is required follow-up,
not completed work. Manual-pan UX remains under consideration.
The server pass found two correction groups: accepted configuration values can exceed their wire
types, and zero-delay hill respawn can retain a partial point. The root executor's subsequent full
Clang gate exposed a third: the integration server configuration builder omitted the two newly
required mode sections. These are correctness and verification findings against existing contracts.
They require no change to the intended games. All three corrections are now implemented and pass
both full C++ lanes; the final browser gate also passes. The findings below retain their original
failure descriptions as review evidence, not unresolved defects.

The implementation follow-up range is `cd5d90f..d470684` plus the six original documentation files:
ADRs 0004, 0005, and 0007, `docs/protocol/v2.md`, `src/gameplay/README.md`, and
`src/simulation/README.md`. The owner's camera-direction follow-up also changes
`frontend-react/README.md`, `frontend-react/src/features/simulation/README.md`, and the plan. The
earlier server review covers `6e697b3..cd5d90f`. Step 19 documentation and the plan are now committed
as `5caf832`; the complete reviewed implementation/documentation range ends at that commit.

The server finding line numbers below refer to `cd5d90f`, before remediation. Reviewers performed
no builds, tests, or source writes. This artifact is the only authorized documentation write;
implementation and verification belong to the root executor. The review had no prior project
memory and used the ADR, plan, source, and tests as its context.

## What I see

The framework gains engine-owned `previous_phase`, a tick-aware objective, shared respawn/reset,
and extracted seating operations. Hill scores presence in a deterministic touring circle. Race
records ordered checkpoints and finishes, removes off-course bodies, and returns them through the
grid or last checkpoint. The client reads these published values and draws course geometry before
entity layers. The racer selects its next gate or the nearest centreline recovery point.

## Apparent intent

The implementation follows ADR 0007's declared system boundaries and evaluation order. The client
and bot implement plan Steps 16–17. The simple racer steering deliberately permits falls and returns;
velocity prediction and more elaborate path following are not requirements of this review.

## 1. Does this seem correct?

### Finding 1 — P2: accepted configuration can exceed published types

| Field | Acceptance gap at review | Concrete failure | Required existing ceiling |
|---|---|---|---|
| `[king_of_the_hill] hill_radius_world_units` | Only finite/positive, `src/gameplay/king_of_the_hill/king_of_the_hill_configuration.cpp:29-44` | `1000000000001` is accepted but the hill encoder rejects its radius. | `simulation::kMaximumPhysicalComponentMagnitude` |
| `[race] track_half_width_world_units` | Only finite/positive, `src/gameplay/race/race_configuration.cpp:19-35` | `1000000000001` on an otherwise valid course is accepted but every race snapshot fails encoding. | Same |
| `[race] checkpoint_radius_world_units` | Finite/positive and no greater than half-width, `src/gameplay/race/race_configuration.cpp:35-41` | A radius and half-width both above the wire ceiling are accepted but fail encoding. | Same |
| `[king_of_the_hill] points_to_win` | Only rejects zero, `src/gameplay/king_of_the_hill/king_of_the_hill_configuration.cpp:63-68` | `9007199254740992` is accepted and emitted unchanged; the client rejects the snapshot's unsafe integer. | `simulation::kMaximumProtocolSafeInteger` |

The existing ceilings are `1e12` and `2^53-1` (`src/simulation/simulation_limits.hpp:15,22`).
Radius publication enforces its ceiling in `src/protocol/component_wire_bound.hpp:28-44`, called
by `src/protocol/components/hill_component_encoding.hpp:18` and
`src/protocol/mode_state_wire_encoding.hpp:146-149`. Hill points are emitted without an upper-bound
check (`src/protocol/mode_state_wire_encoding.hpp:129`;
`src/protocol/protocol_v2_json_encoding.cpp:66-67`), but the published schema uses
`common.schema.json`'s safe integer (`docs/protocol/schema/v2/common.schema.json:42-45`).

**Correction required:** enforce those ceilings at configuration acceptance, retaining the existing
fail-closed publication behavior. Test acceptance at the exact ceiling and rejection above it with
the field's named context; verify publication of accepted boundary values.

The analogous-bound audit found no further gap in generated state. All durations use
`duration_ticks` and the safe tick ceiling (`src/gameplay/shared/duration_ticks.cpp:33-41`;
`src/simulation/tick_sequence.hpp:17`). Gate progress and standings are bounded by marker/entity
limits. Generated hill score and presence increase at most once per bounded simulation tick.
Hill interpolation remains between arena-constrained markers.

### Finding 2 — P2: zero-delay hill return preserves partial progress

`src/gameplay/king_of_the_hill/hill_scoring_system.cpp:95-105` clears presence only from entities
already without bodies. Scoring runs at `kPostKernel`; respawn removes bodies later at `kLifecycle`
and attaches no timer for delay zero (`src/gameplay/shared/respawn_system.cpp:74-77`). The next
tick's phase-0 seating can restore a body inside the hill before scoring observes it
(`src/simulation/game_simulation.cpp:808-811,871-872`). Its pre-knockout partial point then continues.

This contradicts ADR 0007 § Scoring: being knocked out forgets partial progress. The ordinary
positive-delay case does not expose the gap because scoring observes a bodyless tick before return.

**Correction required:** after awarding this tick's points, hill-owned cleanup must also erase
presence for this tick's eliminated entities. Keep shared respawn independent of hill components.
A composed regression should use delay zero, a point interval greater than one, two participants,
and a free spawn inside the hill; the returned player's presence must restart at one.

### Finding 3 — P2: integration server fixtures omit required mode sections

The root executor reported that full Clang verification run `1125` found integration server
fixtures exiting before readiness with `APPLICATION.CONFIG.KEY_MISSING`. Their generated
configuration in `tests/integration/server_process_fixture.cpp`, `write_fixture_inputs`, appended
`[royale]` directly before `[lobbies]`, omitting `[king_of_the_hill]` and `[race]`. The strict
application configuration now requires both sections regardless of the selected mode
(`src/application/application_config_loader.cpp`, `StrictIniDocument::require_all_fields` and
the `GameModeConfiguration` construction).

The failing fixtures therefore never exercised their intended server contracts. This is a fixture
migration omission associated with the new required sections, not evidence that the unchanged
integration assertions or their selected sandbox/royale modes are wrong.

**Correction required:** add complete required hill/race sections to the existing configuration
builder without changing its selected modes or test assertions; rerun the integration/full gates.
The root executor added plan Step 17d for this correction. Run `1125`'s failure is root-provided
evidence; the reviewer inspected the configuration builder patch, not that run's logs.

### Confirmed behavior within scope

- The course renderer scales geometry and stroke together and restores the canvas before entity
  layers (`frontend-react/src/features/simulation/rendering/raceCourseRenderer.ts:26-74`).
- Race schema narrowing matches accepted wire bounds. Results use durable controller identity;
  bodyless returns remain visible; the finish window replaces the race clock
  (`frontend-react/src/features/simulation/sessionSelectors.ts`, `raceModeState`, `raceHudReport`).
- Racer decisions join body/progress by the observed entity, wait through body absence, clamp
  segment projections, retain earliest-segment ties, and emit zero thrust after finishing
  (`src/controllers/racer_controller.cpp:27-57,89-140`).
- No further concrete defect was found in previous-phase propagation, shared timer/reset ordering,
  the seating extraction, race gate/return/objective ordering, or hill tour/contested scoring.
- The race browser proof uses a production server and browser input. Its uniform 0.5 projection
  assertions distinguish corridor, gates, and bodies; it observes off-road absence/countdown,
  checkpoint-centre return, the actual racer finishing, the finish window, and the ended result
  (`frontend-react/e2e/blobRoyaleBrowserRace.spec.ts`; its configuration and
  `fixtures/maps/e2e-race-1920x1280/map.cfg`). No concrete defect was found by reading this flow.

## 2. Is it easy to ascertain whether it's correct?

Race return tests exercise both routes through actual ticks and occupied gates
(`tests/unit/gameplay/race/race_return_timing_tests.cpp`). Racer tests use literal headings for
caution boundaries, bends, and endpoints (`tests/unit/controllers/racer_controller_tests.cpp`).
Unknown mode-state schema rejection already has an ingress test
(`frontend-react/src/features/simulation/sessionProtocolValidation.test.ts`,
`fails closed on an unregistered mode-state schema id`).

The hill knockout test masks Finding 2's scheduling case: it manually removes a body before calling
scoring (`tests/unit/gameplay/king_of_the_hill/hill_scoring_system_tests.cpp:172-184`). It proves
bodiless cleanup but cannot establish cleanup across production zero-delay reseating.

Canvas mocks establish drawing calls and save/restore ordering, not actual restored canvas state or
pixel appearance (`frontend-react/src/features/simulation/rendering/raceCourseRenderer.test.ts`).
The source enclosure is correct; browser evidence remains a separate verification gate.

The extended browser recorder reads actual canvas calls and the current transform
(`frontend-react/e2e/browserFlowSupport.ts`, `installCanvasRecorder`). It establishes drawing order
and transformed geometry for the uniform-scale race fixture; it is not screenshot/pixel comparison.

The first source pass on the composed hill regression missed a fixture defect. Its hazard began
22 units from the player and expected crossing the 20-unit contact distance during integration to
eliminate in tick one. Contacts are discrete and evaluated before integration
(`src/simulation/game_simulation.cpp:854-857`), so that expectation was false. The reviewer retracts
that part of the initial fixture approval. The initial before-fix log
`/tmp/blob-royale-hill-regression-before.log` confirms failure at the prerequisite body-absence
assertion, not at presence cleanup. That initial log was subsequently replaced by the corrected
before-fix run at the same path. The root corrected hazard x=502 to x=499: its 19-unit initial
separation now triggers contact, and its continuing motion stays clear of return x=440 on tick two.
The corrected log was inspected: 6 of 8 assertions pass, while presence incorrectly remains after
knockout and is absent after reseating. The latter matches the old scoring code's premature point
award; the test stops at the presence assertion before reaching its final score assertion.

## 3. Is the work well-founded?

Yes. The registry seam, published rankings, controller policy, and shared mechanics follow the
plan and ADR 0007. The gameplay findings are gaps in implementing those existing contracts. Fixing
them at configuration acceptance and in hill-owned cleanup preserves the intended boundaries.
Completing the integration configuration migration restores the existing server tests' prerequisites.
Documentation edits read so far accurately describe setup-time course binding, schema-keyed client
selection, gate-disc overlap with the off-road region, shared promotions, and source implementation
versus pending deployment. Refreshed production inventories were independently counted: 15 hill
C++ files / 1,160 lines and 20 race C++ files / 1,188 lines, including comments/blanks. Correction
prose matches the four bounds and post-score cleanup. Following owner approval, ADR 0007 is Accepted
with the explicit camera follow-up described below.

### Owner camera direction — documentation review, 2026-09-09

The owner accepted Step 19 with a larger-map/independent-viewport direction. ADR 0004's
"World space and the client viewport" section records a planned client camera, one transform for
entity/course layers, centred body-follow resolved through controller identity, and screen-space
HUD/controls. Manual pan and return-to-follow are options to evaluate; bindings/defaults remain
undecided. This is documented future behavior. `SimulationCanvas.tsx` still derives a scale from
the complete world dimensions, and `WorldProjection` has only horizontal/vertical scale fields.
No movable camera, follow state, or manual-pan input exists in the reviewed implementation.

ADR 0007, the protocol wording, and both frontend READMEs correctly distinguish complete snapshot
validation from intentional viewport clipping. The two README links resolve to the canonical ADR
0004 camera section. The existing compact-map/browser evidence does not demonstrate large-map
playability; camera implementation and dedicated coverage remain a prerequisite to that claim.

One wording inconsistency was corrected during review: ADR 0004 initially required manual-pan
tests unconditionally while manual view remained under consideration. The inspected final text
now requires those tests only "If manual view ships". Required camera acceptance still covers a
larger world, translated layer alignment, centred follow at edges/after respawn, and independent
views. The documentation follow-up is approved within the owner's stated direction.

## Smells

- **Test that proves nothing about the failing lifecycle:** the standalone knockout test exercises
  bodiless cleanup, not the stage ordering its name implies (`hill_scoring_system_tests.cpp:172-184`).
- No other substantive smell was established in the reviewed gameplay/client scope. Finding 3
  concerns a missed migration prerequisite exposed by the broader gate.

## Baseline preservation and scope limits

Read-only `git diff --name-only 6e697b3..cd5d90f` checks found no changes to
`maps/arena-960x640`, existing royale replay data, or `tests/fixtures/royale_replay_fixture_tests.cpp`.
Changes beneath `tests/fixtures/replays` in that range are added hill/race scenarios. This is a
source-preservation check, not an independent rerun of replay determinism.

A post-remediation `git diff --name-only cd5d90f` check against the working tree is empty for those
same paths, including all replay data. The implementation/fix commit sequence is
`dae7662` (client), `1d2e434` (racer), `6adff10` (publication bounds), `c84e2fb` (hill cleanup), and
`4782630` (integration configuration), and `d470684` (browser proof). The same preservation check
was repeated after `d470684` and remained empty. Step 19's eight documentation files and plan are
committed in `5caf832` following owner acceptance with camera direction. The baseline check was
repeated after all fresh Step 20 gates with HEAD at `5caf832` and remained empty. This review artifact
is prepared for the root executor's Step 20 closeout commit; the reviewer has made no commit.

Steps 18–19 browser/documentation edits were concurrent and excluded from the first pass; their
available changes received the later source pass described above. This is not a security review,
deployment certification, numerical re-derivation of every fixture, or
independent execution of the required gates. No generated-state overflow claim covers arbitrary
manually corrupted component stores.

## Remediation and verification record

The first verification set below was recorded on 2026-09-09 before the owner camera-direction
documentation update. Fresh Step 20 results are listed separately after it. The camera update
changes documentation, not runtime code.

- [x] Source-review the configuration-cap implementation and boundary regressions. All four fields
  use the existing simulation bounds; exact-limit acceptance and limit-plus-one rejection preserve
  the named configuration contexts. Post-fix GCC and Clang execution passed (`6adff10`).
- [x] Source-review the hill cleanup implementation and composed zero-delay regression. The new
  event cleanup runs after points are awarded, erases only `HillPresence`, does not mutate the
  event list being traversed, and makes duplicate eliminations harmless. Header documentation
  states the same ordering. Post-fix GCC and Clang execution passed (`c84e2fb`).
- [x] Source-review `tests/unit/gameplay/king_of_the_hill/hill_return_timing_tests.cpp`. It uses the
  real mode/contact/respawn/seating pipeline and starts at two of four presence ticks. Initial
  approval missed the discrete-contact fixture error documented above; the corrected x=499 setup
  has contact at tick one's contact phase and a clear inside-hill return on tick two. The corrected
  before-fix execution reproduces the intended defect, as recorded above.
- [x] Source-review the integration builder correction: both required sections are added before
  `[lobbies]`; selected modes and integration assertions are unchanged. Post-fix integration
  executions pass on GCC and Clang (`4782630`).
- [x] Record post-fix GCC evidence: `/tmp/blob-royale-race-gcc-final.log` was inspected and reports
  `linux-gcc-debug`, 1,133/1,133 passed, including the composed hill regression and all nine
  integration setup/test/cleanup entries. The root reported exit zero; the log ends with
  `verify_focused.status=passed`.
- [x] Record post-fix Clang evidence: `/tmp/blob-royale-race-clang-final.log` was inspected and
  reports `linux-clang-asan-ubsan`, 1,133/1,133 passed, including the composed hill regression and
  all nine integration setup/test/cleanup entries. The root reported exit zero; the log ends with
  `verify_focused.status=passed`.
- [x] Record client verification separately from source review: the root's
  `/tmp/blob-royale-race-web-final.log`, whose completion output was inspected, reports 294/294
  passed, zero failed/skipped/todo, and passing formatting, protocol generation drift, schema
  examples, strict typecheck, lint, and production build. This includes client schema acceptance
  of the configured publication ceilings. It is not browser or C++ verification.
- [x] Source-review the completed Step 18 race flow and its shared canvas recorder changes.
- [x] Record final browser evidence: `/tmp/blob-royale-race-browser-final.log` was inspected and
  reports Chromium 7/7 passed in 1.7 minutes, zero retries, and zero skipped tests. All flows ran
  once: lethal hazard, heavy hazard, hill, race, recovery, rooms, and royale. The root reported
  exit zero; the log ends with `verification=browser-e2e` and `status=passed`. This verifies the
  production-browser flow in `d470684`, including the shared recorder changes.
- [x] Review correction-related documentation and refreshed inventory counts in ADRs 0004/0007
  and the gameplay README. Bounds, cleanup ordering, integration configuration cost, and measured
  counts match the inspected implementation.
- [x] Recheck baseline preservation after remediation through `d470684` and the working tree;
  the checked arena/royale fixture/replay paths have no changes from `cd5d90f`.

The C++, web, and browser results are local advisory evidence. Both C++ lanes explicitly report
`verify_focused.authority=advisory` and `Darwin/arm64 docker=linux/aarch64`; the pinned Linux x86_64
toolchain binaries run through local Docker emulation. Passing these checks does not constitute
authoritative deployment-host validation. No deployment validation or deployment was performed.
### Fresh Step 20 verification after owner acceptance, 2026-09-09

- [x] GCC: `/tmp/blob-royale-step20-gcc.log` was inspected; `linux-gcc-debug` reports
  1,133/1,133 passed and `verify_focused.status=passed`. The root reported exit zero. This fresh run
  retains the same advisory authority and Darwin/arm64, Docker linux/aarch64 environment.
- [x] The root reported a passing targeted Prettier check for the changed frontend READMEs before
  starting Clang. This targeted check is not substituted for the full web gate below.
- [x] Clang: `/tmp/blob-royale-step20-clang.log` was inspected; `linux-clang-asan-ubsan` reports
  1,133/1,133 passed and `verify_focused.status=passed`. The root reported exit zero. Its authority
  and host remain the same local advisory/emulated environment as GCC.
- [x] Web: `/tmp/blob-royale-step20-web.log` was inspected; all 294 tests passed with zero
  failed/skipped/todo, and formatting, generation drift, schema examples, strict typecheck, lint,
  and production build passed. The log ends with `verification=web`, `status=passed`.
- [x] Browser: `/tmp/blob-royale-step20-browser.log` was inspected; Chromium reports 7/7 passed
  in 1.6 minutes, zero retries and zero skipped tests, followed by
  `verification=browser-e2e`, `status=passed`. The root reported exit zero. Each of the seven flows
  passed on its first attempt.

All fresh Step 20 gates are complete. The earlier verification set remains as historical evidence.
Both sets are local advisory checks under the environment qualification above. No camera
implementation, large-map acceptance, deployment-host validation, push, or deployment is claimed.

## Questions

- No unresolved mode-implementation or documentation-consistency question. Manual-pan UX and
  camera implementation remain future work.

## Verdict

Approved within the stated scope. The client/controller/browser paths and documentation follow the
plan; all three findings are resolved by the inspected corrections and the earlier passing
verification. No unresolved mode-implementation defect was found. The owner subsequently accepted
Step 19 and ADR 0007 with the documented camera direction. That acceptance does not claim a camera
exists, settle manual-pan UX, or authorize deployment. The camera documentation is consistent with
that scope after the conditional manual-test correction. Both fresh C++ lanes, the full web gate,
and the seven-flow browser gate pass. The review supports completing Step 20 through `5caf832`.
Step 21 push/deployment approval and the required future camera implementation remain outside this
completed source/verification review.

## Notes to remember

- Body-bound cleanup must account for phase-0 reseating.
- Accepted published configuration values must satisfy the wire's bounds.
- Recorded race controller identity survives entity removal.

## References

- `docs/architecture/0007-king-of-the-hill-and-race-modes.md`: shared respawn, hill scoring,
  race returns/objectives, client, and bots.
- `.claude/plans/2026-09-09-king-of-the-hill-and-race-modes.md`: Steps 1–17 and verification gates.
- Source and test positions cited above; server finding positions are pinned to `cd5d90f`.

Confidence: medium — scoped source, documentation, and all fresh local verification evidence support approval; deployment validation is excluded.
