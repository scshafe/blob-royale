# Step 21: accessible ability controls

Implementation contract, written after a read-only preflight against `adb410d` (Step 20) and
adversarially reviewed before any source edit. **Web-only**: no schema, encoder, component or
server change. Its gate is `verify-web` alone.

The preflight rejected six of nine drafted decisions and found five blockers. Each correction is
marked **[corrected]**, because the wrong version is the one a reader would otherwise reach for.

## Bindings

**[corrected] Take the keys from the pool the owner already reserved, and do not use Shift.**

ADR 0008 records, of Step 11a, that "WASD/arrows no longer steer after Step 11a; those keys are
available for later charge/shield bindings", and Step 11a froze exactly that pool as
`REMOVED_DIRECTION_KEYS` and pinned its inertness in three tests. The draft reached past it for an
arbitrary letter. Take two keys from the pool instead, so the choice is the one already sanctioned
and every reserved-key test becomes a live binding test rather than a stale one:

* **shield → `KeyS`** — the home-row key under the middle finger of the hand whose thumb holds Space,
  which is the fastest reachable key on the board, and shield is the reactive move.
* **charge → `KeyD`** — adjacent, index finger, and the ADR's own mnemonic space (S for shield, D for
  dash).

The four arrow codes stay reserved and stay asserted inert.

**Shift is rejected, and not as a matter of taste.** The go-key guard deliberately filters
`altKey`, `ctrlKey` and `metaKey` and deliberately does *not* filter `shiftKey`, a choice pinned by
an existing test, so Shift+Space thrusts today. A bare-Shift shield would therefore fire on the
leading half of every Shift+Tab a keyboard user makes, five presses of it is the Windows Sticky Keys
gesture, it is two codes (`ShiftLeft`/`ShiftRight`) against a one-constant-per-action model, and
modifier keys do not auto-repeat — so the key-repeat test the ADR requires would pass vacuously
against it. Record all of that; ADR 0008's Shift proposal is superseded here and the ADR gets a
dated amendment saying so, because its Step 11a paragraph still says these keys are available
"without assigning or implementing abilities here" and this step assigns them.

Both keys match on `event.code`, like `THRUST_GO_KEY_CODE`, so they are layout-independent. Both
abilities also get ordinary on-screen buttons; those are the accessible path and are not optional.

## One input owner, but not one send path

**[corrected] Reuse `sendCommand`, not `flush`.** The draft said "the existing sender", which reads
as the thrust path's `flush`, and reusing that is wrong in five separate ways: its change-only gate
would silently swallow a second identical pulse; its 50 ms coalescing timer would delay a pulse
against an 80 ms perfect opening; its single parked timer can drop a pulse on any effect re-run; its
`currentTransmission.direction` bookkeeping is level state a pulse does not have; and its shared
refusal latch means one refused ability send would drop held thrust. The reusable thing is the
`sendCommand` reference and the guards around it, not the fifty-five lines of level semantics.

**[corrected] BLOCKER: add a rate discipline, because exhausting the server's bucket closes the
socket.** The 50 ms floor lives inside `flush`, not in the sender, so "go through the existing
sender" buys no rate protection at all. The per-session bucket is capacity 30, refill 20 per second,
the token is charged before parsing, and an empty bucket is `1008 command_rate_exceeded` — a
disconnect, not a refusal. Held thrust with a moving cursor already runs at the full 20/s refill
rate. Two unthrottled ability keys on top of that drain the burst in seconds and disconnect the
player mid-match, and they buy nothing doing it: the mailbox coalesces same-kind inputs, so multiple
same-tick pulses are at most one attempt.

So: **suppress an activation while the client can see a live published cooldown for that ability**,
which is the primary mitigation and is legitimate published state, and add a per-ability minimum
send interval as the backstop for the window between a press and the next snapshot. The authored
cooldowns are 0.9 s and 1.2 s, so an interval of a few hundred milliseconds costs a player nothing.

## The two payloads are not the same shape

**[corrected] BLOCKER: shield needs an explicit `null`, not an omitted member.** The thrust encoder
omits `input_generation` when the token is absent. `charge` matches that exactly and may reuse the
expression verbatim. `shield` must not: its schema requires the member and admits `null`, and the
client type says `input_generation: number | null`. Reusing the thrust expression produces `{}`,
which the closed envelope rejects, so `sendCommand` logs a refusal and returns false. **The failure
is silent and it targets exactly the players who have never been stunned** — that is, everyone, at
the moment they first press shield. Encode the two separately and test the never-invalidated case
for both.

## Aim

**[corrected] Remembered aim must survive a stun, and charge is mouse-dependent — say both.**

`lastNonzeroAimDirection` is a closure-local reset on every effect run, and the effect's dependencies
include `inputLocked` and `inputGeneration` — so **every stun wipes the remembered aim**. That is not
the contracted discard set, which is body loss, body replacement, a new welcome, and disconnect.
Preserve it across a same-body generation or lock change using the existing same-body condition.

Resolve the direction **inside the effect**, never from the exposed React state in a button's
`onClick`: the exposed value can lag the closure by a commit.

The ability keydown must not inherit the thrust path's `!hasObservation` guard, which is a
held-control rule and would refuse exactly the off-canvas case the last-nonzero fallback exists for.

**And state the limitation plainly: aim exists only for a mouse pointer inside the canvas, so charge
is mouse-dependent.** For a touch, pen or keyboard-only player, no aim ever exists and charge is
permanently unavailable with its explanation showing. That is an accessibility gap in a step whose
title is "accessible ability controls", it is not fixable inside this step's scope without inventing
a keyboard aim the plan does not authorize, and it goes to the owner as a named question rather than
being quietly shipped.

## One-shot semantics

**[corrected] A pulse still needs a held latch, and buttons are a second repeat source.**

"A one-shot has no release" is true of what it *sends* and false of what it must *observe*. The
thrust path rejects repeat with `event.repeat || goHeld`, and the second half — cleared by keyup — is
what makes it robust when a browser or a synthetic event omits `repeat`. Each ability needs the same
per-ability latch, cleared on keyup **and on blur**, because a blur swallows the keyup and the key
would otherwise wedge.

**A focused `<button>` fires click on Enter keydown, and held Enter repeats.** So the one-shot guard
must live in the shared activation path that both the key handler and the button handler call, and
the test for it must press and hold Enter on the button, not only repeat the key.

Two edits to the drafted list: "after cancellation" has no meaning for a pulse, since there is no
interval between press and send in which to cancel; and "after a body replacement" is already free,
because every local including the latch is re-declared when the effect rebuilds, and an existing
four-case test already pins body loss, replacement, new welcome and disconnect.

**Buttons must not steal the go key.** A click on an ability button focuses it, the focused button
then swallows the ability key, and the browser activates the *focused button* on Space — turning the
go key into the shield key. Blur the button after a pointer-initiated activation and keep focus after
a keyboard-initiated one, so a mouse player keeps Space and a keyboard player keeps focus.

## Availability

**[corrected] The list is incomplete on the client's own terms.** Keep the rule Step 20 established —
the client never claims readiness, because charge readiness depends on the off-wire safety envelope —
and add the reasons the client genuinely can see:

* **the advertised capability, per kind.** Today `enabled` checks only `set_thrust`. An unadvertised
  kind returns false from `sendCommand` into the shared refusal latch, so an ability control would
  look live and silently no-op.
* **active shield protection blocks charge.** The server refuses it and the state is published and
  already computed by the Step 20 selector.
* **a live published cooldown for that ability**, which is also the rate mitigation above.
* no own body, not running, stunned, and — for charge — no aim yet.

**Placement matters for a pinned test:** an existing assertion bans the substring `/available/i`
inside the Match-status table, and "unavailable" contains it. The controls and their reasons live
outside that table, which is where they belong anyway, since another test pins that table's
rowheader list exactly.

## Verification

Run `./scripts/run-linux-toolchain -- ./scripts/verify-web`, the step's stated gate. The plan names
four required cases — no queued activation on stun expiry, no ability or thrust while editing
settings, no ability from camera interaction, and unchanged steering and camera bindings — and each
needs a test that could actually fail: assert the *absence of a send* after the lock clears, not
merely that nothing threw; press the key with focus in a real input; activate during a camera drag;
and re-assert the existing Space and pan bindings.

Add, beyond the plan's list: the never-invalidated payload shape for both abilities; held Enter on
each button; a blur that swallows a keyup; a second identical pulse actually sending; suppression
while a cooldown is live; and the reserved arrow codes still inert.

**After the gate, capture screenshots.** Step 20 could not show a shield or stun mark in a browser
because no sender existed. This step supplies one, so the marks become reachable for the first time
and the owner should see them.
