# Step 24 independent release review

Reviewed during Step 23 verification, against parent `0bb089b` and the new acceptance changes.
The proofreader inspected source and contracts; it ran no native gates. Root owns verification.
Step 24 is **not complete**. Native release evidence and the production-loop braking verification below are outstanding.

## Blocking contract finding: arrival braking overshoots

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
the 21-tick estimate until the next eligible observation at tick 40. Root evaluated these written
binary64 recurrences; this is a source-grounded counterexample, not a production-loop test run.
The existing arrival-brake test checks emitted thrust, not the resulting trajectory between actual
decisions. It does not prove its no-overshoot comment.

The current published-observation and held-thrust boundary cannot guarantee universal no reversal.
Drag accepts any finite nonnegative value: sufficiently strong drag removes the original velocity
before a held opposite acceleration ends. Publishing drag alone would still leave observation
cadence, delivery delay, and live tuning changes to resolve.

The owner chose **keep current controls; define and test acceptable overshoot**. The
[accepted contract](2026-09-12-arrival-brake-contract.md) replaces the universal claim with a
conditional component-velocity bound under established regular cadence, immediate next-tick
application, stationary target, fixed tuning, and inactive propulsion limiting. A first zero-delay
decision is outside that envelope and can amplify velocity; irregular scheduling, moving hills,
and live tuning are also excluded. Production-loop tests and final gates remain pending.

No unpublished drag read, command/API change, or authoritative braking mechanism is introduced.
The historical Step 22c guarantee remains visible as a superseded claim. Step 23 acceptance does
not certify this Step 24 correction.

## Other findings and inspected boundaries

- The shipped configuration's Keeper comment promises a literal stable interior, and Bully's
  comment calls its ray a recovery path. ADR 0008's final Step 22c section explicitly defers both.
  Correct the authoring comments without adding those mechanisms.
- Root's pinned tree-wide formatter check confirmed the carried sixteen-file Step 9 drift.
  It affects random-stream source/fixtures, random-count protocol source/tests, race observation
  fixtures, and `gameplay/race/race_mode_state.hpp`. It must be resolved before the native build
  gate; a formatting check on this host does not certify the native matrix.
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

## Remaining release evidence

After the findings are resolved, run on native Linux/amd64:

```
./scripts/verify-linux release
./scripts/run-benchmarks-linux
git diff --check
```

The release chain includes GCC debug/release, ASan/UBSan, TSan, process smoke, full web/browser,
bounded fuzzing, and host-orchestrated dependency evidence. Record source hashes and host/toolchain
provenance. Mac-hosted Docker is advisory and cannot certify this chain. No push or deployment.
