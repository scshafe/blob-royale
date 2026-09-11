### Assumptions

- This is the independent source/design review for Step 7a in
  `.claude/plans/2026-09-10-dynamic-arenas-and-combat.md`. Final execution verification remains
  pending and parent-owned. The reviewer ran no builds or tests.
- ADR-0008 (`docs/architecture/0008-dynamic-arenas-and-combat.md`), sections "Terrain foundation"
  and "Terrain construction provenance repair", governs the contract. No dedicated project
  principles document was found. The review applies separation of policy from mechanism:
  classifying topology and materializing a recovery witness are different decisions.
- The captured causes are distinct. Smooth joins lost tangent provenance; the clipped envelope
  lost an existing endpoint identity. Exact inputs and traces remain in
  `docs/reviews/2026-09-10-racer-projection-admission-blocker-review.md` and
  `docs/reviews/2026-09-10-terrain-clipped-corner-admissibility-review.md`.

### Risks

- No unresolved source-level blocker remains in the reviewed repair. The initial broad endpoint
  shortcut was narrowed, conflicting endpoint evidence now fails, and the final axis certificate
  checks retained authored orientation as well as coordinates (`src/simulation/terrain_queries.cpp`,
  `line_axis`, `axis_line_contains`, and `intersect_features`).
- This remains bounded binary64 geometry. Generic `line_contains` takes determinants after
  rounded coordinate subtraction; it is not an exact orientation predicate over original
  coordinate triples. The new construction shortcuts do not use it as their certificate
  (`src/simulation/terrain_queries.cpp`; `src/simulation/swept_geometry.hpp`).
- Recovery remains incomplete by contract: the selected supported sector can lack a representable
  direction or exhaust its four targets. Neither failure permits another sector or a farther
  feature. Absence of a strict first-order cone also permits curved cusps and must not erase
  their boundary (`src/simulation/terrain_queries.cpp`, `vertex_supported_sector` and
  `vertex_supported_witness`; ADR-0008, "Terrain foundation").
- Uncommitted Step 8 projection changes coexist in `src/simulation/terrain_queries.cpp`.
  This review does not approve their reader cutover or inclusion in the geometry repair commit
  (`.claude/plans/2026-09-10-dynamic-arenas-and-combat.md`, Steps 7a and 8).

### Alternatives

- Retain explicit construction relationships and limit shortcuts to provable axis cases. The
  implemented design preserves general intersection arithmetic while repairing the captured
  failures (`src/simulation/terrain_queries.cpp`, `intersect_features`).
- General exact orientation or broader intersection certificates would require a separate
  arithmetic contract. They are unnecessary for the captured cases. Reusing arbitrary cuts by
  matching rounded rays, merging nearby coordinates, or suppressing incidence guards would not
  establish construction identity (ADR-0008, "Terrain construction provenance repair").

### Recommended path

Accept the current repair at the source/design layer, subject to the existing Step 7a execution
checks. Capsule sides retain correctly signed authored inward normals, and arc vertex incidences
use their stored `Cut.direction`. Every incident tangent and its opposite enters angular ordering;
deduplicating breakpoints does not remove shape constraints. Thus an adjacent open sector cannot
cross an incident halfspace boundary. `sector_inside_halfspace` uses the begin-ray determinant;
at collinearity, the immediately counterclockwise sector is outside at the tangent and inside at
its opposite. Only the first supported sector then constructs and validates an interior direction
(`src/simulation/terrain_queries.cpp`, `feature_normal`, `sector_inside_halfspace`,
`vertex_supported_sector`, and `compile_terrain_boundary`).

The endpoint certificate examines all four line endpoints, uses literal axis coordinates/ranges,
and rejects conflicting identities. Axis recognition also requires the authored normal to agree.
For an axis line through a circle center, the two cardinal directions exhaust the possible
intersections. The compiler reproduces each included cardinal with the identical `circle_point`
construction used by `append_arc`, requires the exact stored point and literal direction, then
validates both candidates before adding incidences. It never chooses a point from an arbitrary
cut's reconstructed ray. Existing line-parameter, arc-direction, missing-incidence, witness,
arrangement-storage, and retained-boundary guards remain. Tangent branches, rim-only spans, and
isolated points retain their separate representations. No temporary traces or changes to the
canonical swept-root/event-order owners remain (`src/simulation/terrain_queries.cpp`,
`intersect_features`, `append_arc`, and `compile_terrain_boundary`;
`src/simulation/terrain_boundary.hpp`; `src/simulation/swept_geometry.{hpp,cpp}`;
`src/simulation/motion_event_order.hpp`).

### What to verify before committing

- The parent records final two-lane and fixed-corpus results required by Step 7a in
  `.claude/plans/2026-09-10-dynamic-arenas-and-combat.md`. Earlier red/partial diagnostics are
  evidence of diagnosis, not final verification of this patch.
- The unchanged original inputs and support/recovery assertions remain in
  `tests/unit/simulation/fixtures/terrain_provenance_fixture.hpp` and
  `tests/unit/simulation/terrain_queries_tests.cpp`. Source review confirms additional assertions
  preserve unrelated crossing constraints, different-curvature tangent branches, a supported
  cusp, singleton contact, rim-only arcs, and isolated corners.
- Stage only the prerequisite repair. Step 8 still requires its own complete pre-delegation
  proof, and Step 5 remains the human gate for Phase C
  (`.claude/plans/2026-09-10-dynamic-arenas-and-combat.md`, Steps 5, 7a, and 8).

Confidence: medium — the final source is reviewed with no remaining blocker; execution verification is pending.

### Parent verification addendum, 2026-09-11

After this independent source review, the parent ran the exact Step 7a filter on both prescribed
advisory Mac/arm64-hosted Docker Linux/amd64 lanes: **1,031/1,031 GCC** and **1,031/1,031 Clang
ASan/UBSan**. Fixed corpus replay passed **55 executions across 7 harnesses**. Pinned formatter
dry-run and `git diff --check` passed. The plan and admission reports record complete commands,
logs, and the preserved uncommitted Step 8 candidates present during verification. This addendum
is parent-reported execution evidence, not a claim that the independent reviewer ran tests, and
does not certify native performance, arbitrary geometry, Step 5, or Phase C.
