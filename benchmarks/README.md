# Linux simulation performance baseline

This directory owns the deterministic, advisory Linux performance baseline. The executable calls
the canonical public `GameSimulation::step()`, `SpatialGrid::rebuilt()`,
`GameSimulation::snapshot()`, `encode_snapshot_message()`, and `SnapshotDeliveryState` APIs. It
does not provide another engine, scheduler, simulation clock, delivery queue, or benchmark
framework.

## What is under measurement, and what is not

`GameSimulation::create` takes three arguments -- the configuration, the initial world, and a
`GameSimulationSetup` that carries every declaration a mode makes. The benchmark passes **no
setup**, so it runs on `GameSimulationSetup::engine_defaults()`: the reserved mode name `idle`, the
two built-in contact rows, no declared system at any stage, a spawn policy that never seats, an
objective that never starts a match, and an arena synthesized as `MapDefinition::bare_arena` from
`SimulationConfig`'s world scalars. Each world is `GameWorld::create(std::vector<EntitySeed>)`, the
scenario-seeded path, rather than the production `GameWorld::create(configuration, map, seed)` that
seats a map's static bodies. `step` is called with `InputBatch::empty()`, the no-input tick. No map
directory, no `GameMode`, no `blob_gameplay` target, and no command participate.

**This is the kernel's floor, not a live match's cost, and the two must not be compared.** A running
`royale` match additionally pays for its four declared systems, its rotating-ring spawn policy, its
per-tick zone entity, and its mode-state block, none of which exist here. What the baseline *does*
now include, because the component registry is closed and its behaviors are generated from it, is
publication of all seven registered component kinds -- royale's `Zone` and `ZoneExposure` among
them, empty -- plus the `MatchSnapshot` section and the derived entity roster on every
`GameSimulation::snapshot()`. Snapshot creation is therefore not comparable across the framework
cutover even at identical player counts.

The protocol case measures **v1** `encode_snapshot_message`. Protocol v2 snapshot encoding, command
decoding, and the `SessionWebSocketSession` path are not measured by this suite.

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
  creation, final tick/player-count checks, and an exact FNV-1a hash of the complete ordered final
  snapshot occur after timing. Every timed sample must match an independent untimed reference.
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

The cases cover 64, 512, and 2,048 spatially sparse players plus a 512-player clustered case. The
output includes player-area fraction and canonical candidate-pair count so density is observable
rather than inferred from a label.

Comparisons from shared or virtualized runners are advisory. A future regression budget must use a
named dedicated native-Linux runner with fixed CPU allocation and power policy, then establish its
threshold from repeated baseline runs. This benchmark intentionally does not infer dedicated-runner
status from CI environment variables.

**No pre-framework baseline is a valid comparison, and none is kept in the tree.** Every run writes
`out/benchmarks/blob-simulation-benchmark.json` and replaces the previous one; the file is not
committed and carries the host it ran on in its `platform` block. Read that block before comparing
two runs at all. The `cole-ubuntu-pc` native runner
(`docs/operations/tailnet.md`) is the only host on which a number here is more than advisory, and
the tree has no baseline from it yet: a re-baseline there is owed before any regression claim about
the gameplay-framework work.

## Root CMake registration

The root `CMakeLists.txt` registers the benchmark after the production domain targets so the runner
can build it through the canonical dependency graph:

```cmake
add_subdirectory(benchmarks)
```

`benchmarks/CMakeLists.txt` declares `blob_simulation_benchmarks` and links the canonical
simulation, protocol, and server targets plus Boost.JSON for the machine-readable report. The
server link exists solely to exercise its production bounded delivery state; the benchmark opens
no listener and owns no transport or runtime clock.
