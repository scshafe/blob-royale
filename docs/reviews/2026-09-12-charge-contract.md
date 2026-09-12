# Step 19: one-shot charge

Implementation contract, written after a read-only preflight against `7dd764f` (Step 18) and
adversarially reviewed before any source edit. Use the existing declarations, the shared ability
system, the frozen-world contact response, typed events and tick windows. No new kernel socket,
event root, pair equation, policy socket, mode declaration, system, or control binding.

Six draft decisions were changed by that review; each is marked **[corrected]** with the reason,
because the wrong version of each is the one a reader would otherwise reach for.

## Authored tuning and value ownership

The existing required `[abilities]` section and the existing `gameplay::AbilityConfiguration` gain
charge's three keys. Step 18's header already promises exactly this ("Step 19 extends this same
owner with charge's gain, cooldown and safety envelope"), so the envelope is not optional here.

* `charge_cooldown_seconds=1.2`, converted once through the shared `duration_ticks`.
  **[corrected] It must be validated strictly positive**, unlike `shield_cooldown_seconds`. The
  shield exemption is justified by a second gate — admission requires both the prior protection and
  the cooldown to have ended — and charge has no protection window, so a cooldown rounding to zero
  ticks admits a burst on every tick, four hundred a second. Reuse `require_positive_ticks` and
  `GAMEPLAY.ABILITY_DURATION_NOT_POSITIVE`, and record in the header why the exemption does not
  transfer.
* `charge_speed_fraction=0.75`, a dimensionless multiple of the **current** normal ceiling read from
  match state, not an authored speed: ADR 0008 says "0.75 times the current normal movement
  ceiling" and the plan says "from Step 10's tuning", and that ceiling is live-tunable. Validate
  finite and strictly positive.
* `charge_safety_envelope_speed=20000` world units per second. Validate finite, strictly positive,
  and at most `kMaximumPhysicalComponentMagnitude`.

**One cross-key rule, and it is the one that bounds the fraction:**
`charge_speed_fraction * kMaximumNormalTopSpeed <= charge_safety_envelope_speed`. In words: a charge
from rest must remain admissible whatever a room tunes its ceiling to. That closes the hole an
unbounded fraction would leave — `charge_speed_fraction=1e6` against a 10,000 wu/s ceiling is a
1e10 wu/s burst whose components sit inside `Vector2`'s domain and which then exhausts
`kMaximumMotionEventCount` on the first tick, and every exhaustion is fatal to the room — without
inventing an arbitrary ceiling on the fraction itself.

`AbilityConfiguration`'s canonical header currently states that it stores nothing but `std::uint64_t`
tick counts and that no seconds value survives startup. Two of the three new values are doubles, so
**that paragraph must be amended in the same commit**. Keep the named-locals-in-declared-key-order
rule: `create` must report the first declared offending key, never a compiler-dependent one. New
codes: `GAMEPLAY.ABILITY_SCALAR_NOT_FINITE`, `GAMEPLAY.ABILITY_SCALAR_OUT_OF_RANGE`, and
`GAMEPLAY.ABILITY_CHARGE_BURST_EXCEEDS_SAFETY_ENVELOPE` for the cross-key rule.

These three values are ADR 0008 tuning proposals and an engineering guard, **not owner-selected
balance numbers**, exactly as the four shield values are. The owner accepted *that a separate
validated safety envelope exists* at Step 1; the owner has never selected its number.

## The component and the command

Body-bound `simulation::Charge` holds exactly one `TickWindow`: the cooldown. Charge is one-shot, so
there is no active window and no captured effect parameter. It exposes `activation_tick()`,
`cooldown_window()` and a private validated constructor, and publishes exactly `activation_tick` and
`cooldown_expiry_tick`. Both endpoints, never a countdown, for the reason the shield encoder states:
an absolute tick stays true in a frame a client buffered or received late. The activation is
published as well as the expiry because a cooldown arc needs the denominator, and
`charge_cooldown_seconds` is deliberately server-side. Because the cooldown is strictly positive,
the schema and the encoder require `cooldown_expiry_tick > activation_tick` **strictly**, unlike
shield's `<=`. It declares the Step 13 body-bound trait; only `AbilitySystem` removes it, once the
cooldown has expired.

`simulation::ChargeCommand{EntityId entity; Vector2 direction; std::optional<TickSequence>
input_generation;}`. `CommandKind::kCharge = 1u << 11`; application rank 11, appended after shield's
10 so every existing rank is preserved.

**[corrected] The wire payload is `{x, y, input_generation}` with `input_generation` OPTIONAL,
matching `set_thrust` and not `shield`.** The draft copied shield's required-and-nullable shape on
"the two ability commands are one vocabulary" grounds. The tree's own written discriminator, stated
in three committed places, keys that choice on payload shape instead: shield is required-and-nullable
because it has no other member and an optional one would make `{}` the whole message, and those
three statements explicitly contrast it with `set_thrust`, which "carries x and y whatever happens".
Charge carries required `x` and `y`. Nothing behavioural turns on it — the C++ struct and the
exact-optional-equality admission are identical either way — so follow the recorded rule rather than
introduce a second, competing one. `x` and `y` reuse `set_thrust`'s `unit_interval_scalar`.

## Direction

**[corrected] Charge may not use `gameplay::normalized_thrust_intent`.** That function is the tree's
single magnitude *clamp*, not a normalizer: its scale is `1.0` whenever the magnitude is at most one,
and its own header says "Subunit analog intent keeps its magnitude". That is right for an analog
throttle and wrong for a one-shot activation with a stated fixed gain. Routed through it, a client
sending `{"x":0.5,"y":0}` — legal under the per-component unit bound — would receive half the burst,
making pointer distance into strength: the inverse of the boundary the shield command draws, and a
direct contradiction of both ADR 0008's "Initial gain: 0.75 times the current normal movement
ceiling" and the owner's authoritative-fixed-strength decision.

Add one canonical helper beside it in `src/gameplay/shared/locomotion.{hpp,cpp}`:

```cpp
// canonical: unit_direction -- the one true normalization, for a fixed-gain activation.
[[nodiscard]] std::optional<simulation::Vector2> unit_direction(const simulation::Vector2& direction);
```

It computes the magnitude with the written-out `sqrt(x*x + y*y)` this codebase uses everywhere for
cross-toolchain agreement, **divides** rather than multiplying by a reciprocal, and returns
`std::nullopt` — never throws — when the magnitude is not finite, is zero, or when the divided
components are not finite or leave the component domain. The nullopt band is not just exact zero:
`{"x":1e-200,"y":0}` passes the decoder and `InputBatch`, its squared magnitude underflows to zero,
and a reciprocal would be infinite. Every one of those cases is a refusal, and the whole point of
returning an optional is that `AbilitySystem` must never throw for a world a client can reach.

The header must record why charge normalizes where thrust clamps, and the ADR amendment must say the
same.

## Admission, the conflict rule, and the effect

Everything lives in the existing `AbilitySystem` at `kPreKernel`. **No second system**: a separate
one could not decide the conflict without a third place holding the priority, which is why the
system is named `ability` rather than `shield`.

`apply` becomes, in this order:

1. The expiry sweep gains a second pass erasing every `Charge` whose cooldown has expired — ids
   collected first, erased after, for the reason the existing comment gives.
2. One join over `(Controllable, PhysicsBody)`. Fetch both recorded pulses; return early only when
   both are absent.
3. Evaluate the four gates common to both abilities exactly once against the world at entry: running
   phase, non-zero tick, non-static body, and the canonical input lock.
4. **Read `protection_active` into a local before any write.** Compute both eligibility booleans
   before either write. This is what makes "evaluate eligibility first" real rather than nominal:
   under a naive "do shield, then do charge" ordering the tie resolves by re-reading a store the
   shield branch just wrote, so it happens to work only because `shield_duration_ticks > 0` is
   enforced in a different file, and "a shield is active" becomes indistinguishable from "a shield
   was activated this tick".
5. `shield_eligible` is the existing predicate set. `charge_admissible` is: pulse present, exact
   optional generation equality, `!protection_active`, charge cooldown expired, a unit direction
   obtainable, and the safety envelope admitting the result.
6. Apply shield if `shield_eligible`. Apply charge if `charge_admissible && !shield_eligible`. The
   conflict gate is spelled `!shield_eligible`, **not** "no Shield present", so an ineligible shield
   pulse provably cannot suppress an eligible charge, and losing the tie consumes no charge cooldown.

The effect is an instantaneous **additive** velocity burst of
`charge_speed_fraction * world.match().movement.current.normal_top_speed()` along the unit direction,
written through `with_velocity`. Additive, so lateral velocity survives; it touches no acceleration,
position, radius, mass, or collision capability. Read `body.velocity()` **before** the write: the
join hands out a reference into the `PhysicsBody` store, and although `insert_or_assign` on an id the
store already holds assigns in place — which is what keeps the forward walk valid, exactly as the
steering system records — the write mutates the referent.

**The safety envelope is checked in raw doubles before any `Vector2` is constructed.** This is not a
style preference. `Vector2::operator+` throws on a component past `1e12`, that throw escapes
`AbilitySystem::apply` and `GameSimulation::step`, and the runtime worker then calls
`record_worker_failure` and returns — the simulation thread stops permanently and the room is dead.
One client's charge must not be able to end a match. So: compute `next_x` and `next_y` as raw
doubles, require both finite, and require `sqrt(next_x*next_x + next_y*next_y)` at most
`charge_safety_envelope_speed`. A magnitude bound at or below the component domain keeps both
components representable, so one check covers both concerns.

**Every refusal is a silent no-op**, on exactly the path every other ability refusal already takes:
no cooldown consumed, no queued activation, no event, no error, no receipt. ADR 0008's "refuse an
inadmissible activation explicitly, not silently convert it" contrasts refusal with *conversion*, not
with silence, and Step 18 committed that a refused pulse produces no per-request receipt and that
this contract promises none. A thrown validation error would be strictly worse: a client-triggered
tick failure.

`StatusSystem` needs **no change**. A burst already in flight keeps flying, which is precisely ADR
0008's "never restores old velocity" and "subsequent external impulses may still move the stunned
body"; the status pass zeroes intent and acceleration and never touches velocity. Fresh activation is
already blocked, with no new code, by the canonical input lock plus the generation bump. A live
charge cooldown survives a stun for the reason shield's cooldown survives cancellation.

## What the burst does and does not decay under

**[corrected] Do not claim the burst decays under drag.** `drag_per_second=0` is the checked-in
value in `config/blob-royale.cfg` and in seventeen of the eighteen replay fixtures, and at zero
drag phase 1's factor is exactly `1.0`, so the burst is permanent there. `deploy/ubuntu-pc/blob-royale.cfg`
authors `drag_per_second=2.0`, so a deployed burst does decay: development and deployment differ,
and no comment may assume either. The plan's "decaying under drag" is
true only where drag is authored non-zero. **The safety envelope, not drag, is what bounds repeated
charges**, and that is the whole reason the envelope is a required key rather than a representability
check: at the default 600 wu/s ceiling a body accumulates 450 wu/s per activation and is refused once
the next burst would pass 20,000, long before any solver work budget is threatened. Say this plainly
in the ADR amendment and in `docs/protocol/v3.md`; a comment claiming decay would be false in the
shipped configuration.

## Ordering inside kPreKernel

`AbilitySystem` runs **last**, after `ThrustSteeringSystem`, in all four modes. On the activation tick
the thrust limiter has therefore already sized this tick's acceleration against the *pre-burst*
velocity, and the committed endpoint is `v_pre + burst + a*dt` — one tick of already-certified
propulsion stacked on top of the burst, at most about 1 wu/s at the default acceleration. **Accept
this in writing rather than reordering.** Running the ability system first would be worse: the
limiter's `max(ceiling^2, v.v)` bound would then include the burst, letting thrust sustain a charged
speed indefinitely, which is a much larger violation of "above the normal ceiling, controls may
brake/turn but must not add speed until back within it". Record the accepted residual in the ADR.

From the next tick on, that same `max` term is what lets a charged body steer without amplifying or
braking, which is the intended feel.

## Publication and the rest of the wire rule

Land together, in this one commit: the `charge` command schema and the `charge` component schema,
the decoder arm, the encoder and its registry include, the vocabulary entries, both golden examples
and their mapping, the regenerated client types, strict client validation of the new component's
cross-field ordering, the outbound command union, the explicit **non-visual** renderer entry, the
`docs/protocol/v3.md` rows and object-member-order bullet, one new implementation row in the
coordinated-3.0 table, and `kCharge` in all four modes' accepted masks. Visual treatment is Step 20
and keys or buttons are Step 21; add no sender.

`kV3ComponentKindNames` grows 16 to 17 and `kV3ClientCommandKindNames` 7 to 8, both ascending, and
**every by-position index in `command_wire_kind.hpp` must be re-verified by hand** — `charge` sorts
first in the client-sendable array, so every existing index shifts by one. `welcome-data.schema.json`'s
`maxItems` becomes 8, and the golden welcome gains `charge` along with its fixture mask, its pinned
frame bytes, both `welcome*-message.json` examples and the one client expectation that reads them,
exactly as shield's did.

A new required `[abilities]` key migrates every authored configuration in the tree, and appends a
fifth entry to the benchmark fixture's four-place provenance contract. That provenance string will be
the one retained benchmark field that differs from Step 18's baseline.

## Verification

Run every gate in the plan's Step 19 line, and retain Step 18's supplemental lanes because the
command vocabulary, a required configuration key and a new component all cross those boundaries:
both core lanes, both application/server/controller lanes, the fixed corpus, full web, the full
production browser gate, and complete benchmarks. Root alone builds, one C++ build at a time. All
local Docker evidence is advisory.

`continuous_motion_charge_speed` is the retained pure-solver case the plan means by "the charge
benchmark against Step 4's baseline". Its `approved_charge_tuning: false` flag stays false — its
40,000 wu/s workload is a solver stress, not the authored tuning — and its historical
`complete_result_hash` must not move.

Prove: every admission gate independently; the conflict rule's three clauses separately, including
that an ineligible shield pulse does not suppress an eligible charge and that losing the tie consumes
no cooldown; zero, subnormal and non-unit directions; the envelope refusal, and that repeated charges
at zero drag converge on it instead of growing without bound; that no refusal throws for any world a
client can reach; additive-not-replacing velocity with lateral motion preserved; no tunnelling
through a body or a hole at charged speed; support loss while charging; an ordinary contact at
charged speed through the Step 18 composition, including into a perfect shield and into a lethal
hazard; cooldown survival across a stun; body loss and round reset clearing `Charge` through the
lifetime trait; cross-mode activation; protocol mutations; and human, bot and replay symmetry.
No weakened goldens, retries, skips, hidden fallbacks, or extra math.
