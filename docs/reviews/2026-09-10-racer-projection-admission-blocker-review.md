# Step 8 blocker: sloped terrain admission before projection proof

Status: repaired and verified under Step 7a on 2026-09-11. Step 8's production callers remain
unswitched pending its own exact pre-delegation gate. Historical diagnostic sections follow.
The latest completed implementation checkpoint is `f837061` (Step 7).

## Observed failure

The exact pre-delegation command `./scripts/verify-focused 'unit.simulation|unit.controllers'`
passed **599/601** on advisory Mac/arm64-hosted Docker Linux/amd64 GCC. The chained Clang command
did not run. Both failures were new proof cases, not accepted simulation/controller regressions.

One failure was a test-authoring mistake: the signed endpoint witness used the terrain/world
ceiling `1e9`, but `Vector2` permits `1e12`. The proof now uses `±1e12` and the literal one-ULP
overshoot/distance `0x1p-13`. Its raw new-core, frozen reference, old no-throw distance, and typed
point rejection assertions pass in the subsequent focused diagnostic. No production bound changed.

The remaining case, `racer projection promotion preserves actual old racer command bits before
delegation`, fails while its truthful fixture compiles terrain, before comparing any commands:

```text
SIMULATION.TERRAIN_GEOMETRY_PRECISION_LOST [terrain_queries]:
a vertex sector has no representable strict interior direction
```

Adding named dynamic sections, without changing the authored geometry or probes, identifies
`sloped`, `fractional`, and `fractional_gate`. The first 16 scenarios pass; the diagnostic records
1,120 successful assertions and three construction exceptions, not command-bit mismatches.

The diagnostic command was:

```sh
./scripts/verify-focused 'unit.simulation.terrain distance promotion|unit.controllers.racer projection promotion preserves actual'
```

It passed **1/2** cases (the corrected signed-extreme proof) and failed the racer case. This is
diagnostic evidence, not a successful rerun of the required gate. Logs are
`/tmp/blob-royale-plan-rest.Ty2s9f/step8-preproof-gcc.log` and
`/tmp/blob-royale-plan-rest.Ty2s9f/step8-proof-diagnostic.log`; exact inputs are retained below and
in `tests/unit/controllers/racer_projection_promotion_tests.cpp`, independent of temporary logs.

## Exact inputs

Both terrain definitions have bounds `960 × 640`, ground `corridors`, one corridor named `road`,
authored half-width **80**, and no holes. The canonical effective corridor radius is therefore
`80 + kPositionTolerance`, unchanged from production. Their points are:

```json
{
  "sloped": [{"x": 100, "y": 100}, {"x": 500, "y": 400}],
  "fractional": [{"x": 100.125, "y": 100.375}, {"x": 700.625, "y": 500.875}]
}
```

The sloped course has gates `(260,220)` and `(500,400)`, body `(280,320)`, next checkpoint 1,
and caution 0.5. The fractional course has gates `(250.25,200.5)` and `(550.5,400.75)`;
`fractional` places the body at `(340.1875,370.3125)` with next checkpoint 1/caution 0.5,
while `fractional_gate` uses `(250.25,200.5)` with next checkpoint 1/caution 1.0.
Terrain compilation fails independently of those controller settings.

## Initial narrow diagnosis and limits (before the captured trace)

Source inspection identifies a plausible tangent-provenance loss, not yet a captured binary-ray
trace. `append_arc` retains authored endpoint directions specifically because rounded points need
not reconstruct them. `vertex_supported_sector` instead asks `feature_normal`, which reconstructs
side normals from rounded offset endpoints and cap normals from `center - roundedPoint`.
Exact direction deduplication can retain two nearly parallel rays at an authored smooth side/cap
join. Their normalized sum can round outside the strict sector, triggering the observed guard.
The inter-feature tangent rule preserves intersection identities but does not supply these sector
normals; intersections belonging to the same shape are skipped.

The failing inputs and guard are confirmed. The exact offending vertex/ray pair and whether its
sector is genuine or manufactured remain unverified. The next narrow diagnostic is to capture
the vertex, feature IDs, authored directions, and reconstructed rays in hexadecimal before any
change to geometry arithmetic. An independent source review reached the same scoped hypothesis.

## Execution disposition

Step 8 requires the actual old racer's command-bit proof over sloped/fractional terrain before
switching readers. Replacing or calibrating these inputs would avoid, not satisfy, that proof.
Do not weaken the guard, merge by epsilon, or change authored widths/coordinates to pass it.
No old reader/type, accepted fixture, clock, kernel seam, or geometry compiler rule has changed.

A dedicated terrain-compilation prerequisite needed approval and a plan amendment before repair.
It must independently verify these exact inputs, existing geometry/support/recovery regressions,
and the separate unresolved clipped-corner input recorded in
`2026-09-10-terrain-clipped-corner-admissibility-review.md`, without claiming the two failures have
the same cause. Preserve the uncommitted Step 8 work; no Step 8 commit or completion is claimed.

## Captured construction provenance loss (owner-approved Step 7a)

After the owner approved the researched strategy, temporary bounded failure-only diagnostics
captured hexadecimal coordinates, feature/shape IDs, stored directions, reconstructed rays, and
exact determinant signs without changing geometry decisions. The advisory Mac/arm64-hosted Docker
Linux/amd64 GCC command below reproduced all three selected test failures (**0/3**):

```sh
./scripts/verify-focused 'unit.simulation.terrain provenance|unit.controllers.racer projection promotion preserves actual'
```

The two new geometry cases retain the exact sloped/fractional maps and original clipped-corner
map. The third is the existing old-racer proof, still with 1,120 successful assertions and three
construction exceptions. Log: `/tmp/blob-royale-terrain-provenance.veIQIP/red-trace-gcc.log`.

At the fractional road's side/cap join, the vertex is
`(0x1.bde3bb10da2a9p+5, 0x1.4ddc6a0f2d1a6p+7)`. Feature 5's authored axis is
`(0x1.2c4p+9, 0x1.908p+8)`, but subtracting rounded side endpoints produces tangent
`(0x1.2c4p+9, 0x1.9080000000001p+8)`. Feature 7's stored endpoint direction is exactly
`(-0x1.908p+8, 0x1.2c4p+9)`, while reconstructing it from the rounded vertex yields another
tangent, `(0x1.0a38d41e5a34cp+6, 0x1.631c44ef25d57p+5)`. These create four angular rays where
the authored smooth join requires one antipodal pair. The first adjacent pair has positive
determinant, but its normalized-sum witness lies outside: begin/interior sign is **-1**.
The integer-coordinate sloped road fails at the analogous side/cap join. This confirms the
construction-provenance diagnosis rather than a ban on fractional authored coordinates.

The clipped-corner failure is separate: side 5/cap 7 share vertex
`(0, 0x1.4000000089706p+3)`, while envelope feature 4 stores the recomputed intersection
`(0, 0x1.4000000089708p+3)`, two binary64 steps away. Here the missing envelope incidence is
real; it is not merely a rounded distance-equality classification. A certified endpoint identity
must survive intersection construction. Proximity merging is not authorized by this evidence.

The repair must preserve authored line orientation and arc endpoint directions, classify open
angular sectors before constructing a selected recovery direction, and preserve already-proven
intersection endpoint identities. Distinct feature incidences and tangent curved branches remain
distinct; lack of a strict supported angular sector must not erase supported rims or isolated
points. Existing bounded selected-witness failure remains visible. Independent review and green
verification are still required before either Step 7a or Step 8 can be marked complete.

### Initial repair diagnostic

The same focused command, after retaining authored normals/cut directions, classifying sectors
before witness construction, and preserving a known line/line endpoint, passed **4/5** on advisory
GCC. The original sloped/fractional construction/query case and actual old-racer command-bit case
passed, as did new unrelated-crossing and different-curvature/tangent-cusp controls. No old reader
switched. Log: `/tmp/blob-royale-terrain-provenance.veIQIP/initial-repair-gcc.log`.

The unchanged clipped-corner case progressed past the missing-incidence guard but reached the
existing `distinct arrangement vertices collapsed to one line parameter` guard. Investigation
continues at this distinct collapse; it is not a passed prerequisite or justification to merge
nearby coordinates. The complete two-lane and baseline gates have not yet run.

The diameter-provenance repair then passed the same **5/5** focused cases on advisory GCC
(`diameter-repair-gcc.log` in the same directory), including the unchanged original clipped input.
Final review additionally requires axis certificates to agree with retained authored orientation,
not only rounded endpoint coordinates. Full prerequisite verification remains pending; this
focused success does not authorize the old-reader cutover by itself.

## Final prerequisite verification (2026-09-11)

After the final authored-axis hardening, the exact Step 7a filter
`unit.simulation|unit.application|unit.gameplay|unit.controllers|fixtures` passed **1,031/1,031**
on each of GCC and Clang ASan/UBSan, on advisory Mac/arm64-hosted Docker Linux/amd64. Fixed fuzz
regression replay passed **55 executions across 7 harnesses**; this was not exploratory fuzzing.
Pinned formatter dry-run and `git diff --check` passed. Logs are `full-gcc.log`, `full-clang.log`,
`fuzz-regressions.log`, and `format-check.log` under the directory above.

The independent source review closed without a blocking finding in
`2026-09-10-terrain-provenance-repair-review.md`. Original authored fixtures, generic swept-root
arithmetic, accepted replay/fixture values, work bounds, and selected-witness guards remain.
Temporary diagnostics were removed. This verifies the diagnosed repair, not arbitrary exact-real
geometry, native performance, or Step 5 acceptance. Step 8 candidates stayed in the verification
worktree but remain outside the separate geometry commit and still require their own gate.
