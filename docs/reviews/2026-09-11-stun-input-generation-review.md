# Step 14: stun windows and input invalidation

Status: implemented contract; scoped source reviews and executed verification are recorded below.
The owner selected cancellation across missed stun snapshots on 2026-09-11. Step 5 remains
unapproved; this foundation does not implement parry, shield, charge, or the live motion solver.

## Decision and scope

Persist optional `input_generation` on `Controllable`, and echo the exact optional value in
`ThrustCommand` / `set_thrust`. Absence permanently means never input-invalidated, not a
compatibility fallback, zero, or wildcard. An applicable positive stun request sets generation
to its positive committing tick. Requests on the same tick share a token; later requests
invalidate input again even if they do not extend an existing longer stun. No counter is added.

Generation survives expiry and same-entity respawn. `ComponentPublication<Controllable>` must
retain it while stripping intent and recorded commands. Only entity destruction ends its
lifetime. Existing unstunned fixtures remain absent-generation and bit-identical; their encoder
must omit the new member. Old golden examples stay unchanged; new examples exercise the field.

This guarantee does not cover invisible entity destruction/replacement: network ingress stamps
the current entity when a frame arrives, so a delayed omitted-generation command can match a
new, never-invalidated entity. The Step 11a observational body/restart limitation remains.

## Authoritative behavior

The shared tick-window value belongs in simulation, below gameplay components. For activation
`t` and duration `d`, its interval is `[t,t+d)`. Check `d <= maximum_tick - t` before adding.
`TickSequence` already guarantees exact integers through `2^53-1`; expiry at that limit is valid.
Zero duration is empty. Merge overlapping active stun by maximum expiry; replace expired state
instead of joining disconnected intervals. Duration one locks no following full steering tick:
application is PostKernel at `t`, and steering next runs at `t+1`, already the expiry.

One shared status system runs PostKernel in all four modes before lifecycle removal. Missing,
bodyless, and static request targets are explicitly ignored. Zero-duration requests are strict
no-ops: no component, generation, intent, or acceleration change. For eligible dynamic positive
requests, validate and aggregate every window before any store mutation. Reject invalid expiry
and a zero committing tick visibly, without clamping or partially applying a request set.

`Stun` contains the canonical window and declares the body-bound lifetime trait. Publish both
absolute endpoints to the browser and in-process readers, avoiding richer bot-only timing.
The status system removes expired status, clears self-propulsion, and updates generation. It
never writes velocity, including first application. Momentum cancellation is exclusively the
later impact response in Step 18; later bumps and ordinary hazard lifetime remain effective.

The first canonical gameplay input-lock predicate is added here: the supposed Step 10 predicate
does not exist. Steering checks active stun before reading commands or returning for absent
intent. Active stun writes explicit zero intent, not absence (which preserves authored
acceleration), and zero propulsion acceleration. Outside stun, a command is accepted only if
its optional generation exactly matches. This also governs zero-direction releases. A mismatch
does not replace existing valid intent or queue work for expiry. Existing last-submission-wins
coalescing remains; stale input may supersede valid pending input and then be ignored.

## Controllers and browser

Pinned C++ interfaces: `TickWindow::create(TickSequence activation_tick, uint64_t duration_ticks)`,
`activation_tick()`, `expiry_tick()`, `contains(tick)`, and `expired(tick)`. The validated factory
may represent empty or zero-origin mathematical windows; it exposes no generic interval-union
operation. Status merging reconstructs through that factory using the selected start/expiry.
`Stun{TickWindow window}` and `StunRequest{EntityId entity, uint64_t duration_ticks}` are the
component/event shapes. Append `optional<TickSequence> input_generation{}` to both Controllable
and ThrustCommand. Present zero is rejected at command intake; state generations originate only
from positive committing ticks. Event kind/name: `kStunRequest` / `stun_request`.

`gameplay::input_is_locked(const GameWorld&, EntityId, TickSequence) noexcept` reports active stun
only; it adds no phase, body-presence, or race-finish rule. `StatusSystem::create()` returns the
usual immutable system pointer and its name is `status`. Append it last in each existing
PostKernel list: sandbox total 1→2, royale 8→9, hill 8→9, race 10→11. Existing relative order and
all lifecycle lists stay unchanged; update exact registration-count assertions explicitly.
Simulation owns `SIMULATION.TICK_WINDOW_EXPIRY_OVERFLOW`; gameplay owns
`GAMEPLAY.STATUS_ACTIVATION_TICK_ZERO`. Controller's protected
`request_thrust(const Observation&, const Vector2&) const` returns zero or one Command, preserving
direction verbatim and adding no RNG, timer, normalization, or retry.

A fresh non-repeat Space press while unlocked captures the observed generation. All aim updates
and its release retain that activation token. A generation change cancels held and pending work
even with no observed Stun. Never read the newest token inside a flush or transport sender and
apply it to old work. Generation cancellation retires old zero releases too; it differs from
sender-only replacement. Keep same-body throttling and the existing post-send retired-lifetime
check. Authoritative snapshot ticks, not wall-clock timers, determine observed stun expiry.

The four active bots use one shared protected steering-authoring method beside `request_body`.
It reads only public observation, suppresses observed active stun, and echoes that decision's
generation. Do not stamp in `Controller::decide` or `ControllerHost`: scripted replay preserves
literal typed commands. Existing CSV replay fixtures remain unchanged; new typed scenarios can
name tokens explicitly. Server admission is shared; controller reaction timing need not match.

## Required amendments and evidence

Step 14 explicitly owns the command/value/registry and v3 additions above; it adds no kernel
policy socket or external mutable-world access. Record a narrow foundation exception to the
no-unproduced-event rule: `StunRequest` has an injected in-tick test producer in Step 14 and its
production producer arrives at Step 18. Do not manufacture a production stun command to fill it.

The original two-lane simulation/gameplay/protocol/runtime/fixture selections, fixed-corpus
regression, and complete web gate remain required. Add controller coverage on both lanes, since
four active authoring sites change. Prove window endpoints/overflow/merge, rollback, zero and
invalid-target no-ops, status cleanup/return/reset, retained external velocity/lifetime, exact
optional matching (including release), public/private projection, literal replay, and missed-
snapshot cancellation through pending timers and synchronous sender retirement. No accepted
fixture outcome or existing random draw changes are allowed.

Source anchors: `src/simulation/tick_sequence.hpp`, `components/controllable_component.hpp`,
`src/gameplay/shared/thrust_steering_system.cpp`, `src/protocol/command_decoding.cpp`,
`src/runtime/command_mailbox.cpp`, `src/controllers/scripted_replay_controller.cpp`,
`src/server/session_websocket_session.cpp`, and the client's canonical `useThrustInput.ts`.

## Scoped browser source review

The independent Step 14 browser pass found no blocking generation/lock issue. The new code
reserves same-body send timing before invoking the sender; an active refusal restores the old
timestamp, while synchronous generation retirement leaves the replacement lifetime's attempt
throttle intact. Its post-send retirement guard remains necessary for either sender result.

An inherited, nonblocking capability limitation remains: a synchronous sender-only replacement
during the first accepted nonzero send sees the previously sent zero direction and may omit a
cancellation zero. Production `sendCommand` is stable, and this predates Step 14. The source
review does not claim this unsupported replacement case is proven. If that capability becomes
dynamic, add an in-flight-attempt regression and preserve its original token. This note is
source-review evidence only; required execution gates remain separate.

## Executed verification (2026-09-11)

The frozen implementation passed the required gates on the Mac-hosted Linux/amd64 advisory
toolchain. This is not native performance or release evidence, and does not approve Step 5.

- Original simulation/gameplay/protocol/runtime/fixture selection: 1,197/1,197 on GCC Debug and
  1,197/1,197 on Clang ASan/UBSan.
- Controller supplements: 87/87 on each of those lanes.
- Fixed-corpus regression: all seven harnesses passed, with 10 application, 10 map, 3 CLI,
  3 HTTP, 24 command, 24 request-id, and 2 scenario inputs. This is saved-input replay, not an
  open-ended fuzzing campaign.
- Full web gate: 692/692 tests, 77 schemas, 25 examples, generated-drift checks, typechecking,
  and production build passed.
- Pinned C++ format dry-run and diff whitespace checks passed. All 93 non-Markdown changed
  source/schema/test paths remained hash-identical through final verification.

Exact commands remain in plan Step 14. Logs are in
`/tmp/blob-royale-stun-input.qGJOqv/{gcc,clang,controllers-gcc,controllers-clang,fuzz,web,cpp-format-check}.log`.
Independent core, browser, and integration source reviews found no blockers. Existing accepted
physics/replay expectations and diagnostic bot random behavior remain unchanged. The proof of
later collision motion and the proof of continuing hazard lifetime are separate scenarios;
neither claims a production parry producer, which remains Step 18.
