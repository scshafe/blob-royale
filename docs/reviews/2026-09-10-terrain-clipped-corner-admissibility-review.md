# Unresolved clipped-corner terrain admission

Status: unresolved numerical/admission issue discovered during plan Step 7. This is not an
accepted rejection fixture, a diagnosed compiler defect, or a production geometry change.

## Exact original input and observation

The original sixth shared terrain golden, `bent_corridor_clipped_envelope`, used this terrain:

```json
{
  "bounds": {"width_world_units": 100, "height_world_units": 100},
  "ground": "corridors",
  "corridors": [
    {
      "name": "edge_road",
      "half_width": 10,
      "points": [{"x": 0, "y": 0}, {"x": 100, "y": 0}, {"x": 100, "y": 100}]
    }
  ],
  "holes": [{"name": "edge_pit", "center": {"x": 100, "y": 50}, "radius": 15}]
}
```

The probes were `(0,0): true`, `(50,10): true`, `(50,11): false`, `(90,30): true`,
`(100,50): false`, `(100,65): true`, `(100,100): true`, and `(110,30): false`.
Construction failed before evaluating any of those probes:

```text
SIMULATION.TERRAIN_GEOMETRY_PRECISION_LOST [terrain_queries]:
a vertex rounded onto a boundary without preserved incidence
```

The advisory Mac/arm64-hosted Docker Linux/amd64 GCC run of `./scripts/verify-focused 'fixtures'`
passed 45/46 selected cases: 44 accepted fixtures and the observation test whose name also matches
the filter passed; this new shared-golden case failed. Within that case, 39 assertions passed,
covering the case-count check and all five preceding terrain definitions/probe groups.
The serial command stopped before Clang. The original log is
`/tmp/blob-royale-plan-rest.Ty2s9f/step7-fixtures-gcc.log`; this document preserves the input and
failure independently of that temporary log. Production terrain sources had no Step 7 changes.

## What is known and what is not

The guard in `src/simulation/terrain_queries.cpp`, `vertex_supported_sector`, found a shape
classified as exactly on a vertex boundary without that shape having stored incidence at the
same binary64 point. This does **not** establish whether the input is legitimately unrepresentable
under the accepted bounded policy or whether the compiler lost an incidence. Neither source
inspection nor the log identifies the exact failing vertex. Calling the input invalid or the
failure fixed would overstate the evidence.

The smallest next diagnostic is to capture that vertex's hexadecimal x/y coordinates, the shape
index/kind, the incident feature IDs, and the relevant shape's feature cuts at the guard. Compare
their root/endpoint provenance before proposing any arithmetic or admission change. Do not remove
the guard, merge with an epsilon, or alter root arithmetic to make the example pass.

## Step 7 fixture scope and gate disposition

The new shared publication/rendering golden now authors width `9.999999999` and radius
`15.000000001`: the existing tolerance produces exact effective radii 10 and 15. This follows the
effective-radius test convention in `tests/unit/simulation/terrain_queries_tests.cpp`. Its topology
and probe list are unchanged, but its authored geometry **is changed**. Passing the replacement
proves only the selected admitted cases; it does not diagnose or cover the original failure.

An independent bounded source review accepted retaining this calibrated Step 7 fixture with this
durable unresolved record and explicit limits on claims. No production geometry guard/arithmetic
or accepted baseline expectation changes. Carry the original issue into the existing Step 5
numerical/live-adoption review; its disposition must be explicit before treating the terrain
foundation as generally certified for live adoption. No native performance/release claim follows.
