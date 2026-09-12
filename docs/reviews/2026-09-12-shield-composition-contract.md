# Step 18: tap shield and the live guarded composition

Implementation contract before source edits. Parent checkpoint: `8d2e15e` (Step 17).
Use the existing declarations, frozen-world contact response, typed events and tick windows.
No new kernel socket, event root, pair equation, command-ack channel, role, or control binding.

## Authored tuning and value ownership

One validated `gameplay::AbilityConfiguration` owns required `[abilities]` settings:
`shield_duration_seconds=0.4`, `shield_perfect_window_seconds=0.08`,
`shield_cooldown_seconds=0.9`, `parry_stun_duration_seconds=0.6`.
These are the ADR's initial tuning assumptions, not owner-selected balance numbers. Convert once
with shared `duration_ticks`; expose corresponding `_ticks()` getters. Rounded shield, perfect and
stun durations must be positive; perfect must not exceed shield. Cooldown may be zero or shorter
than shield: admission requires both prior protection and cooldown to have ended. TickWindow owns
checked absolute endpoint construction; overflow remains named refusal, never saturation.
Add only working shield fields now; Step 19 adds actual charge fields to this same owner.

`GameModeConfiguration::abilities` is passed through each mode's owned declaration. Convenience
factories retain defaults, while the registry path must consume the authored value. A noncapturing
contact response must not read process globals or invent a second configuration owner.

Validated body-bound `simulation::Shield` exposes `activate(activation_tick, shield_duration_ticks,
perfect_duration_ticks, cooldown_duration_ticks, parry_stun_duration_ticks)`, `shield_window()`,
`perfect_window()`, `cooldown_window()`, `parry_stun_duration_ticks()` and `canceled_at(tick)`.
The three windows share the original positive activation. Capture the configured stun duration
on activation so the frozen defender supplies the effect it actually owns. Private construction
enforces positive effect durations and ordered protection endpoints.

Cancellation shortens only a still-active protection window to the cancellation tick using the
same TickWindow value; elapsed perfect history, original activation, cooldown and captured duration
remain unchanged. Cancellation at activation legitimately yields empty protection; repeated
cancellation is an exact no-op. Cancellation before activation is a named chronology error.
This is current post-commit availability, not a denial of the documented same-tick defensive grace.

## Command, admission, and lifecycle

`ShieldCommand` is an entity-addressed pulse with the same optional `input_generation` semantics as
ThrustCommand: absence is the initial generation, present zero is invalid, a stale generation cannot
activate. Wire payload carries required `input_generation` (null or a positive exact tick), never an
actor identity. Existing session ownership stamping and mailbox/value validation remain the edges.
Preserve existing command bits/ranks; add only the new kind and deliberate replay parser support.

Shared PreKernel `AbilitySystem` owns shield activation and expired Shield removal, and is the owner
Step 19 extends for charge/conflict priority. It uses the canonical input lock and admits a pulse only
while running, with Controllable plus a dynamic live body, matching generation, unfinished race,
no stun, no active shield and expired cooldown. Refused pulses do not consume cooldown or queue an
activation. Race course publication stays before this admission so first-tick finished seeds lock.
All four gameplay modes declare the system and advertise shield only with its implementation.

PostKernel StatusSystem cancels protection on active stun, preserving cooldown and later physical
bumps. Keep its validate-before-mutation request aggregation and maximum-expiry merge. Body loss and
round reset clear Shield through the existing lifetime trait, not per-owner cleanup. Empty/canceled
protection is distinct from an expired cooldown. Only AbilitySystem removes fully expired Shield.

Queue acceptance/local send is not activation confirmation. Published Shield windows prove a
committed activation. This step adds no per-request negative receipt and must not claim one; existing
ordinary command refusal semantics remain. Keys/buttons and sender integration remain Step 21.

## One live response path

Add one pair-symmetric live adapter around the exact `compose_guarded_pair` core. A single row
matches dynamic/body pairs, including static counterparts, above built-ins in the four modes.
Static/static pairs remain absent. Read committed Shield windows into PairGuardFacts; no contact-time
status mutation. Translate contact/elimination facts unchanged and each stun recipient using the
opposite (defending) body's captured duration. Preserve typed per-body motion dispositions.

Move the existing lethal phase/presence predicates and diagnostic name unchanged into the guarded
composition's ownership, then delete the old standalone lethal response/row. Retain its admission
and pass-through proofs against the new adapter. Unguarded ordinary math is unchanged; diagnostics
for composed nonlethal contacts now name guarded_pair. Engine-only undeclared modes keep built-ins.
No second quarter-impulse formula: retain the pure core's tested 25% target and non-closing
projection precedence, moving-defender/mutual-perfect behavior and source-oriented any-touch policy.
Touch without incoming motion never manufactures a perfect parry. Cliffs and zone rules bypass shield.

## Publication and trust boundaries

The v3 Shield value publishes exactly `activation_tick`, `shield_expiry_tick`, `perfect_expiry_tick`,
`cooldown_expiry_tick`, and `parry_stun_duration_ticks`. Endpoints are safe integers with
activation <= perfect expiry <= shield expiry and activation <= cooldown expiry. Activation and
captured stun duration are positive; canceled protection may have zero length. This current effect
parameter is public to both C++ bots and browser readers, not a hidden future field. No charge fields.
Land schema, encoder, vocabulary, examples, generated types, strict validation, outbound command
union and explicit nonvisual renderer entry together. Visual treatment remains Step 20.

Protect room state against actor spoofing, invalid scalar inputs and stale pre-stun pulses at the
existing boundaries. Do not impose seated tuning authority on an entity's own combat command or
promise malicious-client physical-key verification. Human, bot and replay inputs share admission.

## Verification

Retain Step 18's original exact core lanes, fixed corpus and full web gates. Add application/server/
controller focused lanes, full production browser and complete benchmarks because command vocab,
required configuration and contact declarations cross those boundaries. Root alone runs builds,
tests, formatting, generation and commits; one C++ build at a time. All local Docker evidence is
advisory. Keep historical oracles/artifacts immutable and retain the pure composition hashes/work.

Prove every timing/cancellation boundary, cooldown preservation, stale generations across missed
stuns, phase/body/finish admission, zero-delay return/restart cleanup, cross-mode activation, ordinary
and perfect lethal protection, moving defenders, static walls, mutual perfects, fixed-tick grace,
hazard lifetime and later bumps, no post-death impacts, protocol mutations, and human/bot/replay
symmetry. No weakened energy/dual-shield goldens, retries, skips, hidden fallbacks or extra math.
