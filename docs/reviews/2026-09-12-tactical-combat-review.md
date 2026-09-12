# Step 22b verification review

Shoving and the combat abilities. Parent checkpoint `5b400cd`. Implementation contract:
[`2026-09-12-tactical-combat-contract.md`](2026-09-12-tactical-combat-contract.md). Why the four
named personalities are not here: [the same contract's](2026-09-12-tactical-combat-contract.md)
§ "What this step deliberately does not do", and the plan's Step 22c.

**Every number below is Mac/arm64-hosted Docker Linux/amd64 advisory.**

## Completed checks

| Gate | Result |
|---|---|
| `verify-focused 'unit.controllers\|unit.gameplay\|unit.application\|fixtures'` (GCC) | 799 / 799 |
| the same filter on `linux-clang-asan-ubsan` | 799 / 799 |
| `verify-fuzz-regressions` | 99 executions over seven harnesses |
| `run-linux-toolchain -- verify-web` | 986 / 986; 83 schemas, 33 examples |
| `run-linux-toolchain -- verify-browser-e2e` | Chromium 13 / 13, 0 retries, 0 skips, 0 flakes |
| `clang-format --dry-run --Werror` over 18 changed C++ paths | clean |
| `git diff --check` | clean |

799 from Step 22a's 784. **Both C++ lanes and the browser gate were re-run after formatting**, because
`clang-format -i` touched source after the first pass and the verified tree must be the committed
tree. `verify-web` was not re-run and did not need to be: the formatter touched only `.cpp`/`.hpp`
files and that gate reads none of them. All 29 changed tracked paths are hash-identical between the
final gate run and the commit.

## What landed

The fifth `kShoveSetup` objective kind and its weight; the opponent-derived shove provider running
alongside the mode provider; per-provider candidate budgets with a stable nearest-N filter and a
re-derived prediction ceiling of 96; `request_shield` and `request_charge` beside `request_thrust`;
an authored charge screen as a fraction of the observed arena diagonal, admitted by comparing the
support-exit time against the contact time; a shared perpendicular-velocity alignment gate; and an
authored shield anticipation window with a bounded linear closing test over published state.

## This is the step that makes Step 22a's weights matter

Every shipped mode yields **at most one candidate**: one `Hill`, one `Zone`, and `race()` returns
one gate or one recovery, never both. With a one-element span `tactical_select_candidate` returns
index 0 regardless of score, so **every objective weight was a common factor changing no production
outcome**, proven only against synthetic unit-test candidate sets. The opponent-derived provider is
the first thing that puts two kinds in one set, which is why the weight-differentiation proof in
this step is the first non-vacuous one in the tree.

## Two preflight rounds, and what the second one caught

The first round rejected **all eight** drafted decisions. Root verified the findings against source,
corrected them, and put the corrected set through a second adversarial round — which was the round
that earned its cost:

* **A zero-weight veto would have been an out-of-bounds read.** `tactical_select_candidate` returns
  `candidates.size()` for "no winner" and its one caller indexes without a size check, because the
  empty case is handled earlier. A veto makes a *non-empty* set produce no winner, reachable with a
  profile the tree deliberately blesses. **Corrected by the Step 22c preflight:** this review
  first called that a container-overflow abort on the sanitizer lane. It would not have been. The
  candidate vector is `reserve`d at the raw merged count and filled only with screen survivors, so
  whenever anything is screened out `capacity > size` and the read lands *inside* the live
  allocation with no sanitizer report — then the lease copies a candidate whose `key.kind` is an
  arbitrary byte, which indexes a five-element array on the next pass. Silent, and worse.
* **An unconditional arrival brake would have left bots permanently inert.** Steering emits a bare
  unit direction, so `-v/|v|` at rest is 0/0; the clamp passes NaN through by design and
  `Vector2::create` throws; `ControllerHost` catches and `TacticalController` rolls state back
  without advancing `last_completed_tick`, so it repeats every pass. The triggering state is a bot
  that arrives at the hill and comes to rest — the terminal state of every successful capture.
* **Gating abilities on the seek draw contradicts itself.** That draw sits inside the not-arrived
  branch, so gating there means a bot standing on the hill can never shield; adding a draw on the
  arrived branch moves every authored profile's RNG stream.
* **The charge screen must compare exit time to contact time.** A `.has_value()` test on
  `first_support_exit` is always true for a charge aimed at a hazard, so it would have vetoed every
  shove the step exists to enable.
* **The alignment gate certifies the wrong angle if it compares aim to intent.** The burst is
  additive: a body at 600 wu/s along +y charging +x leaves 53.1 degrees off the commanded ray.

## Decisions worth recording

* **No `aggression` key, and that is the design.** Its preference half *is*
  `objective_weight_shove_setup`, forced into existence by the fifth kind under Step 22a's
  one-key-per-kind rule; its danger-appetite half is exactly what `risk_tolerance` was built
  one-signed to forbid. Because the key is never created, both pinned rejection rows stay verbatim
  and neither needed a re-pointing.
* **The candidate target is the safe-side standing point S, never the opponent.** With the opponent
  as target, `escape_blocked` would be true for every shove by construction and one
  `risk_tolerance` would have to do two conflicting jobs — the same authored number that lets a
  Bully shove would cancel its cliff caution on the race gate. The header carries the argument
  because a later reader will otherwise "simplify" it back.
* **The charge screen is authored as a fraction of the observed arena diagonal, not in world
  units.** The same absolute number would mean 1378, 436 and 342 world units of stopping distance
  across three configurations already in this tree.
* **The screen bounds visible risk; it does not guarantee survival.** At the development config's
  zero drag an aligned charge from the ceiling needs 1378 world units to stop against a 1154-unit
  diagonal, so no ray length makes it safe; under the deployed drag of 2.0 the same charge stops in
  342. Both figures are written beside the constant with the drag each assumes, because
  `ability_system.hpp` already warns that development and deployment differ here.
* **The shield decision is named as prediction, not as authored caution.** ADR 0008 authorises
  "visible trajectories", and the owner's line is drawn at unpublished physics rather than at
  arithmetic over published state. The authored number is the window; the closing test is a bounded
  linear extrapolation biased early wherever drag is nonzero. Calling that "authored caution" would
  have been the dressing-up the same ADR forbids.
* **`kMaximumTacticalPredictionStepCount` is 96**, derived rather than authored: 32 mode intercepts,
  zero shove intercepts because authored caution extrapolates no opponent into a candidate, and 64
  escape rays over the merged set. The hazard-direction lookup is deliberately *not* counted; it is
  arithmetic over authored terrain, and counting it would dilute what the counter names.
* **N for the nearest-N filter is a compile-time constant**, and the filter is a fixed-size stable
  insertion rather than `nth_element` or `partial_sort`, which give no reproducible order among
  equal elements and would break this file's bit-identical-selection contract across toolchains.

## Corrections to already-shipped work

* **Two READMEs asserted the opposite of a committed file.** Both said the inert-combat fuzz seed was
  "deliberately **not** migrated"; `5b400cd` added six lines to it. The *mechanism* they gave was
  right — the parser refuses an unknown key at the line carrying it, before the missing-key sweep —
  while Step 22a's review had the fact right and the mechanism wrong. Both halves are fixed.
* **The seed is not a guard at all**, which is the more useful correction and is now recorded in
  both READMEs: `verify-fuzz-regressions` asserts only that the target does not crash, and
  `application_config_fuzzer.cpp` catches every typed loader error and returns 0, so a seed that
  started being *accepted* would still pass. The live guard on ADR 0008's no-inert-combat-knob rule
  is a single `ParserFailure` row in the application configuration fixture.
* **ADR 0008 said the perfect opening's length "is never published".** That is false: every `Shield`
  value is published verbatim, so `perfect_expiry_tick - activation_tick` is observable the moment
  anyone raises a shield. What is unpublished is the length before any shield exists. The derivation
  is symmetric, so no boundary weakens — but this step's shield reasoning rested on that sentence.

## Limits recorded rather than repaired

* **A hosted bot pays no command-rate cost.** The per-session bucket lives on the WebSocket session
  and a bot reaches the mailbox without entering the server library, so traffic that would close a
  human's socket at `1008` costs a bot nothing. The kind mask is symmetric; only rate is not. This
  is defensible as a denial-of-service control on an untrusted socket rather than a gameplay rule,
  and the response is a controller that emits at most one ability per pass, not a second rate
  authority.
* **Abilities are derated by the reaction gate.** At the shipped `steady` profile's 80-tick reaction
  delay against 20-tick decision spacing, roughly four passes in five decide nothing, so a defensive
  shield has about a 20% duty cycle on top of the one-to-twenty-one-tick activation jitter. ADR 0008
  requires reaction to apply here, so no second, faster reflex path was invented to hide it.
* **No gate exercises combat prediction at the drag the deployed game runs at.** Both tactical
  browser fixtures author 40, `config/blob-royale.cfg` authors 0, and the deployment authors 2.0.
  Every error figure in the shield reasoning is about 2.0 and nothing visits it. Carried to Step 23,
  which owns cross-mode production behaviour.
* **Worst-case screening terrain work per bot per pass doubles**, from at most 32 escape rays to at
  most 64, and `run-benchmarks-linux` is correctly dropped from the Verify line — so nothing in the
  gate observes the doubling. Stated in the header rather than left to read as free.

## Execution corrections

**One file no worker owned.** `src/controllers/tactical_profile.cpp` threw two
`ControllersValidationCode` values that did not exist, because `controllers_validation_error.hpp`
was in no worker's list. Root added both enumerators in validation order and both rows of the
no-`default` switch. Two independent workers reported it as a blocking build break before root got
there, which is the partition working rather than failing.

**Whole-tree formatting drift did not recur, but this step's own output needed formatting.** Eleven
of the eighteen changed C++ paths needed `clang-format -i`, all in test files. Both C++ lanes and the
browser gate were re-run afterwards.

## Exact acceptance commands

```
./scripts/verify-focused 'unit.controllers|unit.gameplay|unit.application|fixtures'
./scripts/verify-focused 'unit.controllers|unit.gameplay|unit.application|fixtures' linux-clang-asan-ubsan
./scripts/verify-fuzz-regressions
./scripts/run-linux-toolchain -- ./scripts/verify-web
./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e
git diff --check
```

Transient logs are `/tmp/s22b-*.log` and are not committed.
