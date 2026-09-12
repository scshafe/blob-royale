# Step 16 live continuous-motion adoption

Status: Step 16 verified and complete; all evidence is advisory, not native certification.

## Authority and boundary

The owner accepted the prototype boundary in Step 5 (`dbc99ad`) and then approved converting
the three oversized historical simulation workloads into explicitly labeled spatial-grid-only
diagnostics. Step 16 implements the
[integration contract](2026-09-11-live-motion-integration-contract.md). Its unused extraction and
binding proofs passed before reader cutover; the separate
[prerequisite review](2026-09-11-live-motion-prerequisite-review.md) retains that evidence.

The live engine now uses the reviewed continuous solver for phases 2–5. Frozen world facts are
post-intake/post-PreKernel; current motion is carried separately by subjects. Contact callbacks
consume certified observations, including per-source effect eligibility, and return the existing
motion result/disposition type. Mode-owned trigger tables are the eighth declaration and third
kernel policy socket. Termination prevents later same-tick motion and contact effects. The solver
does not publish its diagnostic selection trace as gameplay events or introduce a second path bus.

Sparse, body-bound `ContactEffectAdmission` stores only `any_touch`; absence is `closing_impact`.
One assignment/lookup/projection owner handles instance override over archetype default. Static
body declarations pair geometry with explicit policy; hazard creation uses the previously verified
extraction. Existing authored values retain closing-impact behavior with unchanged geometry,
ordering, and random draws. Publication is complete under coordinated v3.0, including closed
schema, body dependency, generated types, example, validation, and nonvisual renderer registration.

Effective body radii and the reviewed closed-envelope legality rule now have one owner across
solver, factory, endpoint grid, and final surviving bodies. Radius joins identity and position in
index invalidation. The live ceiling is 256 physical bodies including statics; standalone grid
diagnostics retain their previous larger capacity. Lowered resource budgets are test configuration,
not a new mode declaration or production tuning key. Exhaustion rejects the complete tick.

Production support/falling/race registration remains Step 17; guarded composition and deletion of
the temporary lethal row remain Step 18. No push or deployment is part of this work.

## Deliberate live expectation changes

- A fast pair now collides at the certified within-tick contact instead of crossing without an
  impulse. The pure discrete wrapper test remains unchanged.
- Equal-time pair order remains canonical, but an external pair response may re-enable an earlier
  touching pair. A wall response can likewise transfer its reflection through a re-enabled pair
  at the same instant. One global pair pass is no longer the live contract.
- Startup and endpoint-grid legality admit radius overlap when the center is inside the closed
  envelope. They still reject out-of-envelope centers under the explicit static/fold rules.
- Historical pure static-reflection tests retain their full frozen corpus. Live rows consume only
  actual impact certificates; nonimpulsive touch never fabricates an impulse.
- The frozen seven-phase `AcceptedBaselineTick` implementation remains historical. Live tests no
  longer assert universal bit identity with it; contact chronology and event-driven wall arithmetic
  are deliberately different. The tightened GCC test proves exact equality through tick 1,600,
  first divergence at 1,601, the expected reversed velocities in both models, the continuous
  positive-gap travel before reflection, and unchanged unrelated bodies (advisory evidence).
- The wall seed retains a positive representable gap at tick 2,000 and still points toward the
  wall. It reflects on the actual crossing in tick 2,001. The old tolerance snap reflected one
  tick early; live tests now assert the gap and both directions without widening tolerances.
- Maximum-population tests now distinguish 256 live physical bodies from 4,096 stored entities.
  The snapshot test preserves the larger identity population using bodyless controllables. The
  v1 encoder tests the maximum reachable live player projection, while its legacy 4,096-player
  wire ceiling and configuration goldens remain unchanged. No private snapshot factory or second
  engine was introduced to construct unreachable committed states.

## Benchmark comparison boundary

The suite still has eight explicit cases: `sparse_64` is `live_kernel`, historical royale is
`live_gameplay`, three large layouts are `spatial_grid_only`, and three reviewed prototypes are
`pure_motion_solver`. The grid cases keep their old geometry, population, density, initial pair
hash, and grid-rebuild operation counts, with historical names as provenance. Retired stepping,
post-step snapshot, and encoding operations produce no measurements. Unknown categories fail;
population never selects a fallback. Both original prototype baseline artifacts remain untouched.

Pure-solver comparison requires exact equality of workload definitions, all correctness fields,
and deterministic work against both Step 4 and Step 4a. Grid comparison uses the pre-cutover suite
JSON retained in `prereq-benchmarks.log`; only retained operations are comparable. Live timing
changes are not a native capacity or release claim. The complete new advisory suite is retained in
[`2026-09-11-live-motion-adoption-baseline.json`](2026-09-11-live-motion-adoption-baseline.json),
separate from both historical prototype artifacts.

The required clean release runner passed all eight cases and delivery. Exact comparison in
`final-cutover-benchmark-comparison.log` passed for all three prototypes against both historical
artifacts, and for all retained grid geometry/population/density/pair-hash/operation-count/sample-count
fields. Retired measurements are absent. The two live cases also retain every correctness value:
sparse-64 snapshot hash `fnv1a64:7d4249196a64ea5e`, encoded hash `fnv1a64:906d9aa5266618c3`, and
royale snapshot hash `fnv1a64:36ee7ca01d83ddb5`. This particular workload continuity does not restore
universal old/new physics equivalence. All benchmark measurements/comparisons are advisory.

## Verification ledger

All commands below run on Mac/arm64 hosting Docker Linux/amd64 and are **advisory**. Logs are under
`/tmp/blob-royale-step16.OUzlJW`. Pre-cutover results belong to the prerequisite review, not this
materially changed source.

Initial web verification passed 754/754 tests, zero skipped/todo cases, 79 schemas/29 examples,
generated drift, strict typecheck/lint, formatting, and production build in
`cutover-web-initial.log`. The final results below supersede that preliminary run.

The first GCC build completed, then discovery failed on the missing executable bit of the generated
`blob_server_unit_tests` file. A scoped `chmod 755` repaired that build artifact only. The rerun
in `cutover-gcc-discovery-fixed.log` passed 994/999, including all 48 fixture cases. A no-build
supplemental CTest diagnostic in `cutover-supplement-diagnostic.log` passed 655/659. Obsolete
physical-limit/envelope expectations, the new wall-test arithmetic, and new tick-zero encoder
fixtures explain those failures; they are being corrected without changing production behavior.
The exact wall/legacy chronology is verified separately before acceptance. Source review additionally
required entry-grid rollback observation, explicit fault contexts, successful moving controls,
moving retries for ordinary failures, and a pinned rather than ranged legacy divergence assertion.

The corrected focused diagnostic passed 17/18 (advisory) in `cutover-focused-corrections.log`.
Its remaining invalid-state probe targeted an entity already scheduled for removal; the commit
correctly removed it before survivor validation. The test now targets an entity not scheduled for
removal. No production change was needed. The final original GCC filter then passed **1,000/1,000**
(952 unit + 48 fixtures, advisory) in `final-cutover-gcc.log`. The exact original Clang ASan/UBSan
filter also passed **1,000/1,000** (952 unit + 48 fixtures, advisory) in `final-cutover-clang.log`.
The 126 changed/new implementation/input paths are captured in `final-cutover-source.sha256`;
the resulting benchmark evidence artifact is output, not a test input. Both exact supplemental
focused filters passed **659/659** (advisory) in `final-cutover-supplement-gcc.log` and
`final-cutover-supplement-clang.log`. Fixed-corpus replay passed **91 executions across seven
harnesses** (advisory): application 15, map 10, CLI 3, HTTP 3, command 29, request-id 29, scenario 2,
in `final-cutover-fuzz.log`. This is corpus replay, not a new fuzz campaign. Pinned formatting of
all 79 changed/new C++ paths and `git diff --check` passed. Final web passed **754/754** tests,
zero skipped/todo cases, **79 schemas/29 examples**, generated drift, strict typechecking/lint,
formatting, and production build (`final-cutover-web.log`). Chromium passed **13/13** with no
retries, flakes, or skips (`final-cutover-browser.log`). These are advisory results. Expected
readiness/reset proxy messages occurred around deliberate server lifecycle transitions; the
browser result gate passed every declared case on its first attempt. The existing bundle-size
warning remains a warning, not a threshold change or dependency upgrade.

The closing manifest check confirms all 126 implementation/input paths stayed unchanged throughout
the final sequence (`final-cutover-hash-check.log`), and the saved benchmark artifact's suite is
structurally identical to the runner output. No accepted gameplay replay outcome or frozen legacy
implementation changed. The pure discrete crossing wrapper remains intact. Step 16 is complete;
no native performance/release certification, push, or deployment is claimed.

Required final commands:

```sh
./scripts/verify-focused 'unit.simulation|unit.gameplay|fixtures'
./scripts/verify-focused 'unit.simulation|unit.gameplay|fixtures' linux-clang-asan-ubsan
./scripts/run-benchmarks-linux
./scripts/verify-focused 'unit.application|unit.protocol|unit.runtime|unit.server|unit.controllers'
./scripts/verify-focused 'unit.application|unit.protocol|unit.runtime|unit.server|unit.controllers' linux-clang-asan-ubsan
./scripts/verify-fuzz-regressions
./scripts/run-linux-toolchain -- ./scripts/verify-web
./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e
```

Root serialized all C++ builds. Final pinned formatting, `git diff --check`, source-hash stability,
and explicit comparison checks all passed before marking Step 16 complete.

## Source review

Independent source review found no remaining defect in frozen-world lifetime, synchronous
callback ownership, startup/intake/late body caps, transactional result/event transfer, pair
orientation and disposition mapping, radius-aware index invalidation, or sparse policy projection.
Benchmark review confirmed explicit categories, unchanged retained work/layouts, no execution of
retired operations, and untouched pure-solver work/hash code. Two provenance wording findings were
corrected before verification. Source review is not a replacement for executable evidence.
