# Step 16 prerequisite checkpoint

Status: prerequisites implemented and verified on both advisory lanes and the advisory benchmark
suite. Step 16 is not complete and no production simulation, map, component registry, hazard
spawning, or protocol reader is switched. Changes remain uncommitted pending the Step 16 cutover.

## Implemented boundary

- The generic solver accepts optional borrowed per-trigger facts for both query and response;
  pair responses retain the original global facts. Existing six-field declarations keep the
  original reference and behavior, including abstract/noncopyable facts.
- The existing limit validator is exposed verbatim for shared admission, with unchanged solver
  delegation. An unused body-envelope candidate preserves the original static-before-cross
  branches and written arithmetic; an independent frozen source block proves equivalence.
- An unused crossing-hazard creation operation preserves draw/body/lifetime/lethal creation
  order. A separate frozen creation/schedule reference and earlier crossing oracle check full
  world/RNG/reservation and exact body outputs before scheduled spawning may delegate.
- Independently owned immutable trigger policies bind to ascending body IDs then authored
  declaration order. Move-only bound owners retain stable facts loans. Constructor/binding
  limits and checked capabilities fail visibly; geometry, ordering and overlap validation stay
  in the canonical solver.

Source reviews found and corrected temporary-facts borrowing: both facts factories now reject
rvalues, alongside the existing temporary-table and temporary-row borrow rejection. The ownership
proof additionally destroys a mode-like declaring object before binding and solving with its
returned table. No actual GameMode declaration has been added yet.

## Verification

The first GCC and Clang ASan/UBSan focused runs each passed 972/972 tests on Mac/arm64-hosted
Docker Linux/amd64 (advisory). Review then added the declaring-owner destruction proof. Only the
fresh final chain below is evidence for the final source state; the earlier passing runs are not
substitutes. No native capacity, performance, or release certification is claimed.

Final chain is recorded under `/tmp/blob-royale-step16.OUzlJW`:

- `./scripts/verify-focused 'unit.simulation|unit.gameplay|fixtures'`: 973/973 passed,
  `final-prereq-gcc.log` (advisory).
- `./scripts/verify-focused 'unit.simulation|unit.gameplay|fixtures' linux-clang-asan-ubsan`:
  973/973 passed, `final-prereq-clang.log` (advisory).
- `./scripts/run-benchmarks-linux`: passed all eight existing case objects and delivery
  correctness checks, `prereq-benchmarks.log` (advisory).

The final focused selection contains 925 unit tests and 48 fixture tests in each advisory lane.
All 16 changed/new C++ paths are pinned in `final-prereq-cpp.sha256`; the manifest was checked
unchanged after the full final chain (`final-prereq-hash-check.log`). Pinned clang-format dry-run
and `git diff --check` passed (`final-prereq-format-check.log`). Existing accepted fixture geometry and expectations,
historical numerical oracles and the Step 4/4a baseline artifacts remain untouched.

An exact structural comparison against both preserved Step 4 and Step 4a artifacts passed for
the three pure-solver workload definitions, all correctness fields, and all deterministic work
fields (`prereq-benchmark-comparison.log`, advisory):

- `continuous_motion_charge_speed`: `fnv1a64:53a16ad6bc6a44a0`.
- `continuous_motion_dense_32_holes`: `fnv1a64:8f206d66b10f4e84`.
- `continuous_motion_dense_shield_contacts`: `fnv1a64:8d567eb367dea076`.

Timing samples are not an equality oracle or a native performance certificate. The existing full
benchmark JSON is retained in the transcript log and generated `out/benchmarks` output; no old
baseline artifact was overwritten.

## Pre-cutover benchmark contract issue

The source-backed preflight found that `sparse_512`, `sparse_2048`, and `clustered_512` are full
GameSimulation workloads, not grid-only measurements. Their construction and stepping exceed
the adopted solver's maximum 256-body envelope. The current mandatory benchmark runner will
fail at `sparse_512` after live adoption and will not reach the later royale/prototype cases.
See `benchmarks/blob_simulation_benchmarks.cpp`, `benchmark_scenario`, `run_to_final_snapshot`,
`measure_simulation_steps`, and the declarations in `run_benchmarks`.

Disposition approved by the owner's subsequent "yes" on 2026-09-11:

1. Preserve historical source definitions, counts, geometry/layout arithmetic and committed
   baseline artifacts. Do not raise solver caps, truncate populations or retain a legacy engine.
2. Keep sparse-64 and historical royale as live-engine cases. Keep all three pure-solver cases
   and their full-result hashes/work unchanged.
3. Rename the three oversized cases explicitly as spatial-grid-only diagnostics, with former
   names as provenance. Reuse existing world/config/grid-measurement code on the original
   geometry. Emit only grid timing/count/hash evidence and explicit retirement reasons for
   stepping, post-step snapshots and encoding. Never manufacture replacement final snapshots.
4. Classify workloads explicitly, not through a fallback based on whichever cap happens to be
   current. Compare only retained grid operations to their old results; whole-case timings and
   final snapshots are not comparable. Preserve all eight historical case identities in the
   dated old artifacts rather than rewriting those artifacts to describe the new suite.

This affects the benchmark executable, its README, the Step 16 integration contract and the plan.
The executing-plans review pause is now resolved. The owner-approved disposition is recorded in
the integration contract before benchmark edits or live reader cutover; implementation and full
final verification are still required.

## Forward source notes

The kernel cutover must preserve `AcceptedBaselineTick` and the discrete wrapper crossing test.
Static impulse promotion tests currently call the old response row on rejected geometry; keep
their frozen pure-equation checks, but new live row tests must honor optional impact rather than
manufacture a certificate. Standalone SpatialGrid retains its existing entity capacity (it is
also a diagnostic value); only its body-legality guard changes. GameSimulation owns motion body
limits, including late-stage survivors. Reuse the stored mode name to remove the existing second
`mode.name()` call while proving once-read eighth declarations.
