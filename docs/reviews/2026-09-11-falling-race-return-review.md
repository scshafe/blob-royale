# Step 17 verification review

Contract: [falling/race/return](2026-09-11-falling-race-return-contract.md).
Verification completed 2026-09-12. Step 16 baseline is local commit `932ffc6`.
All local results below are Mac/arm64-hosted Docker Linux/amd64 **advisory** evidence, not native
performance/release certification. No push or deployment. Logs: `/tmp/blob-royale-step17.83TlN8`.

## Completed checks

`./scripts/run-linux-toolchain -- ./scripts/verify-web` passed in `web-full-initial.log`:
769/769 tests, zero skipped/todo, 79 schemas/29 examples, generated drift, formatting, strict
typecheck, lint and production build. Existing large-bundle warning remains nonfatal.
The exact gate also passed on the frozen frontend in `web-final.log`, with the same counts.

The initial diagnostic typecheck exposed missing ground-attachment fields in handwritten test
bodies and optional-body guards in new mutation tests. These were explicitly supplied/guarded.
The initial full Vitest diagnostic passed 764/769: all five failures were old standings expectations
(three HUD rows, one projected-field equality, one same-tick tie message). Updated expectations
now assert separate certified tick/fraction values and exact-time tie wording; focused rerun passed
64/64 before the full gate. No production assertion was disabled.

## Review-driven corrections

- Support termination keeps its cursor unchanged; it fits a zero-width support declaration without
  overlapping race gate feature identities. Termination itself satisfies progress.
- The existing course publisher moves to first PreKernel before steering, preserving its standings
  while establishing finish locks even for directly seeded completed progress on the first tick.
- Startup-clearance tests explicitly configure gate radius 10 for their half-width-20 roads;
  otherwise course admission rejects before the intended player-radius clearance check.
- Snapshot/race canonical byte goldens publish all required fields. Generic fixture bodies remain
  explicitly floating; production seating/scenario players are explicitly ground-bound.
- The application registry documentation records the narrow race cross-value admission exception.
- Large coincident courses can still exhaust the unchanged solver work cap. This is named atomic
  refusal, not a promise of completion for every admitted map/trajectory.
- Hill scoring retains its existing PostKernel scoring-before-lifecycle-removal convention; a
  completed point on a knockout tick remains earned. This step prevents later solver impacts after
  falling but does not introduce fractional hill-presence scoring or change that ordering.

## Core, boundary, corpus, and browser evidence

The corrected original GCC gate passed 1,200/1,200 (1,152 unit and 48 fixtures) in
`gcc-correction3.log`. Read-only replay review tightened new analytical comparisons to absolute
1e-10 by disabling Catch's default relative epsilon; formulas and lifecycle assertions were sound.
The original Clang ASan/UBSan gate passed the same 1,200/1,200 in `clang-final.log`, with no
sanitizer errors. Both preserve every required fixture.
The exact application/runtime/server/controller supplement passed 494/494 on both lanes:
`supplement-gcc-final.log` and `supplement-clang-final.log`.
`./scripts/verify-fuzz-regressions` passed in `corpus-final.log`: 91 fixed-input executions over
seven ASan/UBSan harnesses. This is committed-corpus replay, not exploratory fuzz coverage.

The initial browser gate (`browser-final.log`) passed 12/13. Race completed its fall/return and
published a finisher, but the historical placement locator selected both the placement and newly
added finish-time cells. Narrowed it to the placement cell and added a separate live assertion for
the displayed safe-integer tick and finite normalized offset. No production behavior, timeout,
retry, or scenario command changed. Full web/browser gates were rerun after this test correction.
Both exact reruns passed: `web-browser-correction.log` (769/769 and all schema/build checks),
`browser-correction.log` (13/13 Chromium scenarios, zero retries, flakes or skips). The initial
failure screenshot was visually inspected: placement and the separate tick/fraction fit the HUD.

Benchmark review found that its retained prototype hash predates ground attachment. Keep that hash
unchanged for comparison, but independently require input/output attachment equality for both
reference results and each retained timed final output. The JSON publishes that invariant separately;
hash-field descriptions no longer imply the new field is hashed. These old workloads remain floating.

## Build and fixture corrections

Initial GCC build diagnostics (before any test execution): `gcc-initial.log` caught an older
CoursePublisher test aggregate missing the required MotionTime offset; it now explicitly uses
0.25. `gcc-correction1.log` caught a new standings test calling the private tick-close method.
Fixed it by modeling a subsequent tick using an explicit fresh test world with prior standings,
without exposing the kernel's commit API.

`gcc-correction2.log` then exposed older replay standing aggregates without the required offset.
Replay expectations now independently derive gate-entry fractions from the authored acceleration,
fixed step, radius and geometric tolerance. Original tick/rank/lifecycle checks and all five
100-run exact-snapshot loops remain; only descriptive CSV comments changed, not command rows.

The no-rebuild core diagnostic (`core-diagnostic.log`) passed 1,146/1,151. Two failures came from a
test factory's implicit same-ID controller on supposedly nonplayer bodies; these now explicitly
seed no controller. Two came from a stale Sandbox header's false claim that free play remains in
lobby: actual FreePlayObjective auto-starts, and tests/docs now preserve tick-one countdown and
tick-two running while proving all-phase falling and unchanged N+D+1 return. One raw JSON substring
expected decimal 0.25 although canonical Boost.JSON emits 2.5E-1; structural and numeric assertions
already passed. Corrected the byte expectation, not the serializer. No production lifecycle change.

## Benchmark comparison and final freeze

`./scripts/run-benchmarks-linux` passed its clean release build and all eight declared cases plus
delivery (`benchmarks-final.log`). The separate
[Step 17 artifact](2026-09-12-falling-race-return-baseline.json) preserves the complete new suite and
the 125-path implementation/input manifest. All retained correctness values, geometry/population/
density, workload definitions, deterministic work, and sample/per-sample operation counts match
Step 16. Delivery correctness also matches. All three pure-solver workload/work/retained correctness
records still match both historical prototype artifacts. Exceptions are explicit: the corrected
hash-field description and separate new attachment invariant, plus the required-unused Sandbox
section in royale configuration provenance. No native timing/capacity conclusion is drawn.

The Step 4/4a artifact SHA-256 values remain respectively
`c70c82927f96c157d15eea52b404366671b21c46a9cfb80ea1027d3da5272f31` and
`b7555359235b75330695ad7f32ca9bd32815205302d94a19e2b83f62b3adaaeb`.
The frozen crossing-hazard construction oracle remains
`129cd8b27e2ea888133c1a12667bda02b6c06f03b48f774b8449e73c9a6b51de`.

All 78 changed/new C++ paths passed pinned clang-format-18 dry-run/Werror (`format-check.log`).
The 125 implementation/input paths remained hash-identical after the final browser-only correction;
no production code changed after the passing C++ gates. The benchmark-only metadata assertion was
compiled in both lanes and exercised by the complete release runner. Git diff check passed.
Read-only cross-mode/replay reviews found no unresolved behavior defects after the recorded fixes.

## Exact acceptance commands

- `./scripts/verify-focused 'unit.gameplay|unit.simulation|unit.protocol|fixtures'`: 1,200/1,200.
- `./scripts/verify-focused 'unit.gameplay|unit.simulation|unit.protocol|fixtures' linux-clang-asan-ubsan`: 1,200/1,200.
- `./scripts/verify-focused 'unit.application|unit.runtime|unit.server|unit.controllers'`: 494/494.
- `./scripts/verify-focused 'unit.application|unit.runtime|unit.server|unit.controllers' linux-clang-asan-ubsan`: 494/494.
- `./scripts/verify-fuzz-regressions`: 91 fixed executions, seven harnesses.
- `./scripts/run-linux-toolchain -- ./scripts/verify-web`: 769/769; 79 schemas, 29 examples; all build checks.
- `./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`: 13/13; no retries, flakes, skips.
- `./scripts/run-benchmarks-linux`: eight cases plus delivery; retained correctness/work agree.

This completes Step 17's scope, not guarded contact/shield (Step 18), charge, later combat UI/bots,
or the native release gate. Existing hill scoring order and explicit solver work/precision bounds
remain; no new kernel seam, time root, respawn lifecycle, or fallback was introduced.
