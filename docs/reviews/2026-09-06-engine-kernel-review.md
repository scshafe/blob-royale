<!-- canonical: engine_kernel_review -- independent review of plan Steps 15 to 17 -->

# Engine kernel review, 2026-09-06

Independent review of commits `1b69ed9`, `99e4c64`, and `d70d65f` (plan Steps 15 to 17: components
and stores, the command vocabulary and `InputBatch`, and the staged tick kernel) against the owner's
extensibility requirement, ADRs 0002 to 0004, and the agent-first and emergence skills.

**Headline.** A component kind and a mechanic really are "new files plus one registration line". A
command kind is not. The most consequential defect is not an extensibility gap: the committed
spatial grid can silently disagree with the committed bodies.

Every finding below is assigned to the step that closes it. Findings 1 and 12 were sent to the
Step 18 agent while it was still editing that code.

## Findings that force rework if deferred

| # | Finding | Closes in |
|---|---|---|
| 1 | The grid rebuild predicate tests the entity roster, but the grid is built from the `PhysicsBody` store. A hook-stage system that inserts a body commits a grid missing a live body; one that calls `destroy_entity` directly commits a grid holding a dead id and throws on the next tick. Asserted in a comment, not enforced. | Step 18 |
| 2 | `GameWorld` has two sources of truth for "which entities exist": the roster and the component stores. `mutable_store<C>()` lets either drift from the other, and nothing reconciles them at commit. Finding 1 is one symptom; a snapshot whose `entities()` omits an entity its components publish is another. | Step 19 |
| 3 | A system cannot create an entity. `create_entity` takes an explicit id, the world owns no reservation, and `TickContext` carries none. Every mechanic that makes something — projectile, pickup, zone entity, dropped flag — is blocked. The reservation exists and is correct; it is simply not plumbed to the world. | Step 19 |
| 4 | `Controllable::commands_this_tick` is published in every snapshot, so an in-process bot reads every player's live input for the current tick. The v2 schema deliberately withholds this field from the wire for exactly that reason. It breaks the ADR's human/bot symmetry in the bot's favour and costs a per-entity vector copy per publication. | Step 19 |

## Findings that cost compounding friction

| # | Finding | Closes in |
|---|---|---|
| 5 | Adding a command kind touches three existing files at eight sites, not the "one existing file" the registry claims and the two ADR 0004 claims. The third, `recorded_entity_of` in `game_simulation.cpp`, is documented nowhere and duplicates `addressed_identity_of` in `input_batch.cpp`: one capability, two implementations, two places to forget. | Step 20 |
| 6 | Two canonical orders for one command list: `InputBatch` orders by application rank, then `Controllable` is re-sorted by ascending `CommandKind` for no stated reason. The author's own comment says the sort is a no-op today. | Step 20 |
| 7 | `kCommandKinds` and `kWorldEventKinds` are hand-typed parallel arrays guarded only by a size check, so a wrong entry passes and silently drops a kind from `CommandKindMask::all()`. The rank-injectivity assertion is three hand-written pairwise comparisons needing ten at five kinds; if two kinds ever share a rank, `InputBatch` silently drops one command. Both are derivable from the variant with the traits that already exist. | Step 20 |
| 8 | `ComponentStore::mutable_entries()` exposes `Entry::entity`, so a caller can break the ascending order every phase and every binary search depends on. No test covers the mutable accessors. | Step 20 |
| 9 | No canonical two-store join, though ADR 0004 names the pattern. Every mode and mechanic needs one; absent a named helper each writes its own merge. | Step 20 |

## Smaller findings

| # | Finding | Closes in |
|---|---|---|
| 10 | `PhysicsBody::kDefaultRadius` is `0.0` while every phase reads the radius from `SimulationConfig`, so the one body value carries a field that is a lie for some bodies and unread for all. A growing blob needs `with_radius`, which does not exist. | Step 18 or 21 |
| 11 | `kMaximumPlayerCount` now bounds entity rosters and every component store; `grep kMaximumEntityCount` finds nothing. The simulation permits 4,096 entities while protocol v2 caps a snapshot at 1,024, and systems can cross that at runtime, not only at startup. | Step 20, bound at Step 26 |
| 12 | `ContactEvent::rule_name` is a borrowed view whose lifetime argument fails for a rule built from a temporary. A fixed-capacity owned name costs nothing today. | Step 18 |
| 13 | Two exception vocabularies: `std::logic_error` for some engine invariants, typed `SimulationValidationError` with a greppable code for identical ones elsewhere. | Step 20 |
| 14 | Commit validates bodies in bounds before applying despawn removals, so an entity a system both pushed out of bounds and marked for despawn stops the match. Royale's elimination pairs exactly those two. | Step 19 |
| 15 | `SystemPipeline::systems_at` indexes a four-element array with an unchecked cast in a `noexcept` function, though `create` guards the same hazard fifteen lines earlier. | Step 20 |
| 16 | `1.0 - (drag_per_second * delta_seconds)` is a multiply-subtract and the build pins no `-ffp-contract`. Exact at zero drag, so no accepted horizon is at risk; observable the day a nonzero drag ships. `-ffp-contract=off` is cheap insurance. | Step 19 |
| 17 | `SpawnEvent`, `EliminationEvent`, and `ScoreEvent` are registered with no consumer until Step 21, and `EliminationEvent` encodes a Royale-specific two-phase protocol inside the engine's closed registry before the mode exists. | Step 21 |

## What the review confirmed

Component generation works as promised: world equality, `destroy_entity`, and snapshot construction
all visit the registry type list, and only `PhysicsBody` and `Controllable` are ever named concretely
in engine code, so no new component kind can silently fail to participate. Determinism is clean —
no unordered container, hash, pointer-order dependency, or clock anywhere in `blob_simulation`, and
both new sorts are total with their tie-breaks reasoned in comments. Error paths fail closed
throughout, and the tick is transactional against a working copy with a non-throwing commit.

The review also credits the `AcceptedBaselineTick` oracle as the right way to prove a refactor
changed no arithmetic, the test doubles for writing through world-owned components rather than
test-owned buffers, `src/simulation/README.md` for being more honest than ADR 0004 about the cost of
a command kind, and the deviation rationales recorded at the point of each decision.

## Documentation corrections owed

ADR 0004 § "Libraries, and where a new thing goes" states that a command kind edits one existing
file. The true count is three, so the section's "six of eight additions are new files plus one
registration line" headline is five of eight until finding 5 is closed. `src/simulation/README.md`
already states the honest count and should be the model.
