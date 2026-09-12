# Step 22b: shoving and the combat abilities

Implementation contract, written after two read-only preflight rounds against `5b400cd` (Step 22a).
**No source was changed by either round.** The first round rejected all eight of root's drafted
decisions. Root verified the findings against source, corrected them, and put the corrected set
through a second adversarial round, which found a crash, a permanently-inert-bot defect, and a
contradiction between two of the corrections. This is what survived.

Everything here is buildable from published state under the owner decision recorded in ADR 0008
§ "Owner decision: authored caution for bot combat". Nothing here needs a wire change, a kernel seam,
a gameplay dependency from controllers, or a new command kind.

**The four named personalities are Step 22c**, split out here. Why is in
[the plan](../../.claude/plans/2026-09-10-dynamic-arenas-and-combat.md) and in § "What this step
deliberately does not do" below.

## What makes this step worth building

**This is the step that makes Step 22a's selection stage live.** Every shipped mode yields at most
one candidate today: `circles<>` reads a store holding exactly one `Hill`
(`hill_movement_system.cpp:158-161`) or exactly one `Zone` (`zone_shrink_system.cpp:112-117`), and
`race()` returns exactly one candidate on every `kReady` path
(`tactical_objective_candidates.cpp:82-123`). With a one-element span, `tactical_select_candidate`
returns index 0 regardless of score and **every objective weight is a common factor that changes no
outcome in production**. The opponent-derived provider is the first thing that puts two kinds in one
set. Until it exists, 22a's weights are proven only against synthetic unit-test candidate sets.

## The shove geometry

The target of a shove candidate is **S, the safe-side standing point** — never the opponent and
never the hazard. Write that in the header with the reason, because a later reader will "simplify"
it to the opponent's position and silently break the thing below.

```
S = O + unit(O - Hazard) * (r_self + r_opponent + margin)
```

**Why the target must be S.** If the target were the opponent or the hazard, `escape_blocked` would
be true for *every* shove candidate by construction — the hazard beyond it is the entire point — and
`tactical_candidate_score` would apply a constant `-(1 - risk_tolerance)` to all of them. A Bully
would then have to author a high `risk_tolerance` to score any shove above that penalty, and
`controllers_limits.hpp:108-117` defines `risk_tolerance` as the single knob that scales that
penalty away — so the same authored number that lets it shove would cancel its cliff caution on the
race gate. One profile could not be "aggressive toward opponents but careful about ledges", which is
exactly the Bully/Cautious-Racer distinction Step 22c needs. With S the ray `B -> S` continues *away*
from the hazard past S, so `escape_blocked` keeps its shipped meaning verbatim and one
`risk_tolerance` keeps one meaning.

**The badness direction costs zero terrain calls.** Take the nearest of these, all snapshot-visible
and all mode-independent, by smallest published distance under an explicit total order ending at a
stable id:

* `terrain.holes()` centres (`terrain_definition.hpp:104-105`), at most 32, pure arithmetic;
* on `kCorridors` ground, the outward normal from the nearest centreline via the canonical
  `corridor_project_to_centreline` the race provider already calls;
* the nearest entity carrying `LethalOnContact`, snapshot-visible by registration;
* `components<Hill>()`, for the plan's "or out of the hill" clause — a component store, not
  `mode_state`, so it needs no running schema.

**Do not use `disc_clearance`.** It is a nearest-unsupported query, so the earlier claim that none
exists is false — but it is the wrong tool twice. It scans every compiled boundary span, bounded by
`kMaximumTerrainBoundaryElementCount = 8192`, against `first_support_exit`'s walk over authored
primitives bounded by 32 + 32: roughly two orders of magnitude more expensive than the escape ray
the collector already pays per candidate. And `compile_terrain_boundary` pushes the arena envelope
as shape 0 with a `BoundaryFeatureId` carrying no `ShapeKind`, so a controller cannot tell a lethal
hole rim from the harmless outer wall — and outer-map routing cancels outward motion rather than
dropping a body, so the nearest boundary is frequently the one direction a shove accomplishes
nothing.

S then goes through the existing screening loop **unchanged**: `terrain_supports_point(S)` and
`first_support_exit(B -> S)`, the two calls every candidate already pays. This also settles the
first preflight's blocker 3: the screen does not delete a safe-side shove point. S is strictly
farther from the hazard than the opponent is, so it is supported by construction wherever ground
extends one standoff behind the opponent. What the screen does correctly delete is a shove *across*
a hole, because `B -> S` crosses void — the right answer, and it needs no pathfinder.

## Two providers

The shove provider runs **alongside** the mode provider. `collect_tactical_objective_candidates`
today selects exactly one provider from a three-row table by schema id and returns its single
disposition; that must change.

* **The mode provider owns the disposition.** Shove candidates merge only when it is `kReady`. State
  in the header what that decides, because it decides two cases silently otherwise: `race()` returns
  `kWaiting` for a bot with no `RaceProgress` yet — the state immediately after spawn — and
  `kFinished` for a finished racer, and the controller zeroes thrust in both. So a bot cannot shove
  between spawn and the race system attaching progress, and a finished racer is a stationary target
  for the rest of the match. That is the intended reading; say so rather than leaving it implied.
* **`kProviders` must split** into the schema-keyed table and one unconditional row, because a
  single `Registration` cannot say "runs in every schema".
* **`TacticalObjectivePolicy` must grow the shove keys**, since providers receive only the policy.
* **The mode/shove split is not a data boundary.** `circles<>` reads `components<Hill>()` and
  `components<Zone>()` and never touches `mode_state`; only `race()` reads it. The schema table is a
  policy gate. Do not write a header sentence claiming the mode provider gates data availability.

**The unsupported-schema throw is unreachable, and the reason lives two layers away.** A sandbox
world publishes `NoModeState{}`, whose schema matches no row, so
`collect_tactical_objective_candidates` throws `kTacticalModeUnsupported`; `ControllerHost` catches
it and `continue`s, and `TacticalController` assigns `state_` only after `decide_next` returns, so
`last_completed_tick` never advances and the bot would be inert *every* pass, not one. It cannot
happen: `application_config.cpp:29-37` refuses any profiled bot in a mode that does not accept
`kStartMatch`, and `sandbox_mode.hpp:87-92` does not. **Record that in the header.** The safety of a
`blob_controllers` throw is resting on a configuration-time rule enforced in `blob_application`,
which `blob_controllers` does not depend on and cannot see. Do not add a fourth provider row in this
step; do note that a `std::visit` over the closed variant would make the throw structurally
unreachable instead of conditionally so.

## Bounded work

* **Per-provider budget.** `require_candidate_count` applies per provider. The post-merge call
  currently at `tactical_objective_candidates.cpp:190` re-imposes the shared 32 on mode+shove
  combined and would defeat the rule — change it.
* **The ceiling is 4096, not 64.** `kMaximumLobbySeatCount = 64` is, in its own comment, "a *lobby*
  bound, not a roster bound"; every component store is bounded at `kMaximumEntityCount = 4096`. Add
  one sentence that the tighter operational ceiling is `kSnapshotEntityLimit = 1024`, already
  enforced by `match_startup_validation.cpp:60-103`, so a bot never scans more entities than a
  browser could be sent — the human/bot symmetry rule.
* **The nearest-N pre-filter must be a fixed-size insertion under a total order ending at
  `EntityId`.** `std::nth_element` and `std::partial_sort` give no reproducible order among equal
  elements, and this file's stated contract is that two toolchains select the same candidate bit for
  bit. Two opponents at identical squared distance would otherwise filter differently per toolchain.
* **N is a compile-time constant**, `kMaximumTacticalShoveCandidateCount` in
  `controllers_limits.hpp`, with `static_assert(N <= kMaximumTacticalObjectiveCandidateCount)`.
  Never a profile key: an authored N would move a derived ceiling the header says no authored input
  can move.
* **Use `simulation::for_each_entity_with_both`** (`component_join.hpp:48-60`, written explicitly
  for "an in-process bot's `Observation`") so `PhysicsBody` and `Controllable` are one allocation-free
  ascending pass, not an O(P*B) nested lookup.
* **Re-derive `kMaximumTacticalPredictionStepCount` to 96**, not 128:
  `kMaximumTacticalObjectiveCandidateCount * (kTacticalObjectiveProviderCount + 1)`. Mode provider
  at most 32 raw with one hill intercept each = 32; shove provider at most 32 raw with **zero**
  intercepts, because authored caution publishes no opponent extrapolation; one escape ray per
  screened candidate over the merged at most 64 = 64. **Do not count the hazard-direction lookup as
  a prediction step** — it is arithmetic over authored terrain, not extrapolation, and counting it
  would dilute what the counter names.
* Screened candidates rise from at most 32 to at most 64, so worst-case screening terrain work per
  bot per pass doubles. `run-benchmarks-linux` is dropped from the Verify line for good reasons
  (below) and **nothing in the gate observes that doubling**. Say so; do not let the per-provider
  budget read as free.

## The command surface

`Controller` declares exactly `request_body` and `request_thrust`, and Steps 18 and 19 deliberately
added neither ability, naming bot shield policy and charge timing as this step's. Add
`request_shield` and `request_charge` **as sibling helpers**, not as raw commands appended by the
behaviour — appending outside the helper would emit an ability on a pass where the thrust was
correctly suppressed.

Each must reproduce `request_thrust`'s suppression exactly: foreign observation, no seated entity,
missing `PhysicsBody`, `is_static()`, an active `Stun` containing the observed tick, and a
`Controllable` whose `controller_id` matches, with `input_generation` stamped from that observed
`Controllable`. Mind the payload asymmetry Step 21 was bitten by: `ShieldCommand` requires the
member and admits null where `ChargeCommand` omits it, and a present zero generation is a hard tick
failure the sink catches first.

`request_thrust` does **not** check match-running, tick zero, or a completed race course, though
`AbilitySystem` refuses all three. Matching that omission is defensible — a silent engine refusal
costs nothing — but make it a **written choice**, not an oversight, because a bot that keeps pulsing
a shield after finishing a race burns nothing and logs nothing while a reader of mailbox refusal
statistics sees a fault.

**At most one ability command per decision pass**, and locally suppress a pulse whose cooldown the
bot can already see — `Shield` and `Charge` both publish their windows verbatim. This is the honest
response to a real asymmetry rather than a second rate authority: the per-session bucket is capacity
30, refill 20/s, charged per inbound frame before parsing, and it lives on
`SessionWebSocketSession`. A bot goes `ControllerHost::decide_once -> CommandSink::submit` and never
enters `blob_server`, so **a bot pays no rate cost at all**, while a human emitting thrust + shield +
charge at 20 passes per second would drain 30 tokens in about 1.5 s and be disconnected — and the
human client further self-limits at 50 ms for thrust and 300 ms for abilities. The kind mask *is*
symmetric. Record the asymmetry in the README as a DoS control on an untrusted socket that an
in-process bot does not need, not as a gameplay symmetry.

The test helper `thrust()` does `REQUIRE(commands.size() == 1)` and backs every `require_go` and
`require_zero` in `tactical_controller_tests.cpp`. Today's fixtures publish no opponents so nothing
fires, but **any new case that emits an ability must not route through that helper.**

## Three new profile settings, and no `aggression` key

Add exactly three, each read by behaviour landing in this same commit, which is ADR 0008's legality
test. Append to `TacticalProfile::Section`; never interleave, because validation order decides which
key a multi-defect section blames.

1. **`objective_weight_shove_setup`**, finite [0,1]. Forced, not chosen: a fifth
   `TacticalObjectiveKind` cannot be added without it. The key-name and weight-lookup switches carry
   no `default` under `-Werror`, `tactical_objective_candidates.hpp` static-asserts the ordinal
   count, and `tactical_profile_tests.cpp` static-asserts `kWeightMembers.size()`.
2. **`charge_screen_diagonal_fraction`**, finite [0,1], **a fraction of the observed arena
   diagonal** — not world units. `Observation::terrain()` already hands the controller the validated
   bounds. An absolute world-unit scalar would mean two orders of magnitude of different things
   across configurations already in this tree, and 1.0 is a real ceiling ("a screen the size of the
   map is a refusal to ever charge") where `kMaximumWorldDimension = 1e9` bounds nothing.
3. **`shield_anticipation_ticks`**, integer committed ticks, bounded at
   `kMaximumTacticalShieldAnticipationTicks = kSimulationTicksPerSecond / 10 = 40`, with
   `static_assert(<= kMaximumTacticalPredictionHorizonTicks)`.

**There is no `aggression` key, and that is the design, not an omission.** ADR 0008 lists aggression
as a *concept* a profile configures, not a key name. Its preference half **is**
`objective_weight_shove_setup` under 22a's one-key-per-kind rule; its danger-appetite half is
precisely what `controllers_limits.hpp:108-117` forbids, having built `risk_tolerance` one-signed so
that "no value of it adds score to a dangerous candidate, because a knob that did would be Step
22b's aggression". Two knobs fighting over one term of `tactical_candidate_score` is the defect that
rule exists to prevent.

Because the key is never created, **both pinned rejection rows stay verbatim**: the
`ParserFailure{"aim_error=0.05", "aggression=0.05", kConfigurationKeyUnknown}` row and the fuzz
seed's `aggression=1` line. Note carefully: that is true of the two *rows*, not of the two *files* —
the fixture header still needs five edits for the new keys (both profile bodies, the required-field
table, and both expected `TacticalProfile` aggregates).

**The fuzz seed is not the guard, and 22a's review said otherwise.** `verify-fuzz-regressions`
replays each corpus member and asserts only that the target does not crash, and
`application_config_fuzzer.cpp:45-53` catches every typed loader error and returns 0 — a seed that
started being *accepted* would still pass. The live guard is the unit-test row above. Correct the
record; do not build on the wrong mechanism.

## Charge

**Screen by comparing exit time to contact time, never by `.has_value()`.** `first_support_exit`
returns `std::optional<MotionTime>` and `horizon_exits_support` currently discards the value. A
shove charge points at the hazard by construction, so a `has_value()` test is always true and would
**veto every shove the step exists to enable**. `MotionTime` exposes `value()` and a strong
ordering, so this is one exact comparison on a call the pass already makes:

```
charge iff no exit, or t_exit.value() > (|O - B| - r_self - r_opponent) / ray_length
```

— "I reach them before I run out of ground."

**The alignment gate must compare against the resultant, not the intent.** The burst is *additive*:
a body at 600 wu/s along +y charging +x leaves at (450, 600) — 750 wu/s, 53.1 degrees off the
commanded ray — so a ray cast due +x screens ground the body never crosses. Aim-versus-intent
certifies the wrong angle. The bot cannot compute the burst, so use the conservative published-state
form: **refuse when the component of current velocity perpendicular to the commanded direction
exceeds a shared fraction of the published ceiling.** At `|v_perp| >= ceiling` the resultant is at
least 45 degrees off for any burst up to the ceiling. This is a shared unconditional gate, not a
profile key — a key here would be an inert combat knob.

**What must stand beside the screen constant.** It screens what is *in* the corridor; it is never a
claim the body can stop before leaving it. Write the arithmetic out and name the drag each figure
assumes, because the frightening number belongs to the development configuration and not to the
game: at `config/blob-royale.cfg` (ceiling 600, accel 400, **drag 0**) an aligned charge from the
ceiling leaves at 1050 wu/s and needs `v^2/(2a) = 1378` wu to stop against a 1154 wu arena diagonal —
no ray makes it safe. From rest the same charge needs 253 wu. Under the **deployed** `drag 2.0` the
combined stop is 342 wu, 3.4x inside the arena. `ability_system.hpp` already warns that development
and deployment differ here and no comment may assume either. Add that only `charge_speed_fraction`
is unpublished, so the bot cannot compute its own post-burst speed even in principle — the screen is
the compensation, not the solution — and that `v^2/(2a)` for the *pre-burst* body is exactly
computable, since both `acceleration_world_units_per_second_squared` and
`normal_top_speed_world_units_per_second` are published in `match.movement.current`.

## Shield

**Name it as prediction, not as authored caution.** ADR 0008 authorises exactly this — "Perfect-shield
anticipation uses visible trajectories plus profile reaction/error, not a collision callback
available only to bots" — and the owner's line is drawn at *unpublished physics*, not at arithmetic
over published state. The owner decision says "a profile's combat **numbers** are authored caution":
the number is the window; the closing test is a bounded linear extrapolation of published position
and velocity. Calling that extrapolation "authored caution" would be exactly the dressing-up the
same section forbids.

State the bias and its direction: no drag term, because `drag_per_second` is unpublished, so the
prediction is biased **early** wherever drag is nonzero — +2.3% at 8 ticks, +8.5% at 32 and +21.7%
at 80 under the deployed 2.0, and +56% / +268% / +789% at the fixtures' 40, where total coast is
15 wu at 600 wu/s and the predictor is describing a body that has already stopped.

**Why the bound is 40 ticks**, written beside the constant: it reuses the one 100 ms precedent
already in that file (`kSpawnRequestRetryTicks`); it is two decision intervals at 20 snapshots per
second, past which the bot re-observes and corrects twice before the impact it is anticipating —
the same argument the file already makes for the 400-tick horizon; and it is the last window where
the deployed-drag error is a fraction rather than a multiple. Add that at drag 40 even 8 ticks is
+56%, so **no bound makes the predictor honest in every in-tree configuration** and the profile
author owns that, not the constant.

**The bound cannot be the perfect opening.** `blob_controllers` links only `blob_runtime` and
`blob_simulation`; `AbilityConfiguration` is in `blob_gameplay`, so `shield_perfect_window_seconds`
is unreachable, and writing 32 into `controllers_limits.hpp` would be a second authoring home for a
value an operator retunes in `[abilities]`. Say that a window longer than the mode's perfect opening
cannot produce a parry at all, and that keeping that relationship is the author's job because the
controller may not read the value.

**Cost the decision honestly in the header.** All three shield windows date from one activation, so
the next shield is admissible at `activation + max(160, 360) = 360` ticks — 0.9 s, 18 decision passes
— and active protection blocks the bot's own charge for 160 ticks. The payoff window is 32 ticks. A
bot must land inside it better than one time in eleven for a speculative shield to beat holding it,
and the ADR already concedes it cannot reliably do so. The shield is therefore a *defensive* pulse
first and a parry attempt only incidentally.

**Abilities sit after the same reaction gate as everything else, and that derates them.** With the
shipped `steady` profile's `reaction_delay_ticks = 80` against 20-tick decision spacing, four of
every five passes return `kAwaitingReaction`, so an ability has roughly a 20% duty cycle on top of
the 1-to-21-tick activation jitter. Do not invent a second, faster reflex path to hide it: ADR 0008
requires reaction to apply here in terms ("visible trajectories **plus profile reaction/error**").
Record the derating in the README.

**Abilities are decided on the pursuing path**, where a candidate is selected. Do not gate them on
the seek draw: that draw sits inside the not-arrived branch, so gating on it would mean a bot
standing on the hill can never raise a shield — precisely the state "defend a stable interior"
describes and precisely when an opponent's charge arrives — while adding a draw on the arrived
branch would consume randomness that does not exist today and move every authored profile's stream,
which `draw_count()` assertions exist to catch.

## The two browser pins

* **The tactical-movement spec pins `coaster` motionless to 1e-9** on position, velocity *and*
  acceleration, and a charge writes velocity with no acceleration — one charge in the sampled window
  fails the spec by about 1e9 times the tolerance. `objective_seek_probability=0` gates thrust only,
  so it is not protection. **Author the coaster's three new keys provably inert**:
  `objective_weight_shove_setup=0`, `charge_screen_diagonal_fraction=1.0` (a screen the size of the
  map), and `shield_anticipation_ticks=0` — which must be a legal authored answer meaning "never
  anticipates", mirroring `prediction_horizon_ticks=0`. Update that fixture's header comment, which
  claims the only difference between its two sections is seek probability.
* **The tactical-profiles spec asserts the exact published profile list** three ways. This step adds
  settings, not profiles, so the list does not move — but every authored section in both fixtures
  needs the three new keys or the server will not start.

## Everything that must gain the three keys

The family is closed within an instance, so an instance omitting a family key is `KEY_MISSING`.
Nine files, eleven sections, plus C++ construction sites the parser cannot see:

* `config/blob-royale.cfg` `[bot_profile.steady]`
* both `frontend-react/e2e/fixtures/*tactical*.cfg` sections (two files, four sections)
* all five `tests/fuzz/corpus/application/*tactical-profile*.cfg` seeds, each keeping the reason its
  name states
* `tests/unit/application/fixtures/tactical_profile_configuration_fixture.hpp` — both profile
  bodies, the required-field table, and **both expected `TacticalProfile` aggregates**

And the weight-vector construction sites, which compile silently and weight shove at zero because
the all-zero rejection only fires when *every* weight is zero: `tactical_profile_fixture.hpp`
`kEqualWeights`, both nested aggregates in `profiled_bot_reconciliation_fixture.hpp`,
`tactical_controller_tests.cpp`'s `section.objective_weights` assignment, and the designated forms
in the configuration fixture — a designated initializer value-initializes an omitted member too.
**22a burned a repair on exactly this class**, on sites that appeared in no inventory. Either give
`TacticalObjectiveWeights` a factory that cannot omit a member, or add a per-member positive-
authorship assertion to the fixture guard, so the next kind cannot repeat it.

## Documentation owed in this commit

* **ADR 0008 § "Tactical personalities without a class per mood"**, dated entry: "shield timing
  error" is replaced by the anticipation window; "aggression" and "charge appetite" are realized as
  `objective_weight_shove_setup` and `charge_screen_diagonal_fraction` rather than as keys of their
  own, with the reason.
* **ADR 0008 § "Owner decision"**, dated correction: "The perfect opening's *length* is never
  published" is **false**. Every `Shield` value is published verbatim — there is no
  `ComponentPublication<Shield>` specialization — so `perfect_expiry_tick - activation_tick` is
  observable the moment anyone in the room raises a shield. What is unpublished is the length
  *before* any shield exists. The derivation is symmetric, so a browser sees it too and no boundary
  weakens; but the sentence as written is wrong and this step's shield reasoning rests on it. Add
  that the two-sidedness the section promises ("often early or late") now comes from the 1-to-21-tick
  cadence jitter rather than from a signed timing-error key, since a window is a one-signed lead.
* **`src/application/README.md` and `src/controllers/README.md`** both state the inert-combat fuzz
  seed was "deliberately **not** migrated". `5b400cd` added six lines to it. The *mechanism* those
  READMEs give is right — the parser refuses an unknown key at the line carrying it, before the
  missing-key sweep — and 22a's review gave the wrong mechanism while getting the fact right. Fix
  both halves.

## Verification

```
./scripts/verify-focused 'unit.controllers|unit.gameplay|unit.application|fixtures'
./scripts/verify-focused 'unit.controllers|unit.gameplay|unit.application|fixtures' linux-clang-asan-ubsan
./scripts/verify-fuzz-regressions
./scripts/run-linux-toolchain -- ./scripts/verify-web
./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e
git diff --check
```

`run-benchmarks-linux` is dropped and that is now verified rather than inherited: no file under
`benchmarks/` contains a `[bot_profile]` section, and no replay fixture seats a tactical bot, so no
accepted golden and no benchmark hash can move. `unit.application` carries the configuration
fixture; `fixtures` carries `deployment_fixture_tests.cpp`, which parses the shipped configuration
and is the only C++ lane that sees `config/blob-royale.cfg`; `verify-fuzz-regressions` carries five
`[bot_profile]` documents; `verify-browser-e2e` is mandatory because both e2e fixtures author
sections and three new required keys stop both servers starting. `unit.gameplay` is inherited from
22a's shape rather than earned — nothing here touches gameplay — and is kept as cheap insurance.
`verify-web` likewise. Say both plainly rather than implying a dependency.

Prove: a shove candidate is produced, screened and selected on a published opponent; the target is S
and not the opponent; `escape_blocked` keeps one meaning; per-provider budgets hold and the merged
ceiling is 96; the nearest-N filter is stable across toolchains; a charge is refused when the exit
time precedes the contact time and taken when it does not — **both halves**, since a bot that never
charges would pass the refusal test alone; the perpendicular-velocity gate refuses a badly-aligned
burst; a shield is raised on a predicted close and not otherwise; at most one ability per pass; a
visible cooldown suppresses a pulse locally; every new reason code is reachable; and profile
differentiation attributable to the shove weight in a candidate set that now genuinely holds two
kinds — the first non-vacuous weight proof in the tree.

## What this step deliberately does not do

**The four named personalities are Step 22c.** Two independent preflight agents recommended the
split and root initially rejected it on two grounds, both of which failed against source and one of
which inverted. The plan's Step 22b Notes line is not Step 22b's: `git show 5b400cd` shows it as an
unchanged context line, the original Step 22 Notes carried under a new heading and never re-scoped —
and it is stale about two of its six clauses on 22a's own account. And "hold" already has a fixed
meaning in this library, stated twice: "Intercept and hold are the same computation. A bot already
inside a moving hill measures itself against the future centre, so it keeps station", and, older,
"Inside the hill the heading shrinks, which is what 'hold' means here." 22a shipped it. Reading
"hold" as "brake" substituted a new meaning for a defined word and then billed it to a stale line.

The brake is still owed — but by ADR 0008's **Keeper**, which is the deliverable being split out.
Three of the four names need mechanism, not numbers, and each carries its own design problem this
preflight surfaced and did not solve:

* **Keeper's brake** is a steering law, not a setting. Steering emits a bare unit direction, so
  `-v/|v|` at rest is 0/0; `clamp_controller_direction_component` passes NaN through deliberately
  and `Vector2::create` throws, `ControllerHost` catches, and `TacticalController` rolls state back
  without advancing `last_completed_tick` — **a bot that arrives at the hill and comes to rest is
  permanently inert**, which is the terminal state of every successful capture. It also needs a
  magnitude law and a deadband: at drag 40 a full-magnitude brake overshoots to -6.8 wu/s where
  coasting would have reached 1.1, so stability depends on the one number the bot may not read.
* **Opportunist's "prefer exposed"** cannot be a filter, which was root's second failed ground. With
  one kind the weight is a common factor that cannot reorder two candidates, so among admitted
  opponents the bot picks the nearest, not the most exposed — and a nearest-N pre-filter can discard
  the most exposed opponent for being (N+1)th nearest, so the two corrections cancel on exactly the
  profile they serve. It needs a per-candidate quality field and a score term for it. The raw
  material is published: `Stun`, `Shield`, `Charge`, `ZoneExposure` and `HillPresence` all reach the
  wire. (A filter *does* buy "abandon low-value fights": a vanished candidate fires `kLost`, which
  releases the lease and re-arms the reaction window. That half is verified and pinned by a test.)
* **Cautious Racer's "avoid expensive fights"** needs a zero weight to be a veto, and a veto as
  drafted is an **out-of-bounds read**: `tactical_select_candidate` returns `candidates.size()` for
  "no winner" and its one caller indexes without a size check, because the empty case is handled
  earlier. A veto makes a *non-empty* set produce no winner — a container-overflow abort on the
  sanitizer lane. It is reachable with a blessed profile today (`kMinimumObjectiveWeights` authors
  three zeros deliberately). The veto needs an explicit all-vetoed branch, its own reason code and a
  stated fallback; `kNoScreenedCandidate` would be a lie. Its clause 1, road clearance, is a clean
  promotion of the hardcoded `kDefaultRacerCautionFraction` to a per-profile key — the diagnostic
  `RacerController` already authors exactly that knob.

Also deliberately absent: no gate in the tree exercises bot combat prediction at the drag the
deployed game runs at. Both tactical browser fixtures author 40, `config/blob-royale.cfg` authors 0,
and the deployment authors 2.0. Every error figure in this contract's shield section is about 2.0
and no gate visits it. Recorded here for Step 23, which owns cross-mode production behaviour.
