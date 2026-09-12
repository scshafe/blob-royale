# Step 21 verification review

Accessible ability controls. Parent checkpoint `adb410d` (Step 20). Implementation contract:
[`2026-09-12-ability-controls-contract.md`](2026-09-12-ability-controls-contract.md).

Web-only: no schema, encoder, component or server change.

## Completed checks

| Gate | Result |
|---|---|
| `run-linux-toolchain -- verify-web` | 986 / 986 tests; 83 schemas, 33 examples |
| within it: prettier, `generate:protocol:check`, `validate:protocol-examples`, `tsc --noEmit`, `eslint --max-warnings 0`, production build | all passed |
| `run-linux-toolchain -- verify-browser-e2e` | Chromium 13 / 13, 0 retries, 0 skips, 0 flakes |
| `git diff --check` | clean |

986 from Step 20's 940. The browser gate is **not** this step's stated gate; it was run because this
step edits an e2e spec, and shipping a knowingly-broken suite for a later step to discover is worse
than a slow gate here.

## What landed

Shield is bound to `KeyS`, charge to `KeyD`, both with ordinary on-screen buttons. Activation reuses
the canonical input owner's guards and its `sendCommand`, adds a per-ability one-shot latch and a
rate discipline, and computes per-ability availability from published state alone.

## The preflight found five blockers

Step 21 arrived with no implementation contract. A four-agent read-only preflight — one purely
adversarial — reviewed a drafted decision list before any source edit and rejected six of nine.

* **Unthrottled ability keys would have disconnected players mid-match.** The per-session command
  bucket is a 30-token burst refilled at 20 per second, the token is charged before parsing, and an
  empty bucket is `1008 command_rate_exceeded` — a close, not a refusal. Held thrust with a moving
  cursor already runs at the full refill rate. The 50 ms floor everyone assumes protects the sender
  actually lives inside the thrust `flush`, not in `sendCommand`, so "go through the existing sender"
  buys no protection at all. And the traffic would have bought nothing: the mailbox coalesces
  same-kind inputs, so extra same-tick pulses were never more than one attempt.
* **The shield button would have failed silently for every player.** The thrust encoder omits
  `input_generation` when the token is absent; charge matches that, shield does not — its schema
  requires the member and admits `null`. Copying the thrust expression emits `{}`, the closed
  envelope rejects it, and `sendCommand` returns false without a word. The players affected are
  exactly those who have never been stunned, which is everyone at the moment they first press it.
* **Reusing the thrust `flush` breaks a pulse five ways**: its change-only gate swallows a second
  identical press, its 50 ms coalescing delays one against an 80 ms perfect opening, a parked pulse
  dies on any effect re-run, its `currentTransmission` bookkeeping is level state a pulse lacks, and
  its refusal latch is shared with held thrust.
* **A focused ability button turns Space into the ability key.** Clicking focuses the button, the
  focused button swallows the ability key, and the browser then activates *that button* on Space.
  Separately, a focused button fires click on Enter **keydown** and held Enter repeats, so the button
  is a second repeat source the keydown guard cannot see.
* **Remembered aim was wiped by every stun.** `lastNonzeroAimDirection` is effect-local and the
  effect's dependencies include `inputLocked` and `inputGeneration`. The contracted discard set is
  body loss, body replacement, a new welcome, and disconnect — not a stun.

## Shift was rejected, with reasons

ADR 0008 proposed Shift for shield. It is mechanically indefensible here: the go-key guard filters
`altKey`, `ctrlKey` and `metaKey` and *deliberately* not `shiftKey`, pinned by an existing test,
because Shift+Space must still thrust. A bare-Shift shield would fire on the leading half of every
Shift+Tab, five presses is the Windows Sticky Keys gesture, it is two codes against a
one-constant-per-action model, and modifier keys do not auto-repeat — so the key-repeat rule the ADR
itself requires could not be demonstrated against it.

The bindings instead come from the pool this ADR already reserved when Step 11a retired directional
steering: "those keys are available for later charge/shield bindings". `KeyS` and `KeyD` are the
home-row keys under the hand whose thumb holds Space. The four arrow codes stay reserved and stay
asserted inert; `blobRoyaleBrowserCursorSteering.spec.ts` was narrowed to the six that are still
retired, with the reason written in, because asserting the other two inert would now assert the
opposite of the contract.

Both are initial choices and reversible in one constant.

## Verified in a browser, for the first time

Step 20 rendered shield and stun but no browser could produce either, because no sender existed.
This step supplies one, so the marks were captured live against a real server through the project's
own pinned harness:

* shield pressed — ring up, "Protected 0.3 s left", both buttons disabled, Charge explaining "You
  cannot charge out of your own shield", which is ADR 0008's shield-blocks-charge rule reaching the
  player;
* 0.6 s later — ring gone, "No protection" with "Cooling 0.1 s" still running, Charge available
  again. That is Step 20's three-state shield renderer proving its third state live: a component that
  outlives its protection draws nothing;
* the lobby — both controls present, disabled, and explaining themselves before a match exists.

The images went to the owner. The temporary capture spec was deleted and the tree carries no trace
of it.

## Limits, recorded rather than papered over

* **Charge is mouse-dependent.** Its direction comes from the cursor aim or the last nonzero steering
  direction, and both exist only for a mouse pointer inside the canvas. A touch, pen or keyboard-only
  player has no aim, so charge stays unavailable with its explanation showing. Shield is fully usable
  without a pointer. In a step titled *accessible* ability controls this is a real gap; closing it
  means inventing a keyboard aim, which would partly reverse Step 11a's removal of directional
  steering. **It is an owner decision, named here rather than shipped quietly.**
* **The client still never claims readiness**, on Step 20's rule: charge readiness depends on the
  safety envelope, which is server-side and not on the wire. A control says *cooling* and *cooldown
  over*, never *ready*.
* **A refused activation still produces no receipt.** Steps 18 and 19 committed that, and this step
  does not change it: the published windows remain the only positive proof an activation committed.

## Exact acceptance commands

```
./scripts/run-linux-toolchain -- ./scripts/verify-web
./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e
git diff --check
```

Transient logs are `/tmp/s21-web.log` and `/tmp/s21-browser.log` and are not committed.
