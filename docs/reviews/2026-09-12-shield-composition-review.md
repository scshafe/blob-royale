# Step 18 verification review

Tap shield, the shared ability stage, and the one live guarded contact composition.
Parent checkpoint `8d2e15e` (Step 17). Implementation contract:
[`2026-09-12-shield-composition-contract.md`](2026-09-12-shield-composition-contract.md).

**Every number below is Mac/arm64-hosted Docker Linux/amd64 advisory.** None of it is native
capacity, performance, or release certification, and no push or deployment occurred.

## Completed checks

| Gate | Result |
|---|---|
| `verify-focused 'unit.simulation\|unit.gameplay\|unit.protocol\|unit.runtime\|fixtures'` (GCC) | 1,361 / 1,361 |
| the same filter on `linux-clang-asan-ubsan` | 1,361 / 1,361 |
| `verify-focused 'unit.application\|unit.server\|unit.controllers'` (GCC) | 408 / 408 |
| the same filter on `linux-clang-asan-ubsan` | 408 / 408 |
| `verify-fuzz-regressions` | 95 executions over seven harnesses |
| `run-linux-toolchain -- verify-web` | 814 / 814; 81 schemas, 31 examples; no generation drift |
| `run-linux-toolchain -- verify-browser-e2e` | Chromium 13 / 13, 0 retries, 0 skips, 0 flakes |
| `run-benchmarks-linux` | eight categorized cases plus delivery backpressure |
| `clang-format --dry-run --Werror` over every changed/new C++ path | clean |
| `git diff --check` | clean |

The core filter grew by one over Step 17's 1,360 (the new replay case). The supplemental filter
grew from 494 to 408 because Step 17 ran `unit.application\|unit.runtime\|unit.server\|
unit.controllers` while Step 18's contract moves `unit.runtime` into the core filter; the two
filters together cover the same targets. The fuzz corpus grew from 91 executions to 95: two new
`command-envelope-shield*.json` seeds, each replayed by both protocol harnesses. The web gate grew
from 769 to 814 tests and from 79/29 to 81/31 schemas/examples: two new v3 schemas and two new
golden examples.

## What landed

One authored `[abilities]` section owns `shield_duration_seconds=0.4`,
`shield_perfect_window_seconds=0.08`, `shield_cooldown_seconds=0.9` and
`parry_stun_duration_seconds=0.6`, converted once through the shared `duration_ticks` to 160, 32,
360 and 240 ticks. **These are ADR 0008's initial tuning assumptions, not owner-selected balance
values**, and the 25% received-impulse target remains the tested pure core's, with the pair-level
non-closing correction taking precedence over the exact fraction.

`simulation::Shield` is a validated body-bound component: three `TickWindow`s sharing one positive
activation plus the parry-stun duration captured at activation, all five values published on v3.
`ShieldCommand` is an entity-addressed pulse with `ThrustCommand`'s exact optional-generation
semantics. A shared `kPreKernel` `AbilitySystem`, declared last by all four gameplay modes, is the
only writer of an activation and the only remover of a fully expired shield. `StatusSystem` cancels
still-active protection on an active stun. One `guarded_pair` contact rule over the unchanged
`compose_guarded_pair` core replaces the standalone `lethal_hazard` row in all four modes.

## Decisions taken during execution

* **Cancellation ends the perfect opening as well as the protection.** The contract says
  cancellation shortens "only a still-active protection window" and preserves "already-elapsed
  perfect history"; the opening is a prefix of the protection, so a cancellation inside it shortens
  both to the cancelling tick and a cancellation after it has elapsed touches neither. Leaving an
  unelapsed opening alive would have left a stunned, cancelled defender still answering `kPerfect`
  to the next contact — the protection the cancellation exists to remove — and would also have
  broken the published invariant `activation <= perfect_expiry <= shield_expiry`. Elapsed history
  is preserved exactly: the activation never moves and ticks before the cancellation still parried.
* **The declared row is `guarded_pair`; the lethal diagnostic name is retained.** The composition's
  lethal branch still publishes `ContactEvent.rule_name == "lethal_hazard"`, which is what the
  retained pass-through proofs assert, while every composed non-lethal contact now names
  `guarded_pair`. One row, two diagnostic names, exactly as the contract requires.
  `kLethalHazardContactRuleName`, `body_is_lethal_hazard` and `body_is_player_driven` moved verbatim
  into `shared/guarded_pair_contact.{hpp,cpp}` — their only non-test consumer — with their full
  rationale, and the standalone row's two files were deleted.
* **`command_kind_application_rank(kShield)` is 10, after `kLeave`.** The contract requires existing
  bits and ranks preserved. A shield pulse is a pure recorded intent whose only ordering obligation
  is to follow `spawn`; applying it after the lifecycle kinds means a body that despawned or left on
  this tick has no `Controllable` to record into, which is the correct outcome.
* **Sandbox gained the row, the system, and `kShield`.** ADR 0008's mode/state matrix enables
  abilities in Sandbox running with a body, so its three built-in rows became unreachable and its
  header paragraph and its exact-equality-with-`built_in()` test changed deliberately. Engine-only
  modes that declare no row still reach the built-ins, which is what keeps them a live baseline.
* **`shield` takes no `dependentRequired: ["physics_body"]` edge**, following `stun` rather than
  `contact_effect_admission`. Body-bound cleanup runs when the shared respawn sweep runs, not
  unconditionally at every commit, so a one-tick bodyless window is reachable and the edge would
  turn an ordinary tick into a client-wide `1003` close.
* **No `Controller::request_shield`.** `Controller` deliberately holds exactly two capabilities and
  Step 22 owns bot shield policy. `ScriptedReplayController`'s log already carries whole `Command`
  values, so bot and replay symmetry is proven without a premature capability.
* **The replay format learned the `shield` verb, and a fixture exercises it.** No tenth
  `input_generation` column and no `[abilities]` section in any `match.ini`: the parser does not
  read that section and rejects unread keys, so replays inherit `GameModeConfiguration::defaults()`,
  which is the `[sandbox]` precedent. The new `royale-shield-parry` replay is the end-to-end proof.
* **The golden welcome gained `shield`.** It names mode `royale`, so its advertised set is royale's
  real one; leaving it at six kinds would have made it depict a welcome no royale session can
  produce. The fixture mask, the pinned frame bytes, both `welcome*-message.json` examples, and the
  one client expectation that reads them moved together. The whole-world `snapshot-message.json`
  golden is deliberately unchanged, mirroring what Step 14 did for `stun`.
* **`compose_guarded_pair` now reserves its consequence vector.** See the correction below.

## Execution corrections

**A GCC 13 `-O3` false positive, fixed rather than suppressed.** After the lethal predicates moved
into `guarded_pair_contact.cpp` the optimizer could inline them, see through `append_recipients`,
and report `-Werror=stringop-overflow` — "writing 1 byte into a region of size 0", against
`std::variant`'s discriminant inside `vector::_M_realloc_insert`. It failed only the release
benchmark build, which is why the four debug and sanitizer lanes were green while
`run-benchmarks-linux` reported `BENCHMARK.BUILD_FAILED`. Compiling the pre-Step-18 file at the same
flags confirmed the trigger was new. The fix is one `outcome.effects.reserve(3)` at the exact ceiling
the header already promises — at most two recipient facts, then one canonical contact fact — which
removes the reallocating path the warning is about. No warning is disabled, no arithmetic changed,
and every emitted fact and its order is the same; the benchmark hashes below are the proof.

**One `-Werror=range-loop-construct` fix** in a new test: a `const auto` loop variable over an array
of `std::pair<EntityId, EntityId>` became `const auto&`.

**Pre-existing whole-tree formatting drift, deliberately left for Step 24.** Running the pinned
`clang-format` over `main.cpp src tests benchmarks` reformats sixteen files this step does not
touch, all of them over-long comment lines in the Step 9 random-stream work. Those edits were
reverted as out of scope; every changed and new Step 18 path passes
`clang-format --dry-run --Werror`. **Step 24's `verify-linux release` runs that check over the whole
tree and will fail on those sixteen files** until they are reformatted, which is a finding for that
step's review rather than a Step 18 change.

## Benchmark comparison

All eight categorized cases and the delivery-backpressure workload passed. Compared field by field
against Step 17's committed evidence
([`2026-09-12-falling-race-return-baseline.json`](2026-09-12-falling-race-return-baseline.json)),
after normalizing JSON integer/float spelling, **exactly one retained field differs across every
case**: `royale_deployed_roster.historical_reference.schema_migration` gained
`;2026-09-12_required_abilities_shield_defaults`, which is the required provenance entry for the
new `[abilities]` section in the frozen deployment snapshot.

Everything else is identical, including:

* every `correctness` block of all eight cases, and the delivery-backpressure `correctness` block
  with its `delivered_tick_trace_hash`;
* every `deterministic_work` counter of the three pure-solver cases, which therefore still match
  both historical prototype artifacts;
* `royale_deployed_roster`'s `final_snapshot_hash` `fnv1a64:36ee7ca01d83ddb5`, its final player and
  entity counts, and its within-budget verdicts.

That last one is the load-bearing result. The royale benchmark runs the real mode registry, so it
now steps one extra `kPreKernel` system and routes every pair through `compose_guarded_pair` instead
of the built-in rows — and it commits the same world, bit for bit. That is exactly what ADR 0003's
Step 18 amendment claims: with no guard present the composition returns the selected base equation's
output unchanged, and its extra static-branch closing-speed guard is a re-check of a test the
solver's own impact admission has already passed.

Timing moved within advisory noise and is not compared. Native capacity and performance remain
uncertified.

## Boundaries this step did not cross

* No new kernel seam: no policy socket, no ninth mode declaration, no event root, no second pair
  equation, no `TickContext` member, no generic command-receipt channel.
* No charge field, key binding, screen button, sender, or visual treatment. Those are Steps 19
  through 21, and a test pins that only `SimulationApi.ts` may call `.send(`.
* No protocol version bump. `shield` lands under 3.0 with its schema, encoder, decoder, generated
  types, client validation, examples and `docs/protocol/v3.md` row in this commit.
* No per-request receipt for a refused pulse. Queue acceptance and a local send are not activation
  confirmation; the published shield windows are the positive proof.
* The four fail-closed guards in the shield encoder have no reachable specimen, because
  `Shield::activate` is the only construction path and already refuses each shape. They are written
  anyway, and a test pins that reason rather than pretending to exercise them.

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
[`2026-09-12-shield-composition-baseline.json`](2026-09-12-shield-composition-baseline.json).
Transient logs for this run are `/tmp/step18-final-all.log` and are not committed.
