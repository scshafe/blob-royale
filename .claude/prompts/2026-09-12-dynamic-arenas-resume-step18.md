# Resume the dynamic arenas plan at Step 18

Work in `/Users/coleshaffer/Projects/blob-royale`. Continue implementing
`.claude/plans/2026-09-10-dynamic-arenas-and-combat.md` end to end with the `executing-plans` skill.
The plan is the source of truth: verify before checking boxes, amend explicit drift, and surface
genuine blockers rather than adding undeclared kernel seams or bypassing verification.

## Read first

1. `.claude/handoff.local.md`, then `git status --short` and the current commit.
2. The plan's status/execution constraints and Steps 18 through 24.
3. `docs/reviews/2026-09-12-shield-composition-contract.md` in full. It is the current Step 18
   pre-implementation contract, written after read-only code review; no Step 18 source code exists.
4. ADRs 0001 through 0008, `docs/protocol/v2.md`, `docs/protocol/v3.md`, and the gameplay,
   simulation, controllers, application and runtime domain READMEs relevant to the next edits.
5. The existing canonical implementations named in the contract, especially
   `shared/guarded_pair_contact`, `shared/status_system`, `shared/input_lock`, `TickWindow`,
   command/component registries, and session-owned command admission.

Use the applicable engineering/planning/handoff skills. Do not delegate reading skill instructions.

## Exact continuation state

- Branch `main`; latest completed checkpoint is `8d2e15e`,
  **Unify falling, gate chronology, and safe return** (Step 17).
- Step 16 is `932ffc6`, **Adopt the reviewed continuous motion kernel**. Both steps are complete;
  do not redo their implementation. The owner accepted Step 5, so Phase C is authorized.
- Step 17 passed both core lanes at 1,200/1,200 each, both boundary supplements at 494/494 each,
  web 769/769 with 79 schemas/29 examples, browser 13/13 without retries/skips/flakes, fixed corpus
  91 executions across seven sanitizer harnesses, and all eight benchmark cases plus delivery.
  Every number is Mac/arm64-hosted Docker Linux/amd64 **advisory**, not native certification.
- Exact evidence/corrections: `docs/reviews/2026-09-11-falling-race-return-review.md` and
  `docs/reviews/2026-09-12-falling-race-return-baseline.json` (includes the frozen input manifest).
  Transient logs: `/tmp/blob-royale-step17.83TlN8`.
- Only Step 18's plan additions and its new contract are uncommitted implementation-planning work.
  Step 18 remains unchecked. No shield source, schema, configuration, or test changes have begun.
- No build, test, benchmark, browser, or subagent implementation task is running.
- Local handoff and prompt files are untracked user/session artifacts. **Never stage them.**
  Preserve the existing untracked original execution prompt as well.

## Start here

Review the Step 18 contract against source, then implement it. Suggested independent ownership:
one worker for Shield/command/AbilityConfiguration/AbilitySystem/status and admission tests; one
for the live guarded adapter, old lethal-row retirement, mode wiring and contact tests. Root owns
application config migration, protocol/schema/client publication, CMake, documentation, integration,
generation, formatting, verification and commits. Give explicit nonoverlapping file ownership;
workers are not alone and must preserve others' changes. Workers do not run build/test/format jobs.

Important preflight decisions already recorded:

- Use the exact existing `compose_guarded_pair`; move its existing lethal predicates/name without
  changing their semantics and delete the old standalone lethal row. Include dynamic/static pairs.
- One required `[abilities]` configuration authors shield/perfect/cooldown/parry-stun durations.
  The ADR's 400/80/900/600 ms values and 25% received-impulse target are initial tuning assumptions,
  not owner-selected balance values. Ordinary protection continues after the perfect opening.
- Capture stun duration on the defending Shield so the noncapturing response can read it from
  frozen world state. Publish that current effect parameter rather than hiding it from browsers
  while exposing it to C++ bots. No charge fields before Step 19.
- Cancel active protection by shortening its existing TickWindows at the stun tick, preserving
  original activation, expired perfect history and cooldown. Do not erase cooldown with shield.
- Shield pulses reuse ThrustCommand's optional input-generation contract (absence initially,
  present zero invalid); stale pre-stun pulses cannot activate after expiry.
- Queue acceptance/local send is not committed activation. Use published windows as positive
  confirmation; do not invent a generic receipt channel or label sending as activation.
- Race course publication must remain before ability admission to lock seeded completed racers.
  Sandbox already auto-starts: initial lobby, tick-one countdown, tick-two running. Do not restore
  the old erroneous claim that it stays in lobby.
- Space remains held cursor-directed propulsion; left-drag remains camera panning. Ability visual
  treatment is Step 20, keys/buttons Step 21. Do not revive WASD or right-mouse propulsion.

## Constraints and completion

- Root alone verifies, one C++ build at a time. Run every exact original and supplemental gate in
  the plan, on GCC then Clang ASan/UBSan as written. Use the pinned toolchain wrappers for web and
  browser. Preserve all assertions; fix concrete failures without retries/skips or hidden fallbacks.
- Keep deterministic arithmetic, the one root/event order, TickWindow timing and body-bound trait.
  No second collision equation, respawn lifecycle, geometry owner, or undeclared kernel callback.
- Preserve historical artifacts/oracles. The benchmark's legacy prototype hash omits newly added
  ground attachment by design; a separate invariant now checks it. Compare retained fields honestly.
- Update ADRs, domain docs, complete v3 schemas/types/examples/validation and the v3 feature row in
  the same step as authoritative behavior. No premature advertised command or reserved vocabulary.
- One local commit per verified step, using the exact step title as its subject. Stage explicit
  paths. Never commit the handoff or either prompt. No push or deployment.
- Native capacity/performance work remains deferred. Mac emulation cannot certify Step 24's native
  release requirement; report that boundary honestly rather than checking it off.
- Continue through the remaining authorized plan until complete or genuinely blocked. Report each
  verified checkpoint's commit, exact commands/counts and any execution refinements.
