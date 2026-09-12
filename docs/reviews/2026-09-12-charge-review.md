# Step 19 verification review

One-shot charge: the additive burst, the authored safety envelope, and the shield/charge conflict
rule. Parent checkpoint `7dd764f` (Step 18). Implementation contract:
[`2026-09-12-charge-contract.md`](2026-09-12-charge-contract.md).

**Every number below is Mac/arm64-hosted Docker Linux/amd64 advisory.** None of it is native
capacity, performance, or release certification, and no push or deployment occurred.

## Completed checks

| Gate | Result |
|---|---|
| `verify-focused 'unit.simulation\|unit.gameplay\|unit.protocol\|unit.runtime\|fixtures'` (GCC) | 1,418 / 1,418 |
| the same filter on `linux-clang-asan-ubsan` | 1,418 / 1,418 |
| `verify-focused 'unit.application\|unit.server\|unit.controllers'` (GCC) | 410 / 410 |
| the same filter on `linux-clang-asan-ubsan` | 410 / 410 |
| `verify-fuzz-regressions` | 99 executions over seven harnesses |
| `run-linux-toolchain -- verify-web` | 859 / 859; 83 schemas, 33 examples; no generation drift |
| `run-linux-toolchain -- verify-browser-e2e` | Chromium 13 / 13, 0 retries, 0 skips, 0 flakes |
| `run-benchmarks-linux` | eight categorized cases plus delivery backpressure |
| `clang-format --dry-run --Werror` over every changed/new C++ path | 56 paths, clean |
| `git diff --check` | clean |

Movement from Step 18: the core filter 1,361 to 1,418, the supplements 408 to 410, the fuzz corpus 95
to 99 executions (two new protocol seeds, each replayed by both protocol harnesses), web 814 to 859
tests and 81/31 to 83/33 schemas/examples.

Of the 128 tracked paths this step touches, 127 stayed hash-identical through the final chain. The
one exception is `src/simulation/README.md`, corrected mid-chain because its `InputBatch` rejection
list still named only a thrust direction; no gate compiles, validates, or reads that file.

## What landed

The existing required `[abilities]` section and the existing `gameplay::AbilityConfiguration` gained
`charge_cooldown_seconds=1.2`, `charge_speed_fraction=0.75` and
`charge_safety_envelope_speed=20000`, closing a promise Step 18's own header had already made. A
body-bound `simulation::Charge` holds one cooldown window and publishes both its endpoints on v3.
`ChargeCommand` is an entity-addressed pulse carrying a direction. The existing `kPreKernel`
`AbilitySystem` — no second system — now owns both abilities and the priority between them.

**Charge introduces no new kernel seam.** No policy socket, no ninth mode declaration, no event root,
no second solver, no `TickContext` member, no new system, and no control binding. The one genuinely
new shared primitive is `gameplay::unit_direction` beside `normalized_thrust_intent`.

## The preflight changed six of eleven drafted decisions

Step 19 arrived with no implementation contract, so a four-agent read-only preflight — three of them
adversarial — reviewed a drafted decision list before any source edit. It rejected six. Three were
defects that would have shipped:

* **`gameplay::normalized_thrust_intent` is a magnitude clamp, not a normalizer.** Its scale is `1.0`
  whenever the magnitude is at most one, and its own header says "Subunit analog intent keeps its
  magnitude" — correct for an analog throttle, wrong for a one-shot activation with a stated fixed
  gain. Routed through it, a client sending `{"x":0.5,"y":0}` would have received half the burst,
  making pointer distance into strength: the exact inverse of the boundary the shield command draws,
  and a contradiction of both ADR 0008's stated gain and the owner's authoritative-fixed-strength
  decision. Charge now uses a new `unit_direction` that divides rather than multiplying by a
  reciprocal and returns `std::nullopt` — never throws — across the whole non-constructible band,
  not merely exact zero. A subnormal direction such as `{"x":1e-200,"y":0}` passes the decoder and
  `InputBatch`, underflows to a zero magnitude, and would otherwise have produced an infinite
  reciprocal inside the ability system.
* **`drag_per_second=0` is the checked-in default**, and holds in seventeen of the eighteen replay
  fixtures — only `royale-drag-decay` authors a non-zero one. At zero drag the integrator's factor is
  exactly `1.0`, so the burst is permanent and the plan's "decaying under drag" is false in the
  shipped development configuration. `deploy/ubuntu-pc/blob-royale.cfg` authors `2.0`, so a deployed
  burst does decay: development and deployment differ here, and the source comments say so rather
  than assuming either. This is what turned the safety envelope from a representability formality
  into the load-bearing guard on repeated activation.
* **A throw inside a `kPreKernel` system kills the room permanently.** `Vector2::operator+` throws on
  a component past `1e12`; that throw escapes `AbilitySystem::apply` and `GameSimulation::step`, and
  the runtime worker then records a worker failure and returns — the simulation thread stops for
  good. One client's charge must not be able to end a match, so the envelope is checked in raw
  doubles *before* any `Vector2` is constructed.

The other three corrections:

* **`charge_cooldown_seconds` must be validated strictly positive.** The shield's zero-cooldown
  exemption is justified by a second gate — a new pulse also requires the prior protection to have
  ended — and charge has no protection window, so a cooldown rounding to zero ticks would admit four
  hundred bursts a second.
* **The payload's `input_generation` stays optional, like `set_thrust`'s.** The draft copied shield's
  required-and-nullable shape on "the two ability commands are one vocabulary" grounds. Three
  committed statements in the tree key that choice on payload shape instead — shield is
  required-and-nullable *because* it has no other member — and all three explicitly contrast it with
  `set_thrust`. Charge carries required `x` and `y`, so the recorded rule puts it on `set_thrust`'s
  side; adopting the draft would have made those three statements false.
* **There is no "whole-sweep coordinate admission" to reuse.** The draft named one. The solver's only
  construction-time body admission checks position and fit, and the broad phase computes its
  whole-epoch endpoint in raw scalars with an explicit comment saying an out-of-domain hypothetical
  endpoint must *not* fail. The envelope had to be a real authored key, not a reuse.

## The safety envelope, and the number in it

ADR 0008 promises "a separate validated physical safety envelope bounds all speeds and event work"
and nothing in the tree implemented it. The owner accepted at Step 1 *that* such an envelope exists;
the owner has never selected its number. `charge_safety_envelope_speed=20000` wu/s is therefore
recorded throughout as an initial tuning assumption and an engineering guard, on exactly the footing
the four shield values already have — **not an owner-selected balance value.**

It is bounded by one checkable cross-key rule rather than an arbitrary constant:
`charge_speed_fraction * kMaximumNormalTopSpeed <= charge_safety_envelope_speed`, refused with
`GAMEPLAY.ABILITY_CHARGE_BURST_EXCEEDS_SAFETY_ENVELOPE`. In words, a charge from rest must remain
admissible whatever a room tunes its ceiling to. That closes the hole an unbounded fraction would
leave: `charge_speed_fraction=1e6` against a 10,000 wu/s ceiling is a 1e10 wu/s burst whose
components sit inside `Vector2`'s domain and which then exhausts `kMaximumMotionEventCount` on the
first tick, and every exhaustion is fatal to the room.

At the default 600 wu/s ceiling a body gains 450 wu/s per activation and is refused once the next
burst would carry it past 20,000, so repeated charges at zero drag converge on the envelope instead
of growing without bound. That convergence is tested, not asserted.

## The conflict rule, written so it cannot decay

ADR 0008 requires eligibility evaluated first, shield winning a same-tick tie without consuming
charge's cooldown, an ineligible shield pulse not suppressing an eligible charge, and charge
unavailable while shield protection is active. The naive "do shield, then do charge" ordering is
observationally equivalent on today's tree — but only because re-reading the store after the shield
write always sees an active window, which is guaranteed by a positivity check in an unrelated file.
Under that ordering "a shield is active" and "a shield was activated this tick" become the same
observation, so no test can separate them.

So `protection_active` is read into a local before either write, both eligibility booleans are
computed before either write, and the conflict gate is spelled `!shield_eligible` rather than "no
shield present". All three clauses are tested separately.

## Accepted residual, recorded rather than fixed

`AbilitySystem` runs last at `kPreKernel`, after `ThrustSteeringSystem`. On an activation tick the
thrust limiter has therefore already sized acceleration against the *pre-burst* velocity, so the
committed endpoint is `v_pre + burst + a*dt` — one tick of already-certified propulsion stacked on
the burst, at most about 1 wu/s at the default acceleration. Running the ability system first would
be worse: the limiter's `max(ceiling^2, v.v)` bound would then include the burst and let thrust
sustain a charged speed indefinitely. The residual is accepted in writing in ADR 0003 rather than
silently reordered.

## Benchmark comparison

All eight categorized cases and the delivery-backpressure workload passed. Compared field by field
against Step 18's committed evidence
([`2026-09-12-shield-composition-baseline.json`](2026-09-12-shield-composition-baseline.json)),
after normalizing JSON integer/float spelling, **exactly one retained field differs across every
case**: `royale_deployed_roster.historical_reference.schema_migration` gained
`;2026-09-12_required_abilities_charge_tuning`, the required provenance entry for the three new keys
in the frozen deployment snapshot.

Identical: every `correctness` block of all eight cases; every `deterministic_work` counter of the
three pure-solver cases, which therefore still match both historical prototype artifacts; the
delivery-backpressure `correctness` block with its `delivered_tick_trace_hash`; and
`royale_deployed_roster`'s `final_snapshot_hash` `fnv1a64:36ee7ca01d83ddb5` with its within-budget
verdicts. `continuous_motion_charge_speed` — the case the plan means by "the charge benchmark against
Step 4's baseline" — retains its `complete_result_hash` and its `approved_charge_tuning: false`
flag; its 40,000 wu/s workload is a solver stress, not the authored tuning.

Timing moved within advisory noise and is not compared. Native capacity and performance remain
uncertified.

## Carried findings

* **Pre-existing whole-tree formatting drift**, unchanged from Step 18's review: the pinned
  `clang-format` reformats sixteen untouched Step 9 random-stream files. Step 24's
  `verify-linux release` runs that check tree-wide and will fail on them.
* **`tests/fixtures/replay_map_fixture_tests.cpp` keeps a curated `kReplayMaps` list** that names
  neither `royale-shield-parry` nor `royale-charge-burst`. Step 18 set that precedent and Step 19
  followed it, so the two newest replay maps are exercised by their own suites but not by the shared
  map-shape sweep. Adding both rows is a small, self-contained improvement for Step 24.

## Exact acceptance commands

```
./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol|unit.runtime|fixtures'
./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol|unit.runtime|fixtures' linux-clang-asan-ubsan
./scripts/verify-focused 'unit.application|unit.server|unit.controllers'
./scripts/verify-focused 'unit.application|unit.server|unit.controllers' linux-clang-asan-ubsan
./scripts/verify-fuzz-regressions
./scripts/run-linux-toolchain -- ./scripts/verify-web
./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e
./scripts/run-benchmarks-linux
git diff --check
```

The separate evidence artifact is
[`2026-09-12-charge-baseline.json`](2026-09-12-charge-baseline.json). Transient logs for this run
are `/tmp/s19-final-all.log` and are not committed.
