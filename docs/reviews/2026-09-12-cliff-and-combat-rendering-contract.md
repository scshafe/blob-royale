# Step 20: cliff and combat rendering

Implementation contract, written after a read-only preflight against `d774e6c` (Step 19) and
adversarially reviewed before any source edit. This is a **web-only** step: no schema, encoder,
component, server or wire change, and its Verify line is `verify-web` alone.

The preflight rejected seven of eight drafted decisions and found four blockers. Each correction is
marked **[corrected]**, because the wrong version is the one a reader would otherwise reach for.

## The one client-contract change, named

**[corrected] `EntityRenderFrame` gains `tickSequence: number | null`.** The draft claimed Step 20
changes no contract. That is true of the wire and false of the client: every ability window on the
wire is an absolute tick, no renderer can currently see the snapshot tick, and without it a shield
renderer can only draw *component presence* — exactly the reading the published schema forbids, and
one that would paint a protection ring on a cancelled shield.

The frame already carries `eliminationGraceTicks` and `ownEntityId` under a stated rule — "a renderer
receives what it may read and never goes looking" — so this is that precedent, not a new liberty.
`tickSequence` is already computed at the API boundary; it must be threaded to the frame literal in
`SimulationCanvas.tsx` and to the four test frame literals.

Nothing else widens. In particular **terrain does not go on the frame**, for the reason under E2.

## Cliffs

**[corrected] Rim the boundary that is actually a cliff.** A cliff is the boundary of
*(envelope ∩ the positive-ground union) − the union of holes* — the same set
`terrain_supports_point` computes. The draft named only hole boundaries and the outer envelope, and
that is wrong in both directions:

* **Corridor edges are the only real cliffs on any runnable map.** No shipped map and no e2e fixture
  authors a hole; the two corridor maps' road edge is their entire cliff and the draft gives it no
  rim at all.
* **The envelope is not a cliff.** A hole edge kills; the arena edge *folds* you. `SimulationCanvas`
  already strokes the map border from `configuration.world`, and restyling that as a cliff would both
  create the second geometry source the draft promised to avoid and tell the player the wrong thing
  about what happens there. **Leave that stroke exactly as it is.**

Two techniques, both from the geometry the renderer already has:

* **Hole rims fall out of the existing clip.** `drawTerrain` accumulates one even-odd complement clip
  per hole, so the live region is precisely *ground minus the union*. Stroke each hole circle while
  that clip is active and only the ground-side half survives, with arcs interior to an overlapping
  neighbour clipped away — the union outline, correct by construction rather than by extra logic.
  The stroke must be issued after the fill and before the `restore()` that pops the clip stack.
* **Corridor rims are an underprint.** Canvas exposes no outline for a wide stroke and the compiled
  boundary is `simulation::detail`, deliberately not wire data. So stroke each corridor polyline
  twice from the same points: once at `2 * half_width + 2 * rim` in the rim colour, then the existing
  fill at `2 * half_width` over it. The union of the wide strokes minus the union of the narrow ones
  is exactly the road-union outline, and overlapping corridors are correct without special cases.
  All rim passes precede all fill passes, or one road's rim overprints another road's surface.

Rim width is authored in **world units** and projected, so it scales with the camera like every other
world quantity. It is a constant beside the existing fill constants, not a magic number at a call
site.

## Void

**[corrected] The void treatment is terrain-side, not per-body.** The draft wanted a mark beneath a
body whose centre is over void. Three independent findings kill that:

1. The client cannot decide it. Answering "this centre is over void" means a second implementation of
   `terrain_supports_point`, including its asymmetric tolerances — forbidden by that function's own
   canonical note, by ADR 0008 twice, and by the plan's one-geometry-owner Done criterion. The
   antialiasing clause licenses inexact *pixels*, not a computed boolean.
2. **The state does not occur.** A ground-bound body terminates at its last supported point and loses
   its body in the same tick, so no snapshot ever shows a ground-bound centre over void. The only
   bodies over void are floating hazards — and `ground_attachment: floating` bodies never fall, so
   marking them would be a confident lie.
3. The registry structurally cannot host it: one registration per component kind, and the canvas is
   barred from branching on a kind.

So: **paint the void itself.** Give the unsupported region — inside holes on solid ground, off-road
on corridor ground — a distinct recessed treatment in the terrain pass, beneath every entity layer.
That satisfies "void feedback beneath entities" in the z-order sense the layer stack already means by
"beneath", needs no per-body verdict, no second predicate, and no new wire field. Say so explicitly
in the renderer's canonical comment, and say why the per-body reading was rejected, so the next
reader does not re-derive it.

## Combat feedback

**Shield becomes a real renderer, and it draws three states, not two.** [corrected] The draft counted
perfect and ordinary. The third — protection expired or cancelled while the cooldown still runs — is
the *majority of published shield frames* by duration: 160 protected ticks inside a component that
lives at least 360. Presence-keyed drawing would show protection to a player who has none. The
renderer reads the tick and answers:

* `activation_tick <= tick < perfect_expiry_tick` → the perfect opening;
* else `activation_tick <= tick < shield_expiry_tick` → ordinary protection;
* else → **draw nothing**. A live cooldown is not protection.

Geometry comes from the entity's own `physics_body`, which is established practice —
`zoneExposureRenderer`, `lethalOnContactRenderer` and `controllableLabelRenderer` all do it — with
the same discipline: **return silently when the body is absent, and never cache a previous
position.** Step 18 deliberately declined a `dependentRequired` edge precisely so a bodyless tick is
legal rather than a connection close.

**Stun becomes a real renderer** on the same rules: a state a peer must be able to read, drawn only
while `activation_tick <= tick < expiry_tick`.

**[corrected] Charge stays non-visual.** The draft offered "becomes visual, or at least gains
screen-space feedback", which contradicts itself and contradicts the tree's own recorded reasoning:
a charge renderer "would either repaint motion that is drawn or invent an effect the server never
published". The burst is already on screen — it is velocity the body carries. Charge's cooldown is
screen-space only. **Its reason string currently forward-references this step and must be rewritten**
rather than left pointing at the commit that resolved it.

## Layers

**[corrected] Add exactly one layer.** `visualEntityRenderers()` sorts by layer and then by *kind
name*, so registering shield and stun on an existing layer would order them by spelling. Add
`status` between `exposure` and `label`, renumbering `label` 4 → 5. No test pins the numeric values —
only the resulting sorted kind list — and `ENTITY_RENDER_LAYERS` is referenced in two files, so the
renumber is invisible. The pinned draw-order assertion moves deliberately, with its rationale written
beside it, and the three "Step 20 owns this" registry tests are rewritten rather than deleted.

## Screen-space feedback

The HUD owns it, not a renderer: world-space renderers pan and scale with the camera, and a cooldown
readout must not. `sessionSelectors` already computes `inputLocked` from the own entity's stun against
the snapshot tick — that is the template, and the HUD is the only place that can reach
`ticks_per_second` to turn ticks into seconds.

**[corrected] The readout states remaining time, and never states availability.** Two things are not
derivable and must not be implied:

* **Charge readiness.** The safety envelope makes the final refusal and is not on the wire. A "ready"
  indicator would lie whenever the envelope refuses. Say *cooling* and *cooldown over*, never *ready*.
* **The shield's authored window length.** Only the length that actually happened is published, and a
  cancellation shortens it — so a "0.4 s of protection" bar cannot be shown before a first activation
  and is wrong after a cancelled one.

Derivable, and the exact arithmetic each readout may use: stun remaining and fraction over
`expiry - activation`, noting that a merge preserves the activation and takes the maximum expiry so
the denominator can grow and a fraction can move backwards; shield protection, perfect and cooldown
remaining over their respective `<endpoint> - activation_tick`; charge cooldown remaining over
`cooldown_expiry_tick - activation_tick`, which is strictly positive and stable.

**Guard the shield cooldown denominator.** `cooldown_expiry_tick == activation_tick` is an explicitly
accepted frame — a zero authored cooldown — so that fraction divides by zero on a legal snapshot.
Charge's cannot, because its cooldown is validated strictly positive. The asymmetry is deliberate;
handle it rather than discovering it.

## Two rules every new drawing path must obey

* **Nothing may throw.** There is no error boundary anywhere in the client and the draw loop is an
  unguarded `useEffect`, so a throw from any renderer blanks the whole application — the browser
  analogue of Step 19's kPreKernel finding. Absent body: return. Zero denominator: guard. Non-finite
  projection input: do not construct it.
* **Never clip before validation, never hide an unknown kind.** That clause is a *validation* rule,
  not a canvas rule: it forbids trimming a frame instead of rejecting it, and it is enforced by
  `assertKnownSnapshotKinds` failing closed before Ajv. No renderer may become a place where an
  unknown or invalid thing is quietly skipped.

## Verification, and what it does not establish

Run `./scripts/run-linux-toolchain -- ./scripts/verify-web`, the step's stated gate. Tests extend the
existing rendering suites and must cover every world layer under translation, manual and follow
camera, edge view, fractional device pixel ratio, and resize; the shared geometry goldens; and strict
v3 mutation cases — the half of the plan's sentence the draft dropped.

**Be honest about the gate.** `verify-web` asserts call sequences and geometry arguments; it does not
look at pixels, and no shipped map authors a hole, so the hole-rim path is exercised only by
synthetic terrain fixtures. A green gate establishes that the right calls happen with the right
numbers under every camera and DPR condition. It does not establish that a cliff *reads* as a cliff.
That judgement is the owner's, and this step's completion note must say so and put images in front of
them rather than claim it.

## Lockstep

ADR 0008's Step 7 entry states the contract this step refines — "One world layer draws positive
ground minus the union of holes ... race draws objectives only" — and the plan requires a dated
amendment in the same commit. The three registry tests that name Step 20, the client README's
non-visual paragraphs for shield and charge, and `docs/protocol/v3.md`'s statements about those
non-visual registrations all become false when the registrations flip, and all move here.
