# Step 24 independent release review

Reviewed during Step 23 verification, against parent `0bb089b` and the new acceptance changes.
The proofreader inspected source and contracts; it ran no native gates. Root owns verification.
Step 24 is **not complete**. Native release evidence remains outstanding. The braking dynamics pass both advisory C++ lanes;
Step 23a is verified at this local checkpoint.

## Resolved contract choice: arrival braking overshoots

The shared arrival brake can reverse velocity while its contract says it cannot overshoot.
This is separate from Keeper's already-deferred stable interior.

`src/controllers/tactical_controller.cpp` estimates the hold as the larger of reaction delay and
last observation spacing, then emits constant opposite thrust. The reaction gate retains that
intent until the next eligible observation. `shared/thrust_steering_system.cpp` reapplies it each
tick; `simulation/physics.cpp` adds acceleration and then applies drag.

Two valid configurations disprove the claim:

| Inputs | Held acceleration | Velocity after 40 ticks |
|---|---:|---:|
| Stationary hill; arrived vx=10; acceleration=400; ceiling=600; brake fraction=1; reaction=40; observation spacing=20; drag=2 | -100 wu/s² | -0.8553727688952049 wu/s |
| Same values; reaction=21; observation spacing=20; drag=0 | -190.47619047619048 wu/s² | -9.04761904761905 wu/s |

The first recurrence is `v[n+1] = (v[n] - 0.25) * 0.995`, with v[0]=10. The second holds
the 21-tick estimate until the next eligible observation at tick 40. The initial review evaluated these written
binary64 recurrences as source-grounded counterexamples. The Step 23a production-loop tests now
confirm both literal outcomes on GCC and ASan/UBSan.
The pre-existing arrival-brake test checked emitted thrust, not the resulting trajectory between actual
decisions. Its title/comments now describe only command shape; the separate dynamics tests own the
held trajectory.

The current published-observation and held-thrust boundary cannot guarantee universal no reversal.
Drag accepts any finite nonnegative value: sufficiently strong drag removes the original velocity
before a held opposite acceleration ends. Publishing drag alone would still leave observation
cadence, delivery delay, and live tuning changes to resolve.

The owner chose **keep current controls; define and test acceptable overshoot**. The
[accepted contract](2026-09-12-arrival-brake-contract.md) replaces the universal claim with a
conditional component-velocity bound under established regular cadence, immediate next-tick
application, stationary target, fixed tuning, and inactive propulsion limiting. A first zero-delay
decision is outside that envelope and can amplify velocity; irregular scheduling, moving hills,
and live tuning are also excluded. The seven production-loop cases passed both advisory C++ lanes. The final browser gate passed all 24 scenarios without retries, skips, or flakes.

No unpublished drag read, command/API change, or authoritative braking mechanism is introduced.
The historical Step 22c guarantee remains visible as a superseded claim. Step 23 acceptance does
not certify this Step 24 correction.

## Other findings and inspected boundaries

- The shipped configuration's Keeper comment promises a literal stable interior, and Bully's
  comment calls its ray a recovery path. ADR 0008's final Step 22c section explicitly defers both.
  The authoring comments now describe full-radius hill arrival and an authored forward-clearance
  ray, with both mechanisms explicitly deferred. Values are unchanged.
- Root's pinned tree-wide formatter check confirmed the carried sixteen-file Step 9 drift.
  It affects random-stream source/fixtures, random-count protocol source/tests, race observation
  fixtures, and `gameplay/race/race_mode_state.hpp`. Root applied the pinned formatter to those
  sixteen files. Independent diff review found whitespace/comment wrapping and one include reorder,
  with no behavior change. The pinned whole-tree check passed all 716 C++ files; this advisory
  result does not certify the native matrix.
- Shared motion event ordering prioritizes support loss and excludes terminated bodies from later
  events. Contacts and triggers use the shared roots/time mapping. This review did not re-prove
  exact-sign arithmetic.
- Guarded contact and support loss terminate motion at the owning response; abilities/status are
  shared, and body-bound kinds use the registry sweep. Race publishes a road identity and binds its
  corridor from terrain; no former road mirror was found in the inspected paths.
- Session commands receive server-owned identities and the room's sink. Tuning checks seating and
  entry revision and consumes the shared authored movement value.
- Browser JSON.parse precedes parsed-value validation. The token `1.0000000000000001` loses lexical
  information before integer validation. Exact C++ emission does not prove lossless browser token
  validation. Preserve that documented limitation; no parser migration is approved here.
- Step 23's new deployed-drag combat fixtures exercise charge and shield prediction with committed
  outcomes. Their arrival braking is disabled; they cannot prove Keeper's braking law.

Relevant source: `src/controllers/tactical_controller.cpp`, `tactical_controller.hpp`,
`tests/unit/controllers/tactical_controller_tests.cpp`, `src/simulation/physics.cpp`,
`motion_event_order.hpp`, `continuous_motion.hpp`, `src/gameplay/shared/guarded_pair_contact.cpp`,
`support_loss_trigger.cpp`, `respawn_system.cpp`, `src/server/session_websocket_session.cpp`,
`src/simulation/game_simulation.cpp`, and `docs/architecture/0008-dynamic-arenas-and-combat.md`.

## Step 23a production-loop supplement

`tests/unit/controllers/tactical_arrival_brake_dynamics_tests.cpp` adds seven cases. One fixture
feeds actual committed snapshots to `TacticalController`, applies returned commands on the next
tick through `InputBatch`, and retains production `ThrustSteeringSystem` plus `GameSimulation`
physics. Its stationary hill and absent interactions isolate the accepted envelope; it does not
model runtime scheduling or claim full-match stability.

The known outcomes distinguish R40 from R21 despite their shared forty-tick actual hold. Every
qualified physics tick checks each component against its decision-start magnitude, with 1e-10
wu/s tolerance, and checks retained acceleration. The repeated matrix uses R0/R21/R40, drag 0/2/40,
and fractions 1/0.5, requiring speed <=0.01 wu/s after 80 blocks (at most 8 simulated seconds).
That is 0.1% of this fixture's radius per second, not finite-time exact rest. The separate first
R0 fixture positively retains the 0.25 to -4.75 amplification. Diagonal saturation exercises shared
normalization; zero fraction/acceleration cases retain ordinary coast/drag.

Independent testineer review verified timing, production owners, numeric oracles, every-tick bounds,
and settling observations. It found a vacuous coast assertion: an empty decision list could pass.
Root added mandatory decisions at ticks 40 and 80 before inspecting their zero commands. No gate had
run on the weaker draft. Source-comment review found no production behavior or value changes.

The first final browser run passed 23/24 and exposed the same valid running-before-RaceProgress
publication in the race-fall case that Step 23 had repaired only in the finish case. Both now use
one shared wait for actual published initialization during running, then retain their exact
zero-gate assertions. No absent-value fallback, arbitrary sleep, deadline change, retry, or skip
was added. This is a test observation repair; production inputs remain byte-identical to both
passing C++ lanes. Independent review confirmed the barrier against
`CheckpointProgressSystem::apply`. Full web and browser gates passed on the repaired frontend input.

Both full C++ lanes passed 1,896/1,896; full web passed 986/986, the fixed corpus passed 99 inputs
across seven harnesses, and pinned whole-tree formatting passed 716 files. Final Chromium passed
24/24 without retries, skips, or flakes. `git diff --check` passed. The [evidence artifact](2026-09-12-arrival-brake-baseline.json)
records exact input and log hashes, the failed browser observation, and the verified repair. All
24 implementation inputs remained unchanged after their final applicable gates. Step 23a is complete;
Step 24 remains unchecked because native release/benchmark evidence is absent.

## Remaining release evidence

Run the remaining gates on native Linux/amd64:

```
./scripts/verify-linux release
./scripts/run-benchmarks-linux
git diff --check
```

The release chain includes GCC debug/release, ASan/UBSan, TSan, process smoke, full web/browser,
bounded fuzzing, and host-orchestrated dependency evidence. Record source hashes and host/toolchain
provenance. Mac-hosted Docker is advisory and cannot certify this chain. No push or deployment.
