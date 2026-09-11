# Movement tuning boundary preflight, 2026-09-11

Status: read-only architectural preflight plus root threat-map synthesis, not implementation or
verification. Step 10 needs an explicit plan/ADR amendment before source work; none of this belongs
in Step 9's random-stream commit. The `movement_preflight` explorer and `tuning_outcome_design`
rigorous-architect supplied independent source review. Security-as-design guided the threat map.

The executor has requested owner review of commit-confirmed, correlated feedback with interrupted
requests reported as unknown. No answer or adoption is recorded here. Step 9 completed separately
as `e4fad4b`; Step 10 remains unimplemented and unchecked.

## Confirmed scope and threat map

ADR 0008 accepts cooperative seated authority in lobby/countdown/running/ended, atomic room-wide
changes, Apply/Reset, and authoritative feedback. It does not define request correlation, exact
rate, result ownership, or conflict acknowledgments. The mechanisms below are proposed defaults.

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

## Proposed decision and delivery contract

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

Before adoption, amend Step 10, ADR 0002/0008, and v3's client-sequence restriction narrowly for
correlation. Verify canonical contention/staleness, mailbox eviction, skipped/later snapshots,
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
