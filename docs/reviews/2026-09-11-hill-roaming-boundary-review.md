# Hill roaming boundary preflight, 2026-09-11

Status: source-reviewed design gap; no Step 12 implementation or geometry proof is claimed.
The owner questions below are pending. Step 5 and Phase C remain separate.

## Finding

Step 12 assumes shared disc clearance is sufficient for continuous hill roaming. The current
`src/simulation/terrain_queries.hpp` exposes swept **point** support and static **disc** clearance,
not swept disc containment or the contact features needed for reflection. Checking the endpoint
or sweeping only the hill's center is insufficient even at the proposed ordinary speeds.

An analytic counterexample, not yet an executed regression: in solid 1000-by-1000 terrain with
a radius-10 hole centered at (500, 500), a radius-20 hill travels from
(499.925, 529.99999) to (500.075, 529.99999). At 400 Hz this is approximately 60 wu/s.
Both endpoint discs fit, but the midpoint clearance is approximately 19.999990001, less than 20;
the hill's center remains supported throughout. Neither existing query detects the intervening
disc overlap by itself.

The static clearance witness is also not a contact manifold. It chooses one exposed-boundary
witness by computed distance, coordinates, and feature identity and may correct that witness
toward the supported query. Its documented binary64 policy is not an interval-certified bound.
Normals reconstructed from a corrected point would lose analytic provenance and simultaneous
contacts. See `terrain_queries.hpp`'s clearance contract and `terrain_queries.cpp`'s canonical
witness selection, not a gameplay-local reconstruction of roads or holes.

## Recommended prerequisite

Preserve the plan's Boolean terrain scope. Extend the one canonical terrain query owner with
first swept disc-clearance loss plus stable contact-feature provenance. Use the existing exposed
line/arc cache in `src/simulation/terrain_boundary.hpp` so internal road-union seams do not become
walls, and derive candidate times solely through `src/simulation/swept_geometry.hpp` and the
existing event order. Keep the cache private to simulation.

Before any hill consumer, specify and prove initial-boundary direction, tangency, simultaneous
features, representability, bounded work, and positive progress. Add the counterexample above,
union seams, corners, tangent starts, and zero-time recurrence to the proof inventory. Retain
the static query's honest numerical limitations; do not claim total exact-real reflection from
a bounded binary64 witness search.

This is pure geometry, not live-kernel adoption. `HillMovementSystem` remains the existing
post-kernel kinematic owner before scoring in `king_of_the_hill_mode.cpp`; no `PhysicsBody`, new
mode declaration, gameplay root solver, or Step 16 seam is needed. Existing marker-tour arithmetic
and zero hill-stream draws remain unchanged until the separately verified consumer cutover.

## Gameplay decisions pending

1. At a curved containment boundary, an exactly tangent velocity may immediately leave the legal
   region while specular reflection returns that same tangent. A tie-break cannot solve this
   zero-time recurrence. Recommend a deterministic inward redirection that preserves speed and
   position continuity when ordinary reflection cannot produce legal positive progress. This
   is a gameplay policy, not a concealed numerical correction. The alternative is an explicitly
   restricted map-admission contract; full Boolean terrain must not silently become rectangles.
2. Define reachable ground. Recommend player access from a spawn, rather than merely a local
   region where the hill disc fits. A single fitting point is not even a movement region: a
   radius-20 disc fits at the center of a 40-by-40 rectangle but cannot roam there. Current hill
   validation checks marker presence, not this reachability/admission property.

The existing fail-visible policy remains the default for precision/work-limit failures: no
silent dwell, teleport, endpoint repair, or random-heading retry loop. Owner approval of a
redirection rule would not establish numerical totality or a proven admission algorithm.
After decisions, amend the plan with an atomic pure-query/admission prerequisite and its exact
two-lane proof before releasing the roaming consumer. Concrete draw order and speed/retarget
defaults belong in that subsequent design, not in speculative wire vocabulary now.

## Review evidence and limits

Independent numerical source review inspected the current hill pipeline, terrain query contracts,
exposed-boundary cache, canonical swept roots, and reflection equation. The existing wall-motion
solver could support an explicitly narrower solid, hole-free rectangle first version, but that
would change the accepted terrain scope and is not adopted here. No source, map, fixture, wire,
or accepted-baseline change was made for this preflight, and no build/test/native-performance
result is claimed by this document.
