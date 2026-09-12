# Step 20 verification review

Cliff rims, the void treatment, and combat feedback. Parent checkpoint `d774e6c` (Step 19).
Implementation contract:
[`2026-09-12-cliff-and-combat-rendering-contract.md`](2026-09-12-cliff-and-combat-rendering-contract.md).

This is a web-only step. Its gate is `verify-web` alone, and **that gate does not look at pixels** —
see "What the gate did not establish" below, which is the honest half of this step's completion.

## Completed checks

| Gate | Result |
|---|---|
| `run-linux-toolchain -- verify-web` | 940 / 940 tests; 83 schemas, 33 examples |
| within it: prettier `format:check`, `generate:protocol:check`, `validate:protocol-examples`, `tsc --noEmit`, `eslint --max-warnings 0`, production build | all passed |
| `git diff --check` | clean |

940 from Step 19's 859. No schema, encoder, component, server or wire change: the schema and example
counts are Step 19's, unchanged, which is itself the evidence that this step stayed inside the client.

## What landed

The terrain layer draws the cliff rim bounding the supported set and the void underneath it. Shield
and stun became real renderers on a new `status` layer. Charge stayed deliberately undrawn in world
space and its cooldown became screen-space feedback. `EntityRenderFrame` gained the snapshot tick.

## The preflight rewrote the step

Step 20 arrived with no implementation contract, so a four-agent read-only preflight — one of them
purely adversarial — reviewed a drafted decision list before any source edit. It rejected seven of
eight and found four blockers.

* **No renderer could see the snapshot tick.** Every ability window on the wire is a pair of absolute
  ticks, and `EntityRenderFrame` carried four members, none of them time. A shield renderer could
  therefore only draw *component presence* — the reading the published shield schema explicitly
  forbids, and one that paints a protection ring on a cancelled shield. Widening the frame is this
  step's one client-contract change, now named rather than discovered mid-implementation.
* **The draft rimmed the wrong boundaries.** A cliff is the boundary of *(envelope ∩ the
  positive-ground union) − the union of holes*. The draft named only holes and the outer envelope,
  and **no shipped map or browser fixture authors a hole at all** — so both race maps' entire drop,
  their only real cliff, would have gone unmarked. Meanwhile the envelope is not a cliff: a hole edge
  kills, the map edge folds a blob back, and restyling the border stroke `SimulationCanvas` already
  draws from `configuration.world` would have created the second geometry source the draft promised
  to avoid.
* **Per-body void feedback cannot be built, and depicts a state that never occurs.** Deciding "this
  centre is over void" on the client means a second implementation of `terrain_supports_point`
  including its asymmetric tolerances — forbidden by that function's own canonical note, by ADR 0008
  twice, and by the plan's one-geometry-owner Done criterion. Decisively, a ground-bound body
  terminates at its last supported point and loses its body on the same tick, so **no snapshot ever
  contains a ground-bound centre over void**; the only bodies ever over void are `floating` hazards,
  which never fall. A mark on one would be a confident lie. And the registry structurally cannot host
  a body-derived under-layer: one registration per component kind, and the canvas is barred from
  branching on a kind.
* **Shield draws three states, not two**, and the third is the most common by duration: protection
  over, cooldown still live. 160 protected ticks inside a component that lives at least 360.
  Presence-keyed drawing would show protection to a player who has none.

Also corrected: charge stays non-visual (its own registry test already argued a renderer "would
either repaint motion that is drawn or invent an effect the server never published"), one `status`
layer was added because `visualEntityRenderers()` falls back to sorting by *kind name* so shield and
stun would otherwise have landed under the exposure ring by alphabet, and the shield cooldown
fraction divides by zero on an accepted frame — a zero authored cooldown — while charge's cannot.

## Two techniques worth recording

**Hole rims are free and the naive version is wrong.** `drawTerrain` accumulates one even-odd
complement clip per hole, so while that stack is live the region is exactly *ground minus the union*.
Stroking each hole circle there — after the fill, before the `restore()` — leaves only the ground-side
half, and arcs interior to an overlapping neighbour vanish with the clip. That is the union outline,
correct by construction. Stroking before the clips or after the restore paints rims straight through
the interior of the void.

**Corridor rims are an underprint.** Canvas exposes no outline operation for a wide stroke and the
compiled corridor boundary is `simulation::detail`, deliberately not wire data. So every polyline is
stroked in the rim colour at `2 * half_width + 2 * rim` and then in the road colour at
`2 * half_width` over it, with all rim passes preceding all surface passes. The union of the wide
strokes minus the union of the narrow ones is exactly the road-union outline; overlapping corridors
need no special case.

## Execution corrections

Three test expectations were mechanical consequences of the new terrain calls, each updated with its
rationale rather than loosened:

* `hillRenderer.test.ts` records the save-depth at every `arc()`. It expected `[1, 0]`; the rim adds
  a second arc inside the clip scope, so it is now `[1, 1, 0]`. What the case exists to prove is the
  trailing zero — the hill circle is drawn outside any terrain clip — and that is unchanged.
* `SimulationCanvas.test.tsx` counted one `fillRect` per terrain draw; the void underlay adds one.
* The same file counted one corridor trace; the underprint makes it two, at both assertion points.

## Limits, stated rather than discovered

* **A perfect mark is a state mark, never an event treatment.** The opening is 32 ticks at 400 Hz —
  80 ms — against a 50 ms snapshot interval at the shipped cadence, so at least one snapshot always
  lands inside it and the mark is truthful whenever drawn. That is a property of the *configured*
  cadence, not of the protocol: at a lower `snapshots_per_second` the opening can be missed entirely,
  and no client-side interpolation may invent it.
* **The readout states remaining time and never availability.** Charge readiness depends on the
  safety envelope, which is not on the wire, so the readout says *cooling* and *cooldown over* and
  never *ready*. Only the shield protection length that actually happened is published, never the
  authored one, so a full-length bar cannot be shown before a first activation and would be wrong
  after a cancellation.
* **No ability mark is reachable in the live browser yet.** A shield needs a sender and Step 21 owns
  senders; a stun needs a perfect parry, which needs a shield; and no bot presses either until Step
  22. So the shield and stun renderers are evidenced by their unit tests and by the canvas
  tick-threading test, not by a screenshot. That is a consequence of the plan's own ordering, not a
  gap in this step.

## What the gate did not establish

`verify-web` asserts call sequences and geometry arguments. It does not look at a pixel, and since no
shipped map authors a hole, the hole-rim path is exercised only by synthetic terrain fixtures. A
green gate establishes that the right calls happen with the right numbers under translation, manual
and follow camera, edge view, fractional device pixel ratio, and resize. **It does not establish that
a cliff reads as a cliff.**

For that, the real client was run against a real server through the project's own pinned e2e harness
and the canvas captured on both ground kinds: the race map, whose corridor edge is the only real
cliff on any runnable map, and a solid map, which has no cliff to rim and must show no void. The
images were sent to the owner for judgement. The temporary capture spec was deleted; the tree
contains no trace of it, and the images live under the ignored `out/` directory.

The colour choices — a `#94a3b8` void under a `#0f172a` rim, and a 2 world-unit rim width — are
initial values chosen for legibility against the existing palette, not owner-selected art direction.

## Exact acceptance commands

```
./scripts/run-linux-toolchain -- ./scripts/verify-web
git diff --check
```

Transient logs are `/tmp/s20-web.log` and are not committed.
