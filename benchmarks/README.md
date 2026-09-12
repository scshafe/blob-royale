# Linux simulation performance baseline

This directory owns the deterministic, advisory Linux performance baseline. The executable calls
the canonical public `GameSimulation::step()`, `SpatialGrid::rebuilt()`,
`GameSimulation::snapshot()`, `encode_snapshot_message()`, and `SnapshotDeliveryState` APIs. It
also measures the canonical continuous-motion solver directly in the retained prototype cases. It does not
provide another engine, scheduler, simulation clock, delivery queue, or benchmark framework.

## What is under measurement, and what is not

The `sparse_64` live-kernel case uses `GameSimulation::create`, whose three arguments are the
configuration, the initial world, and a
`GameSimulationSetup` that carries every declaration a mode makes. The benchmark passes **no
setup**, so it runs on `GameSimulationSetup::engine_defaults()`: the reserved mode name `idle`, the
built-in contact rows, no declared system at any stage, a spawn policy that never seats, an
objective that never starts a match, and an arena synthesized as `MapDefinition::bare_arena` from
`SimulationConfig`'s world scalars. Each world is `GameWorld::create(std::vector<EntitySeed>)`, the
scenario-seeded path, rather than the production `GameWorld::create(configuration, map, seed)` that
seats a map's static bodies. `step` is called with `InputBatch::empty()`, the no-input tick. No map
directory, `GameMode`, gameplay system, or command participates in this case. The separate
royale case does use the production gameplay and input-loading targets.

This measures empty-input kernel cost, not the full cost of a live match. A running royale match
also pays for declared systems, spawning, objectives and mode state. Registry-generated snapshot
publication includes every currently registered component kind, including empty stores, the match
and derived roster. Live results before and after Step 16's continuous-kernel adoption have
different implementation boundaries; identical population alone does not make timings comparable.

The owner-approved Step 16 suite explicitly categorizes its eight cases: one `live_kernel`, one
`live_gameplay`, three `spatial_grid_only`, and three `pure_motion_solver`. The former full-engine
`sparse_512`, `sparse_2048`, and `clustered_512` populations exceed the adopted motion envelope.
They are now `spatial_grid_sparse_512`, `spatial_grid_sparse_2048`, and
`spatial_grid_clustered_512`. Their original geometry, body counts, layout arithmetic and grid
rebuild counts remain unchanged. Each names its former identity and retired measurement counts
under `historical_reference`. Only `SpatialGrid::create/rebuilt` and canonical candidate counts/
hashes are measured: no live stepping, post-step snapshot creation or JSON encoding is performed
or replaced with a fabricated final snapshot. Categories are declared explicitly, never chosen
as a fallback based on the current body cap. Standalone grid capacity is not motion capacity.

Compare these diagnostics only to the old cases' retained grid operation/count/hash fields.
Whole-case timing, post-step snapshots and encoding results are retired and not comparable.
Historical Step 4/4a JSON artifacts retain their original suite lists and are never rewritten to
describe this new composition. The complete pre-cutover prerequisite suite was preserved in the
Step 16 evidence before changing any reader.

The protocol case measures **v1** `encode_snapshot_message`. Protocol v2 snapshot encoding, command
decoding, and the `SessionWebSocketSession` path are not measured by this suite.

## The royale case

`royale_deployed_roster` retains its historical case identity and runs real royale gameplay, but
does **not** measure the currently selected deployment mode. ADR 0006
(`docs/architecture/0006-lobbies-as-rooms.md` § "The tick-loop decision") states its per-room
budget against this historical workload: **a mean step of at most 250 µs and a p99 of at most 1 ms**
at eight seats.

The runner passes `--royale-reference-config benchmarks/fixtures/royale-roster.cfg` and
`--maps-directory maps`. The fixture is derived from
`a9e0104ca25724ac4660a4fc9031620b8a852d40:deploy/ubuntu-pc/blob-royale.cfg`. The 2026-09-10
Step 6 strict-schema migration replaces the unused `[race] track_half_width_world_units` key with
`road=road`; the 2026-09-11 Step 10 migration moves acceleration 400 to `[movement]` and authors
the fixture ceiling 10000. Fresh benchmark worlds seed that pair as current/default movement.
Step 16 adds explicit `contact_effect_policy=closing_impact` to its existing hazard sections.
Step 17 adds required `[sandbox] respawn_delay_seconds=2.0`; royale does not consume that section.
Step 18 adds required `[abilities]` at 0.4/0.08/0.9/0.6 s, which royale *does* consume: the shared
ability system runs in every mode. The authored values are exactly
`gameplay::AbilityConfiguration::defaults()`, so this migration authors what the fixture would
otherwise inherit and changes no measured input; the ability system itself is a Step 18 workload
change, not a fixture change.
It is no longer a byte-for-byte historical configuration. Historical workload values remain,
and the provenance comment and JSON identify these migrations. This migration is not a new
timing baseline or a claim of native performance certification.
That commit identifies the **configuration only**; `MapLoader` still loads the named
`arena-960x640` from the current repository's `maps/`, not a historical map checkout. The
production `ApplicationConfigLoader` reads the reference `[simulation]`, `[movement]`,
`[abilities]`, `[royale]`, and `[hazard.*]` sections. Only the lobby widens from the reference four seats to the
budgeted eight. JSON records both counts and explicit provenance under `historical_reference`,
including `represents_current_deployment=false` and the current map source. No live configuration
is edited, no mode is silently overridden, and a missing/invalid reference remains a hard failure.

Each sample builds a fresh simulation, spawns and joins eight controllers and presses Start on tick
1, carries the match through the reference countdown with empty ticks, and then times **each
`step` individually** for 6,400 running ticks -- sixteen seconds, which crosses every spawn tick of
both reference hazard kinds. Every tick carries the reservation the runtime would give it, so the
zone entity and the hazards are created exactly as in production; the reference comet is lethal,
so the field thins as comets cross it, and the output carries the player count at both ends of the
window. The per-sample mean, median,
p99, and maximum are reported as raw samples and as robust summaries across the nine samples; the
`budget` block compares the median-across-samples mean and p99 against the ADR's numbers and
**reports** the answer without enforcing it. Snapshot creation of the running world is timed
separately. The final snapshot of every timed sample must hash-match an untimed reference, which
is what makes a seeded hazard table a benchmark rather than a random one.

Native comparison requires the named `cole-ubuntu-pc` runner and comparable recorded workload
inputs. Read the `platform` and `historical_reference` blocks before comparing results, and record
new native evidence in ADR 0006 as a dated amendment. Mac-hosted emulated runs remain advisory.

## Retained direct continuous-motion prototype cases

These three Step 4 cases call the same `solve_continuous_motion`, `compose_guarded_pair`, and
`support_loss_motion_trigger` implementations. Step 16 adopts the solver; production support
registration and guarded composition remain Steps 17 and 18 respectively. They do not measure live
intake, lifecycle or publication; they call the solver directly. All use a 960-by-640 envelope, radius-four bodies,
the fixed 400 Hz quantum, one warm-up, nine samples, and sixteen independent solves per sample.

- `continuous_motion_charge_speed`: eight independent rows, each with one dynamic body at
  x=128 moving at 40,000 wu/s and one static disc at x=192. Each body reflects once and completes
  its actual path. The 100-wu proposed displacement is stress input, not approved charge tuning.
- `continuous_motion_dense_32_holes`: the authored maximum of 32 radius-eight holes on an 8-by-4
  grid, with one body starting 40 wu left of each hole at 40,000 wu/s. Collision masks are zero;
  all 32 bodies must terminate on their first support loss rather than cross a hole and reland.
- `continuous_motion_dense_shield_contacts`: eight touching four-body chains with x velocities
  `(8000, 4000, -4000, -8000)` and frozen guards `(ordinary, perfect, perfect, ordinary)`. Each
  finite zero-time cascade produces five contacts and five stun facts. Terminal velocities are
  `(-250, -250, 0, 0)`: stun kills current motion, but later external bumps remain physical.

Each solve borrows identical already-accelerated bodies, committed world, and frozen guard facts.
The Step 4a observation callback keeps the empty per-object policy list, so all three workloads
retain closing-impact-only admission and their existing outcomes; mixed any-touch policies are
covered by unit tests rather than changing these baseline workloads.
Terrain compilation and world/grid setup are outside timing; acceleration/drag intake, ability
activation/windows, registered stun lifecycle, live-kernel wiring, publication, and transport are
not measured. Result allocation and solver/callback work are inside timing. The typed consequence
facts are not new registered events.

Independent reference runs and the retained final output of every timed sample must match a
historical deterministic hash: pre-Step-17 body fields and disposition, actual path segments, ordered
event keys, typed consequences, trigger cursors, and work counters. Step 17's ground attachment is
checked separately against each input body in the independent references and each retained timed
final output, published as `ground_attachment_preserved`; these unchanged workloads use floating
bodies, not both attachment modes. Hashing and correctness checks occur
after the clock stops. JSON reports exact layout/density inputs, terrain feature counts, charged
root queries, events, pair examinations, candidate-pair high-water count, rebuilds, paths, and
effects. Root counts are canonical public-query budget charges, including charges before bounded
fast returns, not a claim that every charged primitive performed a root solve.

These results are explicitly advisory and certify no native capacity, supported charge speed, or
production rollout. Step 5's human gate is accepted; final native release evidence remains separate.
The dated Step 4 review and selected prototype baseline are retained under
`docs/reviews/2026-09-10-continuous-motion-prototype-review.md` and the sibling
`2026-09-10-continuous-motion-prototype-baseline.json`. They are evidence for that gate, not
accepted regression thresholds; the complete transient suite output remains under `out/benchmarks/`.

## Measurement contract

- Run `./scripts/run-benchmarks-linux`. Host invocations re-enter through the pinned Linux/amd64
  toolchain. The inner runner rejects any OS or architecture other than Linux x86_64 and performs
  a fresh configure plus clean release build. Configure/build diagnostics remain under
  `out/benchmarks/` and are replayed to standard error on failure.
- Build and measurement failures return nonzero and emit a JSON error object. A successful run
  emits one JSON document on standard output using schema `blob-royale-benchmark-v1` and stores the
  same document at `out/benchmarks/blob-simulation-benchmark.json`.
- Each operation uses one untimed warm-up and nine timed samples. Output retains every raw sample
  and reports the median, 25th percentile, 75th percentile, median absolute deviation, minimum,
  and maximum. No regression threshold is enforced.
- Each simulation sample constructs a fresh identical `GameSimulation` before timing. Snapshot
  creation, final tick/player-count checks, and an exact FNV-1a hash of the ordered final snapshot's
  player-motion projection occur after timing. This existing hash is unchanged; the separate
prototype hash covers its historical typed-result fields, with ground attachment independently
checked as described above. Every timed sample must match an independent
  untimed reference.
- Candidate-pair generation is not exposed separately by `SpatialGrid`. The
  `grid_rebuild_and_candidate_pair_generation` measurement therefore times the public atomic
  rebuild operation and reports the untimed canonical candidate count/hash. It must not be
  interpreted as an isolated pair-generation cost.
- Snapshot and protocol-encoding samples retain their final value during timing, then verify its
  hash after the clock stops. The fixed protocol timestamp is benchmark input, not a second clock.
- Process peak RSS uses Linux `getrusage(RUSAGE_SELF).ru_maxrss` and is reported in bytes. Hardware,
  kernel, libc, compiler, Boost, build type, pinned toolchain identity, CPU governor, logical CPU
  count, and physical memory are recorded with each run.
- The delivery case drives the production `SnapshotDeliveryState` for 4,096 presentation slots
  while one simulated write completes every 16 slots. It runs the complete policy once as an
  untimed warm-up, then reports nanoseconds per presentation slot and presentation slots per
  second for nine samples. Final draining happens after each timed region.
- Delivery inputs use lifetime-tracked snapshot objects, and each input reference is moved into the
  production state when its slot arrives. The reported retained-snapshot bound is the maximum
  number of distinct live snapshot objects after excluding only not-yet-presented inputs; it is not
  inferred from delivery-state booleans or `shared_ptr` reference counts. Every sample must drain
  to zero live snapshots and match the warm-up's delivered/coalesced counts, maximum/final tick
  lag, and deterministic delivery-trace hash. Snapshot construction is outside the timed region.

The sparse/clustered categories retain 64, 512, and 2,048 spatially sparse bodies plus a 512-body clustered case. Only the 64-body member runs the live simulation. The
output includes player-area fraction and canonical candidate-pair count so density is observable
rather than inferred from a label.

Comparisons from shared or virtualized runners are advisory. A future regression budget must use a
named dedicated native-Linux runner with fixed CPU allocation and power policy, then establish its
threshold from repeated baseline runs. This benchmark intentionally does not infer dedicated-runner
status from CI environment variables.

**No pre-framework baseline is a valid comparison, and none is kept in the tree.** Every run writes
`out/benchmarks/blob-simulation-benchmark.json` and replaces the previous one; the file is not
committed and carries the host it ran on in its `platform` block. Read that block before comparing
two runs at all. The named `cole-ubuntu-pc` native runner (`docs/operations/tailnet.md`) has dated
royale evidence recorded in ADR 0006. That evidence remains historical; a new native run with
comparable workload inputs and recorded implementation boundaries is required before making a new
performance or regression claim. It does not certify continuous-motion capacity.

## Root CMake registration

The root `CMakeLists.txt` registers the benchmark after the production domain targets so the runner
can build it through the canonical dependency graph:

```cmake
add_subdirectory(benchmarks)
```

`benchmarks/CMakeLists.txt` declares `blob_simulation_benchmarks` and links the canonical
simulation, gameplay, application-input, protocol, and server targets plus Boost.JSON for the
machine-readable report. The server link exists solely to exercise its production bounded delivery
state, and the application-input link solely to read the historical reference configuration and map
for the royale case; the benchmark opens no listener and owns no transport or runtime clock.
