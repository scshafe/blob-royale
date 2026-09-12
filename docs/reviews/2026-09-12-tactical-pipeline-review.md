# Step 22a verification review

The tactical decision pipeline. Parent checkpoint `7e160cd`. Implementation contract:
[`2026-09-12-tactical-pipeline-contract.md`](2026-09-12-tactical-pipeline-contract.md). Why this is
only half of the original Step 22:
[`2026-09-12-tactical-combat-preflight.md`](2026-09-12-tactical-combat-preflight.md).

**Every number below is Mac/arm64-hosted Docker Linux/amd64 advisory.**

## Completed checks

| Gate | Result |
|---|---|
| `verify-focused 'unit.controllers\|unit.gameplay\|unit.application\|fixtures'` (GCC) | 784 / 784 |
| the same filter on `linux-clang-asan-ubsan` | 784 / 784 |
| `verify-fuzz-regressions` | 99 executions over seven harnesses |
| `run-linux-toolchain -- verify-web` | 986 / 986 |
| `run-linux-toolchain -- verify-browser-e2e` | Chromium 13 / 13, 0 retries, 0 skips, 0 flakes |
| `clang-format --dry-run --Werror` over 16 changed C++ paths | clean |
| `git diff --check` | clean |

All 28 tracked paths stayed hash-identical through the chain.

**The Verify line is not the plan's original.** `unit.application`, the fuzz corpus, web and browser
are here because a `[bot_profile]` key change necessarily breaks all four — the section family is
closed within an instance, so an instance that omits a family key is `KEY_MISSING`. The original line
ran none of them and instead ran `run-benchmarks-linux`, which is dropped: no benchmark case seats a
tactical bot, so it cannot observe this step. No replay fixture seats one either, so no accepted
golden is exposed. The plan was amended to record this.

## What landed

The profile-weighted utility selection stage, per-kind objective weights, a risk tolerance, a bounded
prediction horizon, hysteresis on the existing lease, escape screening, hill intercept from the
published `hill_motion`, deterministic reason codes, and bounded candidate/prediction accounting.

## Why the selection stage was the whole point

Before this step, `TacticalController` picked its target with `min_element` over nearest squared
distance, with a kind ordinal breaking exact ties, and the profile was consulted at four call sites —
none of them selection. **Two profiles differing only in numbers therefore picked the same candidate
on the same frame.** The differentiation the plan requires could not have been proven no matter how
many objective kinds were added, so adding kinds first would have been building decoration.

## Decisions worth recording

* **One key per kind, not a positional list.** `objective_weight_hill` and its three siblings are
  separate keys, so an unauthored kind is the parser's own `KEY_MISSING` naming the key, before any
  domain rule runs. A positional list would make a kind's weight a position rather than a name in
  every authored file and turn a wrong length into an arity rejection.
* **Closure against the enum is the compiler's job.** The key-name and weight-lookup functions are
  switches with no `default` under `-Werror`, so a fifth `TacticalObjectiveKind` is a build failure
  before it can become a kind every profile silently weights at zero. Nothing else in the tree closed
  that hole: the ordinal is a cast and the count is a constant, and neither notices an appended
  enumerator.
* **All-zero weights are a named rejection, not a neutral setting.** Zero is exactly what C++ fills
  an omitted aggregate initializer with, and the parser cannot see that a caller was left behind. It
  is also the one weight set that makes selection inexpressive — every candidate scores alike and the
  choice collapses onto the kind ordinal. A profile with genuinely no preference authors equal
  *positive* weights.
* **Risk tolerance can only scale a penalty away, never add score to a dangerous candidate.** That
  directionality is why it is not Step 22b's aggression knob wearing a different name, which ADR 0008
  forbids while the behaviour is absent.
* **The prediction horizon is bounded at one second**, not the four seconds other tick settings
  allow. A hosted bot decides about twenty times a second, so one second already reaches past twenty
  of its own future decisions; longer is a planner's horizon and this step ships no planner.
* **The 32-candidate throw was left exactly as it is**, with a header note that an opponent-derived
  provider will need it revisited. This step adds no per-opponent candidate so it cannot trip it, but
  it becomes load-bearing for Step 22b.

## Execution corrections

**Three stale profile call sites, none of them in any worker's file list.** Two C++ fixtures built a
`TacticalProfile::Section` with positional aggregate initializers, which C++ fills with zeros for the
new members — so they compiled and then threw the all-zero-weights rejection at every `create()`,
which would have failed every controllers test that builds a profile.
`tests/unit/application/fixtures/profiled_bot_reconciliation_fixture.hpp` is the one worth naming:
it is C++ rather than a `.cfg`, so the parser's family closure could never have caught it, and it
appeared in no preflight inventory of "authored profile sections".

**The inert-combat fuzz seed needed the new keys after all, and the contract said otherwise.** The
contract told the configuration worker to leave
`rejected-tactical-profile-inert-combat.cfg` untouched, reasoning that it must keep rejecting. That
was right about the goal and wrong about the mechanism: without the new required keys the seed starts
rejecting for `KEY_MISSING` instead of `KEY_UNKNOWN` on `aggression`, so it would still have been red
while no longer testing the rule its name states. It now authors the six new keys **and** keeps
`aggression`, so ADR 0008's no-inert-combat-setting rule is still the thing under test.

**A CTest discovery permission failure on the sanitizer preset**, not a code failure: a stale
generated `*_tests.cmake` in the clang build directory could not be overwritten. Removing the stale
discovery files and re-running passed 784/784. This is the same class of infrastructure fault Step 16
recorded, and it is worth knowing it recurs.

## The differentiation proof is not vacuous

`tactical_seed_for` mixes the profile *name*'s length and every one of its bytes, so two profiles with
byte-identical settings already draw differently and already steer differently. A test showing "these
two named profiles behave differently" would prove nothing about the settings. The proof holds the
name constant and varies only a weight.

## What this step deliberately does not do

No shoving, no charge timing, no shield decision, and **no aggression, charge-appetite or
shield-timing setting** — ADR 0008 forbids an inert combat knob and a fuzz seed enforces it. Those
are Step 22b, which the owner unblocked on 2026-09-12 by choosing authored caution over derived
physics; the boundary and its accepted consequences are recorded in ADR 0008
§ "Owner decision: authored caution for bot combat".

## Exact acceptance commands

```
./scripts/verify-focused 'unit.controllers|unit.gameplay|unit.application|fixtures'
./scripts/verify-focused 'unit.controllers|unit.gameplay|unit.application|fixtures' linux-clang-asan-ubsan
./scripts/verify-fuzz-regressions
./scripts/run-linux-toolchain -- ./scripts/verify-web
./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e
git diff --check
```

Transient logs are `/tmp/s22a-final-all.log` and `/tmp/s22a-clang.log` and are not committed.
