# Step 22c: the four named tactical personalities

Implementation contract, written after a read-only preflight against `ee763c7` (Step 22b). **No
source was changed.** The preflight corrected all nine drafted decisions, found one blocker against
already-committed work, and replaced root's central mechanism with a smaller one.

Keeper, Bully, Opportunist and Cautious Racer ship as **profiles of the pipeline, never as code
paths**. A branch keyed on a profile's numeric value or on a candidate kind is permitted; a branch
keyed on a profile's identity is forbidden.

## The veto collapses, and that is the main finding

Root drafted a general "a zero kind weight becomes a veto", with an all-vetoed branch, a new reason
code and a zero-thrust fallback. **Do not build that.** Three things dissolve it:

* **There is no crash today, and root's Step 22b review says otherwise.** `tactical_select_candidate`
  initialises `best` to `candidates.size()` and its loop's first test short-circuits on
  `best == candidates.size()`, so on any non-empty set the first iteration unconditionally assigns
  index 0. The sentinel is unreachable for a non-empty set. The defect is a *contract* that permits a
  return value its one caller does not handle — not a live out-of-bounds read.
* **Had a veto introduced one, it would have been silent, not a sanitizer abort.** The candidate
  vector is `reserve`d at the raw merged count and then filled only with terrain-screen survivors, so
  whenever anything is screened out `capacity > size` and index `size()` lands *inside* the live
  allocation; ASan reports nothing without container-overflow annotations. The lease would then copy
  a candidate whose `key.kind` is an arbitrary byte, and the next pass would index a
  `std::array<double, 5>` with it — a second, unbounded read. Step 22b's review and the plan's
  Step 22b bullet both call this "a sanitizer abort"; **correct both**, because the claim understates
  it and a future reader may rely on the lane to catch it.
* **A general veto is not what any profile needs.** Cautious Racer's clause is "avoid *expensive*
  fights", not "never fight", so it authors a small positive shove weight and lets
  `minimum_opening` decide what is cheap. Only Keeper wants none at all.

**What to build instead.** Two independent, much smaller things:

1. **Change `tactical_select_candidate` to return `std::optional<std::size_t>`.** The compiler then
   closes the hole at every future call site. This is the discipline the tree already applies one
   file over — `AuthoredObjectiveWeight`'s deleted default constructor, and the no-`default` switches
   over the kind enum. A bare size check is a comment the next caller can ignore. **This lands
   whether or not anything else here does.**
2. **A zero `objective_weight_shove_setup` skips the shove provider for that profile.** This is a
   provider-level skip, not a selection-level veto: it produces no candidate rather than an
   unselectable one, so it needs no new reason code, no fallback branch, and no change to what a zero
   weight means for the four mode kinds. It also saves the whole nearest-N scan for a profile that
   would never act on it. Keeper is the one profile that authors it.

## The brake

**Keeper's clause, and Keeper's alone.** Behind a per-profile `arrival_brake_fraction`, because an
unconditional brake would mean every profile brakes and Keeper's clause would distinguish nothing —
which is what "a profile names its parameters" forbids. **Zero must reproduce Step 22b's `kArrived`
coast bit for bit**, which is what keeps both browser fixtures' 1e-9 motionless pins and every
existing `kArrived` assertion unchanged.

**kHill only — and re-justify it, because root's stated reason is false.** "Relative to the
objective's motion" *is* computable for `kShoveSetup`: that key's subject is the opponent's
`EntityId` and `tactical_shove_opponent_body` already resolves it. The true reason is the zone: a
`kZone` candidate's arrival radius is the zone's own radius, and the full zone radius is the arena
half-diagonal, so **every royale bot is inside its zone's arrival radius from the first running
tick** — a brake there is a permanent parking brake on every bot in the checked-in default
configuration, which authors `mode=royale`. Say that. Note in the header that `kShoveSetup` is the
other kind where the objective's motion is derivable, and that it is deferred.

**Source the hill's motion from a candidate field the provider writes, never from `key.subject`.**
`kMinimumEntityId` is 1, so a checkpoint index is a legal `EntityId` naming a foreign entity, and
`EntityId::create` throws outside the valid range — which reproduces exactly the permanently-inert
failure the brake exists to avoid. The hill provider already looks the motion up; carry it.

### The law: deadbeat, no deadband, no normalisation

```
brake_direction = clamp_componentwise( -v_relative / (published_acceleration * hold_seconds)
                                       * arrival_brake_fraction )
```

`hold_seconds` is the bot's own command hold, computable from published `snapshots_per_second` and
the profile's own `reaction_delay_ticks`. **This is the whole law.** Its properties are why:

* **No division by `|v|`, so no 0/0 and no NaN.** The velocity vector is divided by a positive
  scalar componentwise. As `v_relative` goes to zero the direction goes smoothly to `(0, 0)`, which
  is exactly what `require_zero` already asserts. **No deadband is needed**, and a deadband must not
  become a profile key: `controllers_limits.hpp` already set that precedent by refusing the charge
  alignment fraction as a key, "a combat knob whose only effect is letting a profile switch off a
  safety screen".
* **It cannot overshoot, so it is stable at every drag without reading drag.** The law asks for
  exactly the thrust that nulls the relative velocity over one hold in the drag-free case; the
  componentwise clamp caps it at full thrust when more is needed. Drag only removes *more* speed, so
  a nonzero drag makes the bot undershoot, and undershoot is self-correcting on the next pass.
  Overshoot is the unstable direction and this law never takes it.
* **It calibrates against a reachable quantity.** Root's drafted `min(1, |v_rel| / normal_top_speed)`
  does not: the reachable ceiling is `min(V, A / D)` and `D` is unpublished, so at the e2e fixtures'
  published ceiling of 10000 with acceleration 400 and drag 40 the terminal speed is 9 wu/s and that
  law yields a brake three orders of magnitude too small — inert in exactly the configuration whose
  instability motivated it.

**Subunit thrust is real and already ships.** `normalized_thrust_intent` is a magnitude *clamp*, not
a normaliser — its own header says so in bold — the held intent is re-scaled by the tuning's
acceleration every tick and never re-normalised, and the wire bound is per-component. `ChaserController`
already emits `unit * aggression_weight`. Cite that precedent rather than re-deriving it.

**The test vocabulary does not cover a subunit brake.** `require_zero` demands exactly `(0,0)` and
`require_go` demands unit magnitude within 1e-15. Add a third helper on `require_same_bits`; do not
loosen either existing one. And note that the arrived branch sits *outside* the seek draw, so
`objective_seek_probability=0` is not protection against a brake — exactly what Step 22b found for
the charge. Both e2e fixtures must author `arrival_brake_fraction=0`.

## The exposure quality

**Booleans in a fixed order, with no division anywhere.** Any ratio-shaped quality divides by a
legally-zero denominator — `elimination_grace_ticks`, `point_interval_ticks` and a shield cooldown
are each documented as legally zero — and worse, two of those denominators live in `mode_state`,
which the unconditional shove provider is documented never to read. `std::get` on the wrong variant
arm throws `std::bad_variant_access`, `ControllerHost` catches and continues, and
`TacticalController` assigns state only after `decide_next` returns, so the bot goes **permanently
inert on every pass**.

The fix costs nothing: `ZoneExposure` and `HillPresence` are **erasure-based presence flags** — the
systems erase the entry rather than storing a zero — so "outside the zone" and "holding the hill"
are booleans needing no denominator and no `mode_state` read at all. Specify the quality as a
fixed-order weighted sum of booleans in `[0,1]`, with the component weights as shared constants in
`controllers_limits.hpp` rather than as keys.

Record two limits in the header: `ZoneExposure` exists only under royale and `HillPresence` only
under king of the hill, so outside those modes the quality degenerates to the stun term plus the two
ability cooldowns; and only the stun term is free — the other three each cost a
`find_observed_component` linear scan.

### How it enters the score

**Multiplicatively, folded into the preference term, never additively:**

```
preference = weight * proximity * opening
```

An additive term destroys commensurability. The limits file and the scoring comment both rest on
"every term lands in the unit interval", which is what makes "a zero-tolerance profile always prefers
a candidate it can stop short of" provable rather than a tuning accident. The escape penalty is at
most 1.0, so any additive exposure term with a positive coefficient could outrank it and a maximally
cautious profile could be made to prefer a cliff-blocked shove over a clean gate. Multiplying keeps
the product in `[0,1]` and still reorders two same-kind candidates, which is the whole point —
with one kind the weight is a common factor and cannot reorder anything.

**The field's default must be `{1.0}`, not `{}`.** `TacticalObjectiveCandidate` gives its other
members zero and false defaults and the `candidate()` helper aggregate-initialises only four of them,
so a `double opening{}` would multiply every hill, zone, gate and recovery candidate's preference by
zero. This is the `AuthoredObjectiveWeight` defect one layer down and **the compiler cannot catch it
here**, because the aggregate has a default member initializer either way. Add a test that a raw mode
candidate scores identically before and after this step.

Amend, rather than silently break, the header's ownership rule that "a raw provider candidate carries
zero and false for both: only `collect_tactical_objective_candidates` holds the terrain and the
arena, so only it may answer them". Exposure is the first field only the *provider* can answer.

**`minimum_opening` runs after the nearest-N filter, not before it.** Root drafted the reverse to
stop a nearest-N discarding the most exposed opponent, but that cancellation needs 33 or more dynamic
controllable bodies against a filter width of 32, and every fixture in the tree seats three. Closing
it would cost either O(P x S) linear scans or a second N-way merge beside the one
`component_join.hpp` declares itself to be — a primitive whose header records that every prior
private copy was an engine review finding. **Write the interaction into the header as an honest
limit** and cite the width against the rosters that exist.

## The four keys

Appended to `TacticalProfile::Section`, never interleaved. Each is read by behaviour landing in this
same commit.

| Key | Domain | Serves |
|---|---|---|
| `road_caution_fraction` | **strictly positive**, `(0, 1]` | Cautious Racer's "preserve road clearance" |
| `arrival_brake_fraction` | `[0, 1]`, zero reproducing 22b exactly | Keeper's "brake relative to its motion" |
| `exposure_preference` | `[0, 1]`, zero reproducing 22b exactly | Opportunist's "prefer exposed targets" |
| `minimum_opening` | `[0, 1]` | Opportunist's "abandon low-value fights" |

**`road_caution_fraction` must be strictly positive, unlike every other fraction in this family.**
The race provider compares `nearest.distance > fraction * road->half_width()`, so a zero means
"recover unless exactly on the centreline" — it *inverts* race behaviour rather than disabling it.
`RacerController` already validates this same knob as strictly positive; adopt its domain. That also
turns every missed positional construction site into a loud `TacticalProfile::create` throw instead
of a silently inverted racer, which matters because four positional `Section` sites value-initialize
a new trailing double to 0.0 and still compile, and `AuthoredObjectiveWeight` protects only the
weights. `kMinimumProfileValues` can no longer author 0 for this one key — say why there.

Also drop `race()`'s `[[maybe_unused]]` on its policy parameter, and note in the policy comment that
a *mode* provider now reads a profile number too, not only the combat screens.

## The four authored vectors

Ship in `config/blob-royale.cfg` **only**. `deploy/ubuntu-pc/blob-royale.cfg` declares no
`[bot_profile]` section at all; the tactical-profiles browser spec pins its own fixture's exact
two-element list three ways; and the tactical-movement fixture seats exactly its two profiles against
a three-seat lobby. **Both e2e fixtures still need the four new keys in all four existing sections**
or neither server starts.

Every number below has a reason. Where a reason is not written here, take it from the preflight.

* **Keeper** — holds one hill, brakes on it, never hunts. `objective_seek_probability=1`,
  `reaction_delay_ticks=40`, `aim_error=0.02`, `target_persistence_ticks=400`, weights
  hill `1` / zone `0.5` / gate `0.25` / recovery `0.5` / **shove `0`** (the one endorsed zero: it
  skips the shove provider, and it is literally "defend a stable interior"), `risk_tolerance=0.25`,
  `prediction_horizon_ticks=120`, `charge_screen_diagonal_fraction=1.0`,
  `shield_anticipation_ticks=28`, `road_caution_fraction=0.75`, **`arrival_brake_fraction=1.0`**,
  `exposure_preference=0`, `minimum_opening=1.0`.
* **Bully** — numbers alone over Step 22b's behaviours; the only profile needing no new mechanism.
  `objective_seek_probability=1`, `reaction_delay_ticks=60`, `aim_error=0.05`,
  `target_persistence_ticks=200`, weights hill `0.5` / zone `0.5` / gate `0.375` / recovery `0.5` /
  **shove `1`**, `risk_tolerance=0.5`, `prediction_horizon_ticks=80`,
  `charge_screen_diagonal_fraction=0.25` (just past the 253 world units a from-rest burst needs to
  stop at this file's acceleration and zero drag — ADR's "acceptable recovery path" as a number),
  `shield_anticipation_ticks=21`, `road_caution_fraction=0.9`, `arrival_brake_fraction=0`,
  `exposure_preference=0` (fights whoever is nearest — precisely what separates it from Opportunist),
  `minimum_opening=0`.
* **Opportunist** — the fastest reflexes and the only profile that scores exposure.
  `objective_seek_probability=1`, `reaction_delay_ticks=20` (one decision interval; an opening that
  survives four ignored passes is not an opening), `aim_error=0.03`, `target_persistence_ticks=100`,
  weights hill `0.5` / zone `0.625` / gate `0.5` / recovery `0.625` / shove `0.75`,
  `risk_tolerance=0.625`, `prediction_horizon_ticks=80`, `charge_screen_diagonal_fraction=0.375`,
  `shield_anticipation_ticks=24`, `road_caution_fraction=0.8`, `arrival_brake_fraction=0`,
  **`exposure_preference=1.0`**, **`minimum_opening=0.5`**.
* **Cautious Racer** — the lowest risk tolerance and a deliberately **non-zero** shove weight.
  `objective_seek_probability=1`, `reaction_delay_ticks=100`, `aim_error=0.01`,
  `target_persistence_ticks=600`, weights hill `0.25` / zone `0.5` / gate `1` / recovery `1` /
  **shove `0.125`**, `risk_tolerance=0.125`, `prediction_horizon_ticks=200`,
  `charge_screen_diagonal_fraction=0.5`, `shield_anticipation_ticks=32`,
  **`road_caution_fraction=0.6`** (above the 0.5714 floor at which the recovery threshold would fall
  inside the shipped gate radius and pull a bot off a gate it is standing in),
  `arrival_brake_fraction=0`, `exposure_preference=0.5`, `minimum_opening=0.75`.

Two rules the vectors encode and the header should state: **recovery weight is never below gate
weight** for any profile, because a profile preferring the gate drives off the road; and **no profile
authors a zero weight on a kind its running mode produces**, because that is the inert-bot trap.

## The gate hole this step must close

**Nothing in the tree parses `config/blob-royale.cfg`.** `deployment_fixture_tests.cpp` loads
`BLOB_ROYALE_DEPLOYMENT_FIXTURE_DIRECTORY`, which `tests/fixtures/CMakeLists.txt` defines as
`deploy/ubuntu-pc`. The shipped configuration is referenced only by `scripts/verify-linux` — Step
24's line — and by `scripts/assemble-release-linux`. So the four profiles this step authors, and
Step 22b's three new `[bot_profile.steady]` keys, and the `steady` section since Step 15, all ship
with **zero automated parse coverage**.

Root verified by hand that the shipped configuration loads and the server reaches `running` after
Step 22b, so nothing is broken today — but that is a one-off check, not a gate.

**Add `BLOB_ROYALE_SHIPPED_CONFIGURATION_FIXTURE_DIRECTORY` and one `fixtures`-lane case** that loads
`config/blob-royale.cfg` through `ApplicationConfigLoader` and asserts the catalogue holds exactly
the five profile names. Mirror `load_deployment_inputs`. **And correct the Step 22b contract's
sentence** claiming `deployment_fixture_tests.cpp` "parses the shipped configuration and is the only
C++ lane that sees `config/blob-royale.cfg`" — it is false, and Step 22b's keys shipped on it.

## ADR 0008 amendments owed

The honest line is **whether a clause named something the published-state boundary forbids, or
something this step simply did not build.** Amend the first kind; record the second kind as deferred,
because amending a buildable clause away is moving the goalposts.

* **Amend:** Opportunist's "prefer distracted/exposed targets" ships as *exposed* only. Nothing in
  the component registry encodes attention or target, so "distracted" is unobservable.
* **Record as not built, buildable, deferred:** Keeper's "defend a stable interior" — a hill
  candidate's arrival radius is the *full* published hill radius, so a bot reports arrived one world
  unit inside the rim, where it is trivially shoved out, and the brake makes that worse by stopping
  it there. The value-keyed fix that stays inside the ADR rule is an `arrival_radius_fraction`
  scaling the published radius. **Named as owed; not built here.** And Bully's "charge only with an
  acceptable recovery path" — the charge screen asks whether there is ground under the corridor, and
  its own header says it is never a claim the body can stop before leaving it. A recovery path is
  about getting back, and nothing models it.
* **Record as a limit:** nothing makes a Bully shove with ordinary thrust. The only push is the
  charge, so a Bully on its charge cooldown stands at the standing point doing nothing.

## Verification

```
./scripts/verify-focused 'unit.controllers|unit.gameplay|unit.application|fixtures'
./scripts/verify-focused 'unit.controllers|unit.gameplay|unit.application|fixtures' linux-clang-asan-ubsan
./scripts/verify-fuzz-regressions
./scripts/run-linux-toolchain -- ./scripts/verify-web
./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e
git diff --check
```

Prove: each of the four profiles selects differently from the others on one identical observation,
with the profile **name** held constant so `tactical_seed_for` cannot make the proof vacuous;
`arrival_brake_fraction=0` is bit-identical to Step 22b at `kArrived`; the brake nulls a relative
velocity without overshoot and emits exactly `(0,0)` at rest; `exposure_preference=0` scores
identically to Step 22b and a raw mode candidate is unchanged; exposure reorders two equidistant
shove candidates; `minimum_opening` drops a closed opening and fires `kLost`, releasing the lease and
re-arming the reaction window; a zero shove weight skips the provider rather than producing an
unselectable candidate; `tactical_select_candidate` returns an empty optional only where documented;
`road_caution_fraction` at its floor keeps a bot on a gate it is standing in; and the shipped
configuration parses with exactly five profiles.
