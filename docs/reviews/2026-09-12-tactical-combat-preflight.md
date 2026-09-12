# Step 22 preflight: shoving and defensive tactical choices

Read-only preflight against `e81e100` (Step 21), by four agents including one adversarial. **No
source was changed.** Step 22 is not started and remains unticked, because the preflight found that
the step as written cannot be built honestly in one pass and that its stated Verify line does not
run the gates its own changes would break.

This records what was found so the analysis is not lost and so the owner's decision has something
concrete to act on.

## The short version

Step 22 asks bots to make quantitative predictions about physics the wire does not publish. Some of
the step is buildable today from published state; some of it is not buildable at all without either
extending the wire or letting controllers read gameplay configuration, and the second of those
violates the architecture this project has held since ADR 0002.

## What a bot genuinely cannot see

`Observation` carries the published snapshot and nothing else, and `src/controllers/CMakeLists.txt`
links no gameplay. Against that, four of the step's named behaviours need values that never reach a
reader:

* **The perfect opening's length is not published.** The shield component publishes
  `activation_tick`, `shield_expiry_tick`, `perfect_expiry_tick`, `cooldown_expiry_tick` and
  `parry_stun_duration_ticks` — all of a shield that already exists. A bot deciding *whether to
  raise one* has no window length to aim at, so "pulse within the profile's timing error" has no
  denominator without a second copy of `[abilities] shield_perfect_window_seconds` inside the
  controller.
* **The charge burst has no published length.** `normal_top_speed` is published, but
  `charge_speed_fraction` and `charge_safety_envelope_speed` are not. A bot cannot compute its own
  post-burst velocity, cannot compute a stopping distance, and cannot tell an available charge from
  one the safety envelope will silently refuse.
* **`drag_per_second` reaches no snapshot and no welcome.** It lives on `SimulationConfig`. The three
  configurations in the tree author 0, 2.0 and 40. At the deployed 2.0 a linear predictor overstates
  travel by about 7% over 32 ticks and about 21% over 0.2 s, and the bias is one-signed — it always
  predicts contact *earlier* than it happens — and it flips with a file the bot cannot read.
* **Shove force needs `restitution`, which is on the in-process body but not on the wire.** A
  controller reading it would see more than a human client, breaking the human/bot symmetry this
  tree asserts and tests.

## The decisive one is cadence, not prediction

Hosted bots decide at presentation cadence: `snapshots_per_second=20` against a 400 Hz tick, so one
decision per twenty committed ticks, on a snapshot that may itself be a publish interval stale, with
the command landing at the next tick's phase 0. The activation tick is therefore `observed + k` for a
`k` of roughly 1 to 21 that the bot cannot observe.

**The perfect opening is 32 ticks.** The unobservable jitter is comparable to the entire window the
step aims at, and it dominates any profile-authored timing error. A bot can raise a shield in
anticipation of a contact; it cannot reliably land the perfect opening, and a step that claims
otherwise would be claiming something the architecture forbids it from delivering.

## Five structural blockers inside the existing pipeline

1. **There is no utility or weighting stage.** Selection is `min_element` over nearest squared
   distance with a kind ordinal breaking exact ties, and the profile is consulted at four call sites,
   none of them selection. Two profiles differing only in settings therefore pick the *same*
   candidate on the same frame. Adding objective kinds cannot express preference; the weighted
   selection ADR 0008 names has to exist first.
2. **The 32-candidate bound is a hard throw**, enforced on the raw per-provider count before terrain
   screening. Any opponent-derived provider emits one candidate per opponent and is bounded by 64
   seats, not 32. A throw is not a degraded decision: the host isolates it and the bot silently
   stops acting for that pass.
3. **The shared terrain screen deletes exactly the candidate a safe-side shove needs.** Every raw
   candidate is filtered through `terrain_supports_point` and `first_support_exit`, which is the
   definition of the point you would stand on to drive an opponent over a cliff.
4. **`aggression` is currently a pinned rejection in two places**, including the fuzz seed
   `rejected-tactical-profile-inert-combat.cfg`, which exists to enforce ADR 0008's "no inert
   aggression/charge/shield settings are accepted before their behavior". Step 22 is the step that
   makes them legal, so that seed must be deliberately re-pointed rather than quietly flipped.
5. **The differentiation proof can pass vacuously.** `tactical_seed_for` mixes the profile *name*'s
   length and bytes, so two profiles with byte-identical settings already draw differently and
   already steer differently. A test that only shows "these two behave differently" proves nothing
   about the settings.

## The Verify line is wrong

Step 22's stated gate is `verify-focused 'unit.controllers|unit.gameplay|fixtures'` on both
toolchains plus `run-benchmarks-linux`. New required `[bot_profile]` keys break every authored
profile section in the tree — the section family is closed within an instance, so an instance that
omits a family key is `KEY_MISSING`. That includes both browser e2e fixtures and four fuzz seeds. So
the step necessarily breaks `unit.application`, `verify-fuzz-regressions`, `verify-web` and
`verify-browser-e2e`, none of which it runs; and it runs a benchmark that cannot see a controller at
all.

Two specific e2e pins would fail without any local gate noticing: the tactical-movement spec asserts
the `coaster` profile is motionless to 1e-9 on position, velocity *and* acceleration — and a charge
writes velocity with no acceleration — and the tactical-profiles spec asserts the exact published
profile list.

## Blast radius, for the record

Small, and this is the good news. The royale benchmark seats `wanderer:1`; no replay fixture seats a
tactical bot; `deploy/ubuntu-pc` seats `wanderer:1`. So bot behaviour changes should move no
benchmark hash and no accepted replay golden. The exposure is entirely the two browser e2e fixtures
and the authored configuration surface.

## Recommended split

The seam is **what the observation can already answer**.

**Step 22a — the decision pipeline, buildable today.** The profile-weighted utility stage and
objective weights ADR 0008 already names; deterministic reason codes; hysteresis and target-loss
handling; escape screening; hill intercept and hold, which needs only the published `hill_motion`;
and bounded candidate/prediction accounting with the 32-bound problem resolved. Road recovery and
gate advance already exist. This is a controllers-and-configuration step whose Verify line must add
`unit.application`, the fuzz corpus, `verify-web` and `verify-browser-e2e`.

**Step 22b — combat behaviours, gated on a decision.** Safe-side shove setup, charge timing and the
trajectory-based shield decision. Each needs one of two things first, and the choice is the owner's:

* **Publish the missing facts.** Ability tuning (`charge_speed_fraction`,
  `charge_safety_envelope_speed`, `shield_perfect_window_seconds`) and `drag_per_second` become
  public session facts, the way `movement` already is. This is a v3 wire change with schemas,
  examples, generated types, client validation and a `v3.md` row, and it belongs in a step that is
  allowed to touch the wire. It also hands the same facts to every browser client, which is
  consistent with the symmetry rule but is a real product decision.
* **Or accept authored caution instead of derived physics.** Bots screen a ray of profile-authored
  length and raise shields on a profile-authored anticipation window, with the contract stating
  plainly that these are authored numbers and not predictions of the kernel. Cheaper, honest, and
  strictly weaker: bots will sometimes charge into a wall and will rarely land a perfect parry.

Either way, the perfect-parry timing limit stands: at 20 snapshots per second against a 32-tick
opening, a bot cannot reliably hit it, and no amount of published state changes that. If reliable
bot parries are wanted, the presentation cadence for hosted bots is the thing to revisit — which is
a separate decision again.

## Status

Step 22 is **not started and not ticked**. No source file was changed by this preflight. The plan's
Step 22 block carries a dated note pointing here.
