# Step 16 live motion integration contract

Status: implementation contract, not verification evidence. Step 5 was explicitly accepted on
2026-09-11. This supplements the reviewed Step 4/4a prototype; it does not replace its numerical
limits, historical oracles, or native-certification requirement.

## Kernel boundary

Freeze the working world after phase 0 and `kPreKernel`. Phase 1 derives accelerated/dragged
subjects separately. Predicates, effect-policy projection, trigger binding, and responses all
read that same immutable kernel-entry world; current motion comes from the supplied subject,
not the frozen body's velocity. Replace only phases 2–5 with `solve_continuous_motion`.
Keep intake, stages, lifecycle, phase 6, and the final noexcept world/grid/tick commit.

`ContactRule::Response` receives that world and the certified `PairContactObservation`.
`ContactResponse` uses existing `MotionBodyResult`/`MotionDisposition`, not another enum.
Unchanged means current bodies, continuing motion, and no effects. Built-in rows call existing
contact-taking physics overloads, never radius-based re-detection. A touch without an impact
cannot produce an impulse. Preserve first-match and canonical-before-swapped rule order.
Swapping negates certified normals, preserves distances/relative normal speed, swaps source
eligibility, and maps both bodies and dispositions back without detecting again.

The existing lethal row uses source eligibility and terminates a victim immediately while
emitting its existing elimination event. Full guarded composition and deletion of that row
remain Step 18. Only solver `effects` are gameplay events; the selected-certificate diagnostic
trace is not. Retain solver-owned actual paths through the operation, without adding an unused
public path store or generic timestamped event bus.

## Eighth declaration and lifetime

`GameMode::motion_triggers()` returns an independently owned `MotionTriggerTable`, once at
`GameSimulation::create` (not `GameSimulationSetup::of_mode`). Default is empty. Explicit setup
tables are allowed only without a mode; duplicate declaration sources fail. No ninth socket.

Each declaration owns `unique_ptr<const MotionTriggerPolicy>`, feature base, and cursor limit.
A policy exposes const `bind` (eligibility/initial cursor only), `query`, and `respond` methods
against the frozen world/context and current subject. Binding cannot run geometry or mutate.
Traverse body entities in ascending ID, then declaration order. Check body count before nested
traversal, non-null policies and checked feature ranges at table construction, and row/cursor
limits during binding. The solver remains the owner of overlapping-feature validation and all
chronology, root work, candidate generation, and no-progress checks.

`LiveMotionFacts` is a checked variant borrowing either `ContactRuleTable` or one trigger policy;
wrong-capability access fails with `SimulationValidationError`. `BoundMotionTriggers` owns a
facts vector and rows vector, is move-only, and exposes rows only on lvalues. Build all bounded
metadata/facts before rows borrow them; no later reallocation. Standard-allocator vector moves
preserve loans. Forbid binding temporary tables. Tables and bound owners outlive the synchronous
solve; policies must never retain references to world/context/budget/bindings.

Append an optional `reference_wrapper<const Facts>` override to the existing generic
`MotionTrigger`. Both query and response select that reference when present; absence uses the
original global facts reference. Pair callbacks always use global facts. No copied/owned Facts,
void pointers, downcasts, cursor-encoded policy identity, or changes to ordering/caches/budgets.
This preserves noncopyable/abstract Facts supported by the borrowed generic API.

Step 16 proves support-capable injected declarations and immediate termination. Production
ground attachment, falling and race trigger registrations remain Step 17.

## Canonical body legality and bounded failure

Promote the reviewed solver's existing body-envelope validation, preserving its arithmetic:
static centers must be inside closed bounds even with the cross flag; dynamic crossing bodies
are exempt; other dynamic centers must be inside, diameter must fit each axis, and an exactly
zero-span axis requires zero velocity on that axis. Initial wall-radius overlap is legal and
does not cause snapping/depenetration. Effective body radius controls walls. Startup, grid
coverage and final commit share this guard; spawn clearance remains a stricter separate rule.
Keep grid padding arithmetic and static authored-radius normalization unchanged.

The existing MotionLimits ceilings remain fail-visible correctness limits, not certified
capacity. `GameSimulationSetup::with_motion_limits` may lower them using the existing canonical
validator; no configuration key or mode socket is added. Reject excessive initial body count
at creation and same-tick growth transactionally. Never raise ceilings, drop bodies, or fall
back to the discrete kernel. Validate all results, append bounded effects and run remaining
stages/index construction before commit. Include existing pre-kernel events in event capacity.

Tests cover callback throws, malformed results/dispositions, lower resource budgets, trigger
no-progress, invalid policy projection, post-solve event overflow, late-stage failures, and
retry parity including world/RNG/reservations/status/tuning/index.

## Per-object authoring and publication

One sparse body-bound `ContactEffectAdmission` stores only `any_touch`; absence means
`closing_impact`. Canonical validated assignment, effective lookup, and string conversion use
instance override > archetype default > closing impact. Explicit closing impact removes an
inherited nondefault. Reject invalid enum/string, redundant stored default, or bodyless policy;
project body-backed rows in ascending ID. No arbitrary component-patch command or sensor filter.

`StaticBodyDeclaration` owns body plus explicit policy in `MapDefinition`; no parallel policy
vector. Strict static CSV gains `contact_effect_policy`. Hazard sections gain the same required
key as an archetype default. All existing CSV rows/headers and hazard sections explicitly author
`closing_impact`; geometry, order, values, RNG draws, budgets, lifetime arithmetic and static
radius normalization remain unchanged. No compatibility grammar or new authored radius column.
Promote existing crossing-hazard creation to one typed operation with optional instance policy;
scheduled spawning calls it without an override. New fixtures demonstrate any-touch behavior.

Register the body-bound component, lifetime tests, C++ encoder, sorted v3 kind table, closed
schema (`policy` is required and exactly `any_touch`), entity schema row, example, generated
TypeScript, validator tests, and nonvisual renderer registration. Document absence/default
semantics in v3.md. Stay v3.0 as the plan requires. Existing absent-component goldens remain.

## Proof and cutover sequence

1. Add unused canonical body-envelope and crossing-hazard creation candidates with independent
   old/new proofs before switching their readers. Add the generic borrowed-facts override and
   unused live trigger binding infrastructure. Prove absent-override output/work identity,
   original facts address and noncopyable/abstract Facts, per-instance query/response routing,
   pair global facts, moved-owner loans, and invalid range/cursor/budget rejection.
2. Root runs both original Step 16 focused lanes and benchmark runner serially. Compare all
   historical prototype hashes/work, without changing their baseline artifacts. No reader
   cutover until these prerequisite proofs pass.
3. Integrate live motion, authoring/component and protocol/browser slices with explicit file
   ownership. Update ADR 0003/0004 and domain docs in the final Step 16 commit.
4. Run original Step 16 gates plus both
   `unit.application|unit.protocol|unit.runtime|unit.server|unit.controllers` supplements,
   fixed corpus, complete web, and production-browser gates. Record exact counts, changed
   expectations and source hashes. Mac-hosted Linux/amd64 results are advisory only.

Preserve `physics_tests.cpp`'s discrete wrapper crossing test. The intentional live crossing
inversion belongs in `game_simulation_tests.cpp`; review live pair/wall/replay differences
individually. Historical oracle code and Step 4/4a baseline artifacts stay untouched; do not
retarget a legacy oracle to the new solver or blanket-regenerate fixtures.

## Owner-approved benchmark migration (2026-09-11)

The owner approved the disposition in the prerequisite review. Preserve sparse-64 and historical
royale as live-engine cases. Convert only sparse-512, sparse-2048 and clustered-512 to explicitly
named spatial-grid diagnostics, keeping original world/configuration/layout and counts, former
case names as provenance, and the existing grid measurement implementation. Do not simulate
oversized populations, fabricate final snapshots, keep another engine, or choose a runtime
fallback based on the cap. Emit no retired stepping/post-step snapshot/encoding measurements.
Compare only retained grid operation/count/hash fields to the old workload. Keep all three pure
solver cases and their complete workload/correctness/work comparisons unchanged. Historical
artifacts remain historical; document new suite categories in the new evidence and benchmark
README. The same required benchmark runner must pass all applicable assertions.
