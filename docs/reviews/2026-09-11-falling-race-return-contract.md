# Step 17: falling, chronological racing, and safe return

Implementation contract before source changes. Step 16 is committed as `932ffc6`.
This step uses the existing motion-trigger declaration; it adds no kernel callback or root math.

## Body and support ownership

`GroundAttachment::{kFloating,kGroundBound}` is a validated `PhysicsBody` value, with a named
wither and a floating generic default. All ordinary player seating and scenario construction
explicitly choose ground-bound. Existing crossing hazards and statics remain floating; a typed
interior dynamic hazard can choose ground-bound. This preserves the frozen crossing-construction
oracle without changing it to accommodate the migration. All withers preserve the capability;
motion responses may not change it. Publish required v3 `ground_attachment` with closed values
`floating` and `ground_bound`; legacy wire contracts remain unchanged.

Shared `SupportLossTrigger` implements `MotionTriggerPolicy` and delegates geometry unchanged to
`support_loss_motion_trigger`. An explicit phase policy distinguishes always-active Sandbox
free play from running-only hill/race/royale. Static and floating bodies never bind. A bound
Controllable receives EliminationEvent and termination; other bound dynamic bodies receive
DespawnEvent and termination. Existing mode-owned respawn/placement consumes these facts.

Sandbox gains validated `[sandbox] respawn_delay_seconds`, required by the strict loader, with
an initial default of 2 seconds matching hill/race (tuning assumption, not an owner-confirmed
number). It declares the same RespawnSystem and always-active support binding, including lobby.
Competitive support remains running-only, preserving race's previous phase boundary.

Execution correction: the preflight trusted a stale Sandbox header comment. FreePlayObjective
already auto-starts: initial lobby, tick one countdown, tick two running indefinitely. Preserve
that lifecycle; always-active support also covers its pre-running ticks.

## Safe seating

Replace `point_is_occupied` with one `seat_is_supported_and_unoccupied(point,bodies,player_radius,
terrain)` query in spawn_seating. It calls canonical `terrain_supports_disc` and compares each
actual effective body radius using written `sqrt(dx*dx + dy*dy)` and the existing contact
tolerance. Refresh marker eligibility after every successful seat against the updated store.
Checkpoint return and ordinary spawn (including hill/Sandbox return) use that same query.
Occupied supported points defer; impossible checkpoint discs are rejected at application startup,
where both configured player radius and map are available, not handled by an invented relocation
fallback. Preserve shared respawn's return chronology N + D + 1 and body-bound cleanup.

## Race facts and finish

Race declares shared support and an owned ordered-gate policy. Reuse `ordered_gate_motion_trigger`
with a cursor initialized from frozen RaceProgress; absent progress means zero. A narrow
RaceCheckpointEvent carries entity, resulting next-checkpoint index, and the certified MotionTime
itself. Gate response advances the cursor and terminates at finish; no endpoint re-detection.
CheckpointProgressSystem retains zero-progress initialization and applies emitted facts even when
a later support loss removed the racer, preserving earlier credit. Delete TrackBoundsSystem.

Move the existing CoursePublisherSystem to first PreKernel, before steering (do not duplicate it).
This publishes bound course facts before a directly seeded completed racer's first input admission;
the writer preserves standings and no kernel initialization seam is added.

Standings are ordered by certified time then EntityId; equal times alone share placement.
RaceStanding adds required `MotionTime finished_tick_offset` (normalized fraction in [0,1]); v3
publishes `finished_tick_offset` alongside finished_tick, without recomputing a root or converting
to wall time. UI finish ordering/presentation uses the offset. Finish clears movement intent;
the canonical shared input lock also recognizes completed race progress so later held input
cannot reactivate movement. This does not invent permanent ghosting or a new input generation.

## Verification and ownership

Root alone builds/tests/formats/generates/commits; one C++ build at a time, GCC then Clang
ASan/UBSan. All local Docker evidence is advisory. Workers do not change historical evidence.
Preserve original Step17 gates and add application/runtime/server/controller lanes, fixed corpus,
browser, and full benchmark diagnostics because strict configuration and complete v3 publication
cross those boundaries. Explain intentional chronology/wire changes; do not loosen unrelated
assertions. Cover cliff-before-finish ties, multigate progress, unequal/equal finishes, no later
contact after death/finish, pre-first-gate return, retained checkpoint, nearby simultaneous seats,
actual-radius occupancy, zero-delay return, Sandbox/hill return, royale elimination, and floating
versus bound hazards. Keep Step17 unchecked until all gates pass.

Authored checkpoint count is a geometry bound, not a guarantee that every trajectory fits one
tick's 2,048-event budget. Coincident large courses may fail with the existing named budget error;
prove transactional rollback and preserve the cap, without a partial finish or fallback.
