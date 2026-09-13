# Step 23 implementation contract

Parent checkpoint: `0bb089b`. Scope: production acceptance evidence for the existing
dynamic arenas and combat behavior. No gameplay, kernel, wire, tuning, or replay grammar change
is authorized by this test step. Existing assertions and historical fixtures remain intact.

## Evidence boundaries

Read-only testineer, replay/integration, and combat preflights preceded implementation.
The production browser consumes 20 Hz publications while the simulation commits at 400 Hz.
Retaining every received frame cannot recover unpublished intermediate ticks. Pair real-browser
commands, committed state, and canvas observations with tick-addressed production replay proofs.
Never infer activation from a sent pulse or exact event chronology from endpoint motion alone.

Browser synchronization observes published ticks, windows, bodies, revisions, and UI state.
Wall-clock timeouts bound a failed observation; sleeps cannot establish gameplay chronology.
Author broad ability windows only in new browser fixtures, explicitly as observation fixtures,
and bracket the observed contact entirely within the intended published window. Exact default
32/160-tick shield boundaries remain C++ assertions. This proves behavior, not latency or balance.

## Required witnesses

- Retain all thirteen existing browser cases. Their slow race return/finish, cursor direction,
  camera/DPR, room lifetime, reconnect, and profile-menu guarantees remain requirements.
- Two real clients share one running room. One holds unit cursor steering while the other applies
  movement tuning through the UI. Both publish the same values, revision, and effective tick;
  held acceleration adopts the new value without another nonzero activation. A third client in
  another room keeps its original movement state. A dirty peer draft is preserved and requires
  review before Apply. Existing real-session stale-revision and retired-protocol rejections remain
  the protocol failure witnesses. Never bypass blur cancellation to make the held-input case pass.
- A large-map random-roam hill publishes changing center/velocity and continuing retargets;
  dangerous terrain remains dangerous when a hill crosses it. A charge crossing a small hole
  loses its body and returns safely with cleared abilities/input. Keep the existing camera
  bodyless/return proof and observe the new terrain on the real canvas.
- A charge hits a body even where its free endpoint would lie beyond the entire contact interval;
  it cannot jump a hole whose far side lies before that endpoint. Resting authored spawns make
  the first charged quantum independent of network arrival time. At drag 2, radius 2, x=300 and
  target/hole x=310, default charge 7500 travels 18.65625 units in the first quantum. The hole's
  radius is 2; the body contact interval is x=306..314. An endpoint-only implementation misses both.
- Exact production replay ticks prove multiple ordered race gates, finish chronology, and that a
  fall prevents a later gate/finish/contact. Browser publication connects the course and outcome
  to a real session; it does not certify an unobserved contact fraction.
- Perfect, ordinary, and late protected contacts are distinct. Late means the latter part of the
  active ordinary window, not expired protection. Pin the final protected and first expired ticks
  in C++. A stopped equal-mass attacker and a stationary shielded defender make the quarter-impulse
  target directly observable at drag zero without the non-closing correction changing it. Do not
  demand that ratio from the existing mass-200 hazard: separation legitimately overrides it.
- A real incoming bounceable hazard receives published stun and loses momentum after a perfect
  shield; its normal lifetime continues. A player holding Space through parry stays idle after
  stun expiry until a fresh activation, with the new input generation on subsequent commands.
- A real tactical controller at deployment drag 2.0 commits combat effects from published
  observations. Existing drag-40 seeker/coaster fixtures keep their original isolated purpose;
  their command-free combat settings cannot certify prediction at deployment drag. Resolve
  profiles by authored seat declarations and bodies by published identity, never allocation order.

## Observation and ownership

Extend the existing `browserFlowSupport.ts` recorder with stroked arcs (center, radius, angles,
style, width, order) so actual shield/stun drawing is visible. Retain the existing filled-arc and
polyline semantics. Shared new session observation helpers decode the real recorded transport;
they neither replace WebSocket nor inject commands or inspect React internals.

Root owns the shared browser support, movement-tuning browser case, all CMake files, plan,
ADRs, review/evidence docs, formatting, generation, builds, tests, and local commits. Workers
receive explicit disjoint new test/fixture ownership, preserve others' edits, and run no
verification or git mutation. Replays use the existing strict grammar and default ability
configuration; fresh post-stun input and tuning remain real-session/browser observations.

## Verification

Run the unchanged Step 23 gates, one C++ build at a time:

```
./scripts/verify-focused 'integration|fixtures'
./scripts/verify-focused 'integration|fixtures' linux-clang-asan-ubsan
./scripts/run-linux-toolchain -- ./scripts/verify-web
./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e
git diff --check
```

No retries, skips, flakes, private production seams, or assertion relaxation. Re-run affected
gates if formatting or a later edit changes their inputs. Record each failure and its concrete
repair honestly. All results on this Mac are Docker Linux/amd64 advisory. Step 24 remains
unchecked until its native release requirement is actually met. No push or deployment.

## Discovered input migration

The first full GCC selection passed all five new replay tests but failed the existing backpressure
fixture at startup: its 512 physical bodies exceed the accepted 256-body live solver bound.
This is an omitted Step 16 input migration; subsequent focused selections did not run this process
fixture. Migrate its population and exact expected snapshot count to 256 while retaining every
transport oracle: 60 Hz, 1,024-byte unread receive buffer, twelve-second observation, actual 1013
slow-consumer close, healthy-peer sequence/tick progression, readiness, and clean shutdown.
The migrated fixture must actually produce that close within the original bound; fewer bodies
alone are not a pass. No solver ceiling, socket behavior, or deadline changes.

The new terrain cases use Playwright fixture groups. The browser report checker previously
collected only file-level tests, so it would omit those nested results and reject the mismatch
against the total. Traverse the suite tree recursively and retain every original count, status,
retry, skip, and flake rejection. This expands report discovery, not test acceptance.
