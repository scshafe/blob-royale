# Movement tuning boundary preflight, 2026-09-11

Status: adopted Step 10 execution contract, not implementation-completion or verification evidence.
The owner approved commit-confirmed feedback on 2026-09-11; the plan and dated ADR amendments now
name the ownership and result changes before source cutover. The `movement_preflight` explorer and
`tuning_outcome_design` rigorous-architect supplied the original independent source review.
Security-as-design guided the threat map. This remains separate from Step 9's random-stream commit.

The owner selected **Proceed with commit-confirmed feedback**, including correlated results and
interrupted requests reported as unknown. Step 9 completed separately as `e4fad4b`. Step 10 starts
with unused movement/arithmetic and wire-result values; old live readers remain until both exact
pre-delegation gates pass. Its checkbox remains unchecked.

## Confirmed scope and threat map

ADR 0008 accepts cooperative seated authority in lobby/countdown/running/ended, atomic room-wide
changes, Apply/Reset, and authoritative feedback. Its original text did not define correlation,
exact rate, result ownership, or conflict acknowledgments. The 2026-09-11 amendment adopts the
mechanisms below; exact parameter/rate values are engineering defaults, not user-selected numbers.

| Protected asset | Actor/action/consequence | Proposed enforcement owner |
|---|---|---|
| Another room's or player's authority | Browser forges routing/actor fields to change unrelated settings | Existing room-bound command sink and server-issued controller; no browser room/controller payload; tick checks seated membership. |
| Coherent shared movement | Concurrent/stale requests overwrite half a pair or obtain order from client IDs | Simulation-owned validated tuning value; existing closed phase-0 handler; freeze entry revision and choose the last eligible canonical contender; one atomic pair/revision commit. |
| Bounded server resources | Flooded or pipelined settings accumulate commands/results | Preserve pre-parse resource limiter and mailbox priority; tuning-specific rate; one unresolved runtime exchange plus one session-owned result in flight. |
| Truthful completion feedback | Queue admission, skipped snapshots, or callback order is mistaken for application | Decisions released only after successful tick commit; correlate results; snapshot tick covers decision; transfer result ownership before writing. |
| Private steering intent | New persisted input leaks through public snapshots | Existing component publication owner strips intent alongside live command lists; settings remain public room data. |

Accepted limits: a seated cooperative player may choose any legal tuning, even if others dislike
it. This does not add host/admin roles, moderation, or anti-cheat machinery. Reconnection does not
resume an old exchange; an unobserved result is unknown, not proof of rejection or rollback.
Cross-reconnect acknowledgment recovery would be a new product decision. Deployment/TLS and
existing global resource policy remain unchanged. Reset provides the existing recovery path for
undesired legal settings; shared-state and resource integrity remain enforced.

## Source-confirmed ownership corrections

`MatchState`, commands, and batch validation belong to simulation, so the validated tuning value
cannot live in gameplay. Locomotion remains in `shared/ThrustSteeringSystem`. Controller-addressed
mutations are already consumed in `game_simulation.cpp` phase 0; `TickContext` has no batch and
must not gain one. Extending that closed visitor is not a new policy socket. A narrow committed
tuning-decision result from `step` would nevertheless be an explicit Step 10 kernel-contract
amendment, not an incidental feature edit.

Persist normalized intent once, preserving the original `(component * scale) * maximum` order.
An absent intent must remain distinct from explicit zero: seeded scenarios can carry authored
acceleration before any command. The normal-speed ceiling needs finite-step arithmetic; removing
positive velocity/acceleration dot product alone still permits perpendicular Euler speed gain.
Preserve the exact uncapped branch and existing phase admission. The shared `seat_body_at_rest`
owner must clear previous-body intent without dropping newly received commands; a bodyless
pre-kernel check alone misses zero-delay replacement. Round reset already destroys participants.

## Adopted decision and delivery contract

The tuning payload adds correlation-only `tuning_request_id` and `expected_revision`, plus both
absolute scalars. IDs never order commands. At tick N, every candidate compares against the same
entry revision R. The last eligible canonical contender wins; matching-R losers are superseded,
other revisions are stale. Commit once to R+1/effective N, even for identical values; reject revision
exhaustion without wrap. No winner means no value/revision/effective-tick change. Apply precedes
Start's existing lifecycle evaluation. Same-batch joins do not retroactively grant authority and
later leave does not undo an admitted decision.

Mailbox admission is not completion (`command_submission_result.hpp` and
`session_websocket_session.cpp`). Accepted tuning evicted by existing mailbox priority needs an
explicit terminal refusal; it must not disappear or be reclassified as a lifecycle command.
Use a tuning-specific runtime exchange owner following `CommandSink` session lifetime, not the
presentation-only controller directory or a world-event bus. Proposed initial rate is one new
exchange per 500 ms, separate from the unchanged global pre-parse limiter.

Deliver a closed session-specific result beside the existing snapshot, outside authoritative
match state. A room-bound, narrowly consuming result-delivery capability atomically claims an
already terminal result only when the chosen snapshot covers its decision tick. Claim transfers
the immutable result into the session's active-write record and reopens the runtime slot **before**
encoding/`async_write`. The write callback releases only that local record, never the runtime slot.
Thus a client receiving A may submit B before A's server write callback without false pipelining
rejection or accidental deletion of B. If B finishes while A is in flight, B remains in the one
runtime slot until a later write claims it. No new frame scheduler or generic subscriber system.
Encoding/write failure closes the session and discards its claimed result; it must not reinsert A
into a slot that may already contain B. This is ownership transfer, not proof of peer receipt.

Step 10 and ADRs 0002/0003/0004/0005/0007/0008 now carry dated amendments; v3's client-sequence
restriction changes narrowly for correlation with its implementing wire cutover. Verify canonical
contention/staleness, mailbox eviction, skipped/later snapshots,
body replacement/departure, interruption/unknown results, bounded session churn, and the immediate
next-request/late-write-callback race. Source anchors: `command_mailbox.cpp`,
`simulation_runtime.cpp` drain/step/publication loop, `snapshot_delivery_state.hpp`, and
`session_websocket_session.cpp` read/active-write/completion owners. No automated evidence is
claimed by this preflight.

## Implementation map retained for the next step

- Extend the existing closed controller visitor in `game_simulation.cpp` (near lines 289/332),
  with the committing tick from its current call site (near 795/798). Do not give `TickContext`
  the input batch. Allocate bounded tuning decisions before commit and return them only after
  successful commit. Explicitly amend the plan's kernel-seam allowlist for this Step 10 change.
- Batch order is kind, controller identity, then submission. Rank 3 is already occupied: insert
  tuning after thrust and before Start while preserving every existing command's relative order.
  Client request IDs are correlation only and cannot select a winner.
- Append optional private intent after existing `Controllable` fields, preserve aggregate callers,
  and strip it at the existing publication owner. Amend the old two-fields-only comment and
  snapshot privacy tests. Do not install a new Running-only thrust gate.
- `spawn_seating.cpp`'s shared `seat_body_at_rest` resets old-body intent for both normal spawning
  and race returns, retaining commands recorded this tick. A bodyless pre-pass misses zero-delay
  body replacement. Round reset destroys participants but must retain the match tuning.
- `match_startup_validation` owns cross-configuration/map checks; the canonical simulation value
  owns intrinsic bounds. Seed the world match beside seats in `blob_royale_application.cpp`
  (near line 448), after its existing validation call (near 409).
- Include Sandbox's hard-coded scalar/factory bypass, direct gameplay test factories, and both
  parsing and initial-world seeding in `tests/fixtures/replay_fixture.cpp` (near 469–510), beyond
  the authoring paths already enumerated in Step 10. Fixture ceilings must remain unreachable.
- Introduce the normalized-intent arithmetic as an unused candidate with a frozen old reference
  and two-lane proof before delegation. Test the exact uncapped branch and a finite-step cap;
  never clamp externally imparted velocity or move integration/drag out of their existing owner.

Line anchors describe the pre-Step-10 source, not permanent line-number contracts. The proposed
500 ms tuning-specific interval is an engineering default, not a user-selected exact rate.

## Settled implementation boundaries

`MovementTuning` uses `.acceleration()`/`.normal_top_speed()` and a validating `create` factory.
Intrinsic bounds are acceleration 0..10,000 and speed 1..10,000, with defaults 400/600; constants
live in simulation limits. Explicit fixture ceilings of 10,000 must prove inactive under unchanged
complete replay bits. The standalone old arithmetic helper's wider domain stays in its frozen proof.
`MovementTuningState` contains current/default pairs, uint64 revision, and `TickSequence` effective
tick. `MatchState::movement` owns it. The narrow `MovementTuningDecisions` return from `step` owns
a bounded vector; span views are lvalue-only, moves are noexcept, and existing callers may ignore
the result. Committed decision fields are controller, request ID, status, decision tick, revision.

Exchange storage lives inside `CommandMailbox` under its existing mutex, with no second exchange
lock. Directory registration/rollback and mailbox registration occur without holding both locks.
Close retires the exchange and enqueues Leave atomically before closing presentation registration.
Completion for a retired controller is discarded as unknown. A well-formed new ID advances the
per-controller high-water; non-increasing IDs close 1008 `tuning_request_id_reused`, and a second
unresolved request closes 1008 `tuning_request_in_flight`. Neither replaces an existing result.
An eligible attempt starts the 500 ms deadline even when mailbox capacity refuses it; a rate
refusal does not extend that deadline and reports ceil remaining milliseconds in 1..500.

Committed statuses are `applied`, `superseded`, `stale_revision`, `not_seated`, and
`revision_exhausted`. Admission statuses are `rate_limited`, `mailbox_full`, and `mailbox_evicted`.
Runtime owns that decision/refusal variant; a server adapter constructs the protocol-owned
validated wire value. This preserves the protocol dependency boundary.

Wire order is `match.movement` after `start_requested`, then the existing outcome/placements/mode
state; movement fields are current, defaults, limits, revision, effective_tick. Pair keys use full
unit names in acceleration/speed order; limits use the same keys with minimum/maximum values.
Welcome appends `movement_tuning_minimum_interval_milliseconds` after terrain. Snapshot data
appends required nullable `tuning_result` after match. Its fields are tuning_request_id, status,
decision_tick, revision, retry_after_milliseconds. Admission tick/revision are null; retry is
nonnull only for rate limiting. Server claims after egress admission/begin-active-write and before
encoding, retains the value with the payload, and releases only that local value in callbacks.

The frontend retains one `sendCommand` transport, with caller-provided monotonically increasing
tuning IDs and API-owned pending/result validation. State is idle/pending/resolved/unknown;
unknown is client-only. Reserve pending before socket.send so synchronous test delivery is safe;
send exceptions are unknown, not evidence of rejection. Validate correlation, duplicates, and
decision-tick coverage before frame sequence acceptance and callbacks. Step 11 owns controls and
ID allocation; body replacement never resets the counter. Reconnect never replays old requests.

## Numerical review resolution before proof

The independent rigorous-architect review confirmed the unchanged integrator and drag boundary,
but rejected interpreting rounded projection identity as a certificate of pure-outward input.
At canonical delta, `v=(1,1)`, normal speed `1`, and
`a=(400,nextafter(400,+infinity))` produce computed requested endpoint `(2,2)` and projected
endpoint `(1,1)`, despite noncollinear acceleration. Adopt first-computed-projection identity
explicitly as zero and retain this regression. It can erase a tiny turn by rounding.

All speed/non-amplification inequalities use computed binary64 squared norms. Requested
integration still raises its existing physical-domain errors. Uncapped acceleration is returned
verbatim. For a nonidentity first projection, each original-endpoint radial attempt uses the
initial factor plus at most eight corrections toward zero; each permits its initial
non-amplifying acceleration scale plus at most eight corrections. A later componentwise zero
acceleration or canonical integrated endpoint equal to current velocity fails immediately,
without advancing into artificial braking. Exhaustion raises the explicit locomotion precision
error. Zero must not be detected through a squared magnitude that can underflow. This is a
bounded witness search, not an exact-real certificate, correctly rounded projection, or proof
that every admitted input has a discoverable witness. No automated evidence is claimed here.

## Prerequisite execution in progress

The initial exact GCC `unit.simulation|unit.gameplay|fixtures` gate passed **866/866** on
Mac/arm64 hosting Docker Linux/amd64 (advisory only). Old steering readers remain unchanged.
Log: `/tmp/blob-royale-movement-tuning.PDrGmr/preproof-gcc.log`. This is not the two-lane release.
Source review then derived an ordinary-scale admitted later-identity failure witness, still
awaiting executed confirmation. Add its explicit guard regression and sustained-default
steering success tests before repeating GCC and running Clang; do not reclassify valid input
as mathematically invalid or change a failed ordinary-trajectory success expectation to throw.
Both direct protocol-result supplemental gates and all live/final gates remain pending.

The strengthened exact GCC prerequisite passed **869/869**, including 26 held-input trajectories
(axes, diagonals, unit/analog oblique input, initial ceiling, external overspeed; each with drag
0 and 2) and two scheduled turn/reverse/lowered-held-ceiling trajectories. Each tick requires
successful cap/integration/drag and strict computed speed/non-amplification predicates. The
earlier 866-test run remains separate. Log: `preproof-gcc-trajectories.log` in the same directory.
Clang is running, not yet passed; no live reader is released.

The derived hexadecimal witness also passed all exact intermediate-value checks and reproduced
`GAMEPLAY.LOCOMOTION_PRECISION_LOST` at a later radial identity. Its valid velocity components
are `0x1.8000000000003p+0`, acceleration components `0x1.2c00000000003p+9`, and normal ceiling 1.
This limitation occurs at ordinary magnitudes, not only physical-domain extremes. A prospective
alternative would accept identity of any enumerated radial target before materialization while
retaining nonidentity-target materialization/exhaustion errors. It was reviewed but is **not
adopted**; the sustained-success matrix found no such failure and the written first-target-only
policy remains unchanged. Correction-budget exhaustion is a distinct, currently unwitnessed
branch. The successful matrix is not a totality proof for every legal input.

## Cutover release

Both strengthened exact prerequisites passed **869/869**, GCC and Clang ASan/UBSan; both
supplemental protocol-result filters passed **14/14**. All four are advisory emulated evidence,
with logs under `/tmp/blob-royale-movement-tuning.PDrGmr`. The original live readers were still
unchanged at these gates. This releases implementation, not completion or Step 5 acceptance.
Sandbox keeps its existing accepted command mask because it has no seats; its normal locomotion
still reads shared authored tuning, with no special live-tuning authority. The final required
full filters and web/fixed-fuzz gates remain pending.

## Cutover source review

The active historical benchmark fixture was missing from the original authoring inventory.
Migrate its acceleration 400 to shared movement with fixture ceiling 10000 and seed benchmark
world current/defaults explicitly. Its INI, README, and JSON retain derived-schema provenance;
a production-loader fixture test guards admission. This does not establish a new timing baseline.
The independent frontend review found that effect cleanup could discard the pending-to-unknown
callback and carry room A's exchange state into room B. The backend/client owner is correcting
room-change reset separately from same-room interrupted unknown, with hook-level regressions.
These findings are not closed by source changes alone; final execution remains pending.

Source-review closure: gameplay held-input/seating/reset behavior and the runtime bounded
exchange path match the adopted contract. Independent client review closed prior-room state
leakage, teardown callback reentrancy, and nested synchronous result-publication ordering.
The process isolation test now checks every buffered frame, not just the fresh-tick endpoint.
The single-frame oracle independently checks effective-tick, decision-tick, and revision coverage;
its nine added cases mutate encoded bytes rather than reusing encoder input rejection. The fixed
command-fuzzer retains exhaustive accepted-command checks and gains tuning invariants plus four
request seeds. No artificially delayed live socket callback test is claimed: A/B ownership is
covered by deterministic mailbox claim/local-record lifetime tests and reviewed session callbacks.

Both supplemental lower-layer target builds and `unit.simulation|unit.gameplay|unit.runtime`
filters passed **926/926** on advisory emulated GCC and Clang ASan/UBSan. Early missing-include,
retired Sandbox constructor, and two command-inventory corrections changed no accepted motion
expectation. Full final gates are still pending; source is frozen and generation/formatting passed.

## Final verification and closure

Step 10's exact GCC and Clang ASan/UBSan filters each passed **1377/1377**. Full web passed
**505/505**, zero skips/todos, strict typecheck/lint, formatting, generated drift, **75 schemas and
21 examples**, and the production build. Fixed-corpus replay passed **65 executions across seven
harnesses**; this is not a fuzz campaign. Pinned C++ formatting and `git diff --check` passed.
Logs: `final-gcc-range-fixed.log`, `final-clang.log`, `final-web-lint-fixed.log`, and `final-fuzz.log`
under `/tmp/blob-royale-movement-tuning.PDrGmr`. Earlier test-construction/type/lint corrections
are retained in the plan as correction history, not substituted for these full successful gates.

All evidence is advisory Mac/arm64-hosted Linux/amd64 emulation. Public v1 schema/generated files
and accepted motion expectations remain unchanged. No native capacity, performance, deployment,
human-playtest, or complete-input representability claim follows. The ordinary-scale rejected
locomotion witness remains an explicit limitation. Step 10 is closed; UI controls and their
dirty-draft policy belong to Step 11, and Step 5 remains a separate unapproved human gate.
