# Step 22a: the tactical decision pipeline

Implementation contract, written after the read-only preflight recorded in
[`2026-09-12-tactical-combat-preflight.md`](2026-09-12-tactical-combat-preflight.md) and split from
the original Step 22 at the seam that preflight recommended: **what the observation can already
answer.** Everything here is buildable from published state. Nothing here needs a wire change, a
kernel seam, a gameplay dependency from controllers, or a new command kind.

The combat half — safe-side shove setup, charge timing, the trajectory-based shield decision — is
Step 22b and is blocked on an owner decision. Do not build any of it here, and do not author a
setting for it: ADR 0008 forbids an inert aggression, charge or shield knob, and a fuzz seed
enforces that rule.

## The point of the step

**Selection is the deliverable.** Today `TacticalController` picks its target with
`min_element` over nearest squared distance, with a kind ordinal breaking exact ties, and the profile
is consulted at four call sites — none of them selection. So two profiles differing only in numbers
pick the *same* candidate on the same frame, and the differentiation the plan requires cannot be
proven no matter how many objective kinds are added.

ADR 0008 names the path as "published observation → objective candidates → safety screening →
**utility selection** → steering/actions" and says profiles configure "objective weights, aggression,
risk tolerance, …". The utility stage and the weights do not exist. Build them.

## Profile settings

The section is **`[bot_profile.<name>]`** — there is no `[tactical.<name>]` anywhere in the tree, and
building to that name would create a second competing family. It holds exactly four keys today.

Add only settings this step's behaviour reads:

* **per-kind objective weights**, one per `TacticalObjectiveKind`, which are what make a Keeper
  prefer the hill and a Cautious Racer prefer the road.
* **a risk tolerance**, which scales how much a screened-but-marginal candidate is penalised.
* **a prediction horizon in ticks**, bounded, used by escape screening and by hill intercept.

**Do not add `aggression`, a charge appetite, or a shield timing error.** All three belong to Step
22b, and `aggression` in particular is currently a *pinned rejection* in two places, including
`tests/fuzz/corpus/application/rejected-tactical-profile-inert-combat.cfg`, which exists to enforce
the ADR rule. That seed must keep rejecting; leave it pointed at `aggression` and it still tests
exactly what it names.

**Every authored profile section in the tree must gain the new keys**, because the section family is
closed within an instance: an instance that omits one of its family's keys is `KEY_MISSING`. That is
`config/blob-royale.cfg`, both `frontend-react/e2e/fixtures/*tactical*.cfg`,
`tests/unit/application/fixtures/tactical_profile_configuration_fixture.hpp`, and the four
`tests/fuzz/corpus/application/*tactical-profile*.cfg` seeds. Keep each seed's existing intent: a
`rejected-*` seed must still be rejected for the reason its name states.

## Behaviours

* **Profile-weighted utility selection**, replacing nearest-first. Score every screened candidate
  from its distance, its kind weight and the risk tolerance, in a written operation order, and select
  the maximum with a deterministic tie-break that still ends at the stable kind ordinal so no
  selection can depend on container order.
* **Hysteresis and target-loss handling.** A held target keeps a bonus until its persistence window
  ends or it disappears, so a bot does not switch every frame between two near-equal candidates. The
  existing lease is the mechanism; give it the bonus.
* **Escape screening.** Rank a candidate down when the straight path toward it has no supported exit
  within the profile's horizon. This uses the canonical terrain queries the controller already calls;
  it introduces no second support predicate.
* **Hill intercept and hold** from the published `hill_motion` component. A moving hill's future
  centre over a bounded horizon is a published velocity times a known fixed delta — this is the one
  prediction that needs nothing unpublished, because `hill_motion` publishes committed velocity and
  the hill does not drag.
* **Deterministic reason codes.** A closed enum recording why the pipeline produced what it produced
  — which branch returned, which candidate won, why a held target was kept or dropped. Not free text,
  and not a claim the bot is smart. Expose it the way the existing accessors are exposed so a test
  can assert cause rather than only effect.

## The 32-candidate throw

`require_candidate_count` throws `CONTROLLERS.TACTICAL_CANDIDATE_LIMIT_EXCEEDED` on the raw
per-provider count *before* terrain screening, and the host isolates the throw so the bot silently
stops acting for that pass. Today no provider is opponent-derived, so it never fires. Escape
screening does not add per-opponent candidates, so this step does not trip it either — but the bound
is now load-bearing for Step 22b, which will.

**Do not raise it silently.** Either leave it exactly as it is and record in the header that an
opponent-derived provider will need it revisited, or make the accounting per-provider so one
provider cannot exhaust the shared budget. Pick one, and say which in the code.

## Bounded work

The plan requires measuring bounded candidate and prediction work. Count both, expose the counts the
way the existing accessors are exposed, and assert a ceiling. A prediction is a fixed number of
fixed-delta steps over a bounded horizon; there is no search, no replanning loop, and no planner.

## Two traps the gate will not catch

* **The tactical-movement browser spec pins the `coaster` profile motionless to 1e-9** on position,
  velocity *and* acceleration. Any new setting must leave that profile's behaviour bit-identical, or
  the spec fails — and it fails in a gate the original Step 22 Verify line did not run.
* **The tactical-profiles browser spec asserts the exact published profile list.** Adding a profile
  changes it; adding only settings does not. This step adds settings, not profiles — the four named
  personalities are Step 22b's, because three of the four are defined by combat behaviour.

## The differentiation proof must not be vacuous

`tactical_seed_for` mixes the profile **name**'s length and every one of its bytes, so two profiles
with byte-identical settings already draw differently and already steer differently. A test that
shows "these two named profiles behave differently" therefore proves nothing.

**Hold the name constant and vary only the settings**, or compare against a same-name baseline, so
the difference is attributable to a weight and not to a seed.

## Verification

```
./scripts/verify-focused 'unit.controllers|unit.gameplay|unit.application|fixtures'
./scripts/verify-focused 'unit.controllers|unit.gameplay|unit.application|fixtures' linux-clang-asan-ubsan
./scripts/verify-fuzz-regressions
./scripts/run-linux-toolchain -- ./scripts/verify-web
./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e
git diff --check
```

`unit.application`, the fuzz corpus, web and browser are here because a `[bot_profile]` key change
necessarily breaks all four, which the original Verify line missed. `run-benchmarks-linux` is
dropped: no benchmark case seats a tactical bot, so it cannot observe this step. No replay fixture
seats one either, so no accepted golden is exposed.

Prove: profile differentiation attributable to a weight rather than a seed; the reaction and aim
error bounds still hold; hysteresis keeps a target and target loss releases it; a fallback choice
when every candidate screens badly, rather than a throw or an empty decision; every reason code
reachable; bounded work at its ceiling; and normal command admission — a bot's commands go through
the same `InputBatch` path a human's do.
