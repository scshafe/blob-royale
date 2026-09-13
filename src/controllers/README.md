<!-- canonical: controllers_domain -- the bots, and where a new one goes -->

# Controllers domain

`blob_controllers` is where the **decisions** live. `blob_simulation` owns values and mechanism,
`blob_gameplay` owns the rules that turn that mechanism into a game, `blob_runtime` owns the clock
and the one path from a command source into a tick, and this library owns the in-process agents that
decide what to send down that path
(`docs/architecture/0002-simulation-architecture.md` § "Decision";
`docs/architecture/0004-gameplay-architecture.md` § "Controllers").

It depends on **`blob_runtime` and `blob_simulation` and nothing else**. No gameplay, no protocol,
no server, no observability, no Boost, no logging. A bot that wanted a mode's rules has confused
"what the game does" with "what I am trying to do", and a bot that wanted a logger has forgotten
that this library counts and the composition root logs.

## Controller is a role, not a base class

Three things fill the command-source role, and only two of them implement an interface:

| Filled by | Where | Implements `Controller` |
|---|---|---|
| A networked player's session | `blob_server` | no |
| An in-process bot | `blob_controllers` | yes |
| A scripted replay | `blob_controllers` | yes |

All three hold **exactly two capabilities**: `const SnapshotPublication&` to read and `CommandSink&`
to write. Nothing else. That is why the simulation cannot tell them apart: all three arrive at the
world as indistinguishable `Command` values in one `InputBatch`, the tick stores
`Controllable::controller_id`, and it never branches on it. `blob_server` does **not** depend on this
library and does not have to, because what is shared is the sink and the command vocabulary, not the
interface.

That symmetry is asserted rather than assumed:
`tests/unit/controllers/human_bot_symmetry_tests.cpp`.

## What is here

```
src/controllers/
  controller.hpp/.cpp                the role's form, its three command helpers, one spawn rule
  observation.hpp/.cpp               everything a controller may see, and the only thing
  controller_observation_queries.hpp/.cpp  canonical retained-snapshot player/component lookups
  controller_steering.hpp/.cpp        canonical target arithmetic and direction-component clamp
  controller_host.hpp/.cpp           one decision pass over the hosted roster
  controller_registry.hpp/.cpp       the closed map from a bot kind name to its factory
  controllers_limits.hpp             every bound this library's values enforce
  controllers_validation_error.hpp   the one `CONTROLLERS.*` exception vocabulary
  wanderer_controller.hpp/.cpp       a seeded random heading, held for a reaction delay
  chaser_controller.hpp/.cpp         thrust toward the nearest other controllable entity
  hill_seeker_controller.hpp/.cpp    thrust toward the hill's centre and hold there
  racer_controller.hpp/.cpp          seek ordered gates and recover toward the centreline
  tactical_controller.hpp/.cpp       one weighted decision pipeline for every authored profile
  tactical_profile.hpp/.cpp          thirteen validated active settings and bounded profile identity
  tactical_profile_catalogue.hpp/.cpp  immutable ordered configured profiles
  tactical_objective_candidates.hpp/.cpp  objective providers, screening, combat screens, selection
  tactical_seed_identity.hpp/.cpp     authored identity and domain-separated per-running seed
  creation_context.hpp               borrowed profile and authored identity during factory calls
  scripted_replay_controller.hpp/.cpp  a recorded command log, one step per pass
```

## Identity: `ControllerId` decides, `EntityId` is what it is currently driving

ADR 0004 § "Controllers" was written before plan Step 22 settled the identity split, and this library
resolves the two places that matters.

* A controller holds a **`ControllerId`** for its whole life. It is issued once by
  `CommandSink::open_session`, is never reused, and survives elimination, respawn, and the lobby
  wipe.
* `Observation` is built from that durable identity and **resolves the `EntityId` itself** from the
  snapshot's published `Controllable` store, which is the only place the two identity spaces meet.
  That is how a controller finds itself across a respawn with no lookup table anywhere.
* `Observation::entity()` and `Controller::entity()` are therefore `std::optional`. Absent means
  pending or eliminated, and both are **defined states**, not precondition violations: `decide` is
  still called and may return nothing or a spawn.
* `ControllerHost` decides in **ascending `ControllerId`** order rather than the ADR's sketched
  ascending `entity()`, because `entity()` is absent before a first spawn and changes on every
  respawn, so it is neither total nor stable, while `ControllerId` is both. It is also the key
  `InputBatch` already orders spawns by.

## Asking for a body

`Controller::request_thrust` is the shared authoring path for each fresh steering decision by the
four diagnostic bots and tactical. It requires the publicly observed owned dynamic body, suppresses
an observed active stun, and echoes the observed optional Controllable input generation. It
preserves the submitted direction verbatim and adds no normalization, random draw, retry, or
decision timer. Never-invalidated observations therefore produce the previous commands exactly.
The helper reads simulation values only, without depending on gameplay. The authoritative
gameplay lock still admits or ignores the command at its applied tick.

`controller_observation_queries` is the canonical retained-snapshot lookup surface. A published
player is the body/Controllable join, not every body; component lookup retains the component's
own domain. Returned pointers borrow the supplied snapshot, and temporary-snapshot calls are
deleted. `controller_steering` owns written target subtraction, product/square-root order, and
the source-side component clamp. Raw offsets deliberately are not bounded `Vector2` values:
two valid positions may differ by more than that type permits. These helpers add no normalization
or decision policy to the diagnostics; tactical explicitly normalizes its own go direction.

Scripted replay deliberately does not use this helper: typed logs carry their literal token or
absence, and old CSV fixtures remain unchanged. Neither the base `decide` wrapper nor the host
rewrites returned commands. Generation survives status expiry and same-entity body return, not
entity destruction; it is not a promise of invisible body-incarnation detection.

**`Controller` gained no shield capability at Step 18 and no charge capability at Step 19, and both
were held back until a bot had a policy to spend them on.** The base class deliberately held exactly
two — request a body, request thrust — because a `request_shield` before bot shield policy existed
would have been vocabulary ahead of behaviour, and because a bot that could charge but had no policy
for holes, hazards or the arena edge would have been a bot that could kill itself; the plan's words
for the second were "no knowingly suicidal charge". Step 22a built the decision pipeline and did
**not** change that. **Step 22b is the step that spends them**, adding both in the same commit as
the behaviour that calls them: see "The two ability helpers" below.

Human, bot and replay inputs shared one admission path throughout, which is why neither wait cost
anything provable. A `ShieldCommand` from any source reaches the same shared `ability` system and
the same phase, body, input-lock, generation, protection and cooldown checks; a `ChargeCommand`
reaches the same ones plus charge's own expired-cooldown, no-active-protection,
normalizable-direction and safety-envelope gates, and the same shield-wins-the-tie rule. Symmetry
was provable *before* either capability existed, because `ScriptedReplayController`'s typed log
already carries whole `simulation::Command` values, so a scripted controller emitted either with no
change in this domain; the replay `commands.csv` parser accepts a `shield` verb that uses
`entity_id` alone and leaves every other payload column empty, which is the legal never-invalidated
case, and learned a `charge` verb that **reuses `thrust`'s two existing direction columns** rather
than declaring a pair of its own, so no existing fixture grew a column. What the two verbs mean by
those cells differs and the difference is not this domain's: `thrust` clamps the magnitude and keeps
a subunit one, `charge` normalizes, so `1,0` and `2,0` are two different thrusts and the same
charge, and `0,0` is a legal thrust release and a refused charge. A bot cannot author a stronger
burst than a human can, for the same reason a client cannot — the strength never leaves the server.
The four diagnostic bots are unchanged by Step 22b, draw no differently, and observe shield and
charge exactly as any other reader does: through the published components.

A controller with no live entity that wants one returns a single `SpawnCommand` naming its own
`ControllerId` — the identical command a networked session sends after `welcome`. `Controller`
owns that decision for every bot (`Controller::request_body`), including *when it may ask again*:
at most once every `kSpawnRequestRetryTicks` committed ticks (100 ms at the fixed 400 Hz rate),
and immediately after a body it held is destroyed.

Both extremes are wrong. Asking once and never again strands a bot silently and permanently
whenever its spawn was refused. Asking every pass gives **one controller two bodies**, because a
request is in flight for at least one tick and a host deciding faster than the world publishes
would ask again while the engine was already seating it.

**This narrows a gap it cannot close, and the gap is not a controller's.** Nothing in the engine
refuses a spawn from a controller that already drives a body: `SpawnCommand` addresses a
`ControllerId`, `InputBatch` keeps at most one spawn per controller *per tick*, and phase 0 creates
an entity for each. A network client that sent two spawns in two ticks would get two blobs exactly
as a bot would. The rule that makes the second body impossible belongs in kernel phase 0 or in a
mode's `SpawnPolicy`.

## The two ability helpers

`Controller::request_shield` and `Controller::request_charge` are **siblings of `request_thrust`,
never raw commands a behaviour appends beside it**. All three now share one suppression, factored
into a single internal `commandable_controllable` rather than copied per helper: a foreign
observation, no seated entity, a missing or `is_static()` `PhysicsBody`, an observed `Stun` whose
window contains the observed tick, and a `Controllable` whose `controller_id` is this controller's.
The three stores are read in that one order for all three helpers, so the suppression cannot drift
by kind. A behaviour that built an ability command itself would emit one on exactly the pass where
its thrust was correctly suppressed — the stun pass, which is the pass an ability is most tempting
and least admissible.

The generation is **copied from that same observed `Controllable`, never constructed**. Absence is a
real value, an entity whose input has never been invalidated, while a *present zero* is a hard tick
failure `InputBatch::create` refuses before any system sees it, so the only safe source is the one
the world published. The payload asymmetry belongs to the commands and not to the helpers: a shield
is a bare pulse and a charge carries a direction, which is why only one of the two takes one.

**They deliberately do not check match-running, tick zero, or a completed race course**, which is
the omission `request_thrust` already makes and `AbilitySystem` already covers — it refuses all
three, and a refusal consumes no cooldown, queues nothing, throws nothing and emits no event.
Duplicating the three gates here would give one rule two homes, and the copy in this library could
not see the mode's input lock at all. That is a written choice rather than an oversight, because it
is not free: a bot that keeps pulsing a shield after finishing a race burns nothing and logs
nothing, while a reader of mailbox refusal statistics sees a fault.

**A hosted bot pays no command-rate cost, and that asymmetry is why tactical emits at most one
ability per decision pass.** The per-session token bucket is capacity 30, refill 20/s, charged per
inbound frame before parsing, and it lives on `SessionWebSocketSession`. A bot goes
`ControllerHost::decide_once -> CommandSink::submit` and never enters `blob_server`, so nothing
charges it; a human emitting thrust plus shield plus charge at twenty passes a second would drain
thirty tokens in about 1.5 s and be closed at 1008, and that human's client further self-limits at
50 ms for thrust and 300 ms for abilities. The command *kind* mask is symmetric — only rate is not.
**This is a denial-of-service control on an untrusted socket, not a gameplay symmetry**, and the
honest response is not a second rate authority inside this library but a controller that emits one
ability at most and declines a pulse whose cooldown it can already see. Recording it here rather
than tuning it away is deliberate; `tests/unit/controllers/human_bot_symmetry_tests.cpp` asserts the
capability symmetry, not a rate one.

## `ControllerHost`

Constructed from `const SnapshotPublication&` and `CommandSink&` and **nothing else**; there is no
overload, setter, or accessor through which a `GameSimulation&`, `GameWorld&`, or
`SimulationRuntime&` could arrive.

`decide_once()` acquires the latest snapshot **once**, builds one `Observation` per controller over
that same retained value, lets every controller decide in ascending `ControllerId`, and submits each
one's commands in the order it returned them before the next controller decides. One acquisition per
pass is what keeps two bots in one roster from deciding against two different worlds — a difference
no human player can observe, and therefore a break of the symmetry in the bots' favour.

**A controller that throws is isolated and counted**, exactly as a misbehaving network session is
refused rather than fatal. Its commands are discarded whole for that pass and the rest of the roster
decides normally; `failed_controller_count` rises and `last_failure()` names the controller, its
kind, its message, and the tick. **A submission the sink refuses is counted and the pass continues**
— the host never retries, because the mailbox supersedes by kind and identity so a retry is either
identical or already superseded. Nothing is swallowed: this library links no logger, so the host
counts and the composition root turns a rising count into a structured line.

## Adding a bot

```
new  src/controllers/<name>_controller.{hpp,cpp}         the algorithm and its validated settings
new  tests/unit/controllers/<name>_controller_tests.cpp  mirroring the source
edit src/controllers/controller_registry.hpp             one include and one row
edit src/controllers/CMakeLists.txt                      the new .cpp
edit match configuration                                 `[match] bots=`
do not touch                                             blob_simulation, blob_runtime,
                                                         blob_gameplay, blob_server, blob_protocol,
                                                         ControllerHost, any other bot
```

`@extension-point controller` — `controller_registry.hpp`. The table is `constexpr`, so two rows
claiming one name fail to compile rather than resolving to whichever was written first.

**Personalities are constructor configuration, not new types.** A cautious wanderer and a twitchy one
are two `WandererController::Personality` values; a timid chaser and a relentless one are two
`ChaserController::Personality` values; a shy seeker and a committed one are two
`HillSeekerController::Personality` values. Adding a class for a mood is the mistake this rule exists to
prevent, and `tests/unit/controllers/wanderer_controller_tests.cpp` and
`chaser_controller_tests.cpp` each assert that a tunable changes behavior with no new type.

Five registered algorithms: the four plain diagnostic kinds `wanderer`, `chaser`, `hill_seeker`,
and `racer`, followed by profiled `tactical`. `Registration::requires_profile` is the admission
metadata; application code reads the same row returned by `ControllerRegistry::find`, not a
parallel list of tactical kinds. The single `create` path takes a sink-issued controller ID, the
unchanged legacy room seed, and optional `CreationContext`. Plain rows reject either context
field. Tactical requires both a borrowed `TacticalProfile` pointer and
`TacticalSeedIdentity{raw match seed, lobby id, authored seat index}`; it copies both values and
retains no pointer. Missing or mismatched context is a named failure, never a default profile.

The unregistered implementation `scripted_replay` is absent on purpose: a registered kind is one a roster line may name,
and a scripted controller is meaningless without the recorded log no configuration line carries, so
a row for it would make `bots=scripted_replay:1` produce a bot that silently decides nothing.
Fixtures construct it directly.

`RacerController` resolves the race block's selected `road` identity against `Observation::terrain()`;
gates stay in the race block and its next gate comes from `race_progress`. It never selects the
first corridor or assumes a fixed name. Missing bindings fail visibly. The canonical simulation
centreline projection supplies the nearest target; controllers own no private geometry loop.
It waits before progress exists, seeks the next gate while centred, and turns
toward the nearest centreline point strictly beyond its personality's `caution_fraction` of the
half-width (default `0.75`, finite in `(0, 1]`). Exact nearest-segment ties keep authored order.
It waits while bodyless, leaving checkpoint return to the mode, and releases thrust after finishing.
This geometric policy uses no random draws; it retains the factory seed and repeats exactly for
the same observation and personality. It links no gameplay rules.

## Configured tactical profiles

`TacticalProfile` and `TacticalProfileCatalogue` are controller-owned immutable values.
`simulation::BotProfileName` is only their shared bounded identity: nonempty lower snake case,
at most 64 bytes. A catalogue may be empty or contain at most 16 unique names in authored order.
`profiles()` exposes a const span; `find(name)` returns a borrowed pointer or explicit absence.
Another personality is another value, never another tactical class or registry row.

The application's strict section-family parser accepts **thirteen active settings, authored as
seventeen required keys**. Step 15 shipped the first four; Step 22a added the three the decision
pipeline reads; Step 22b added the two combat settings and the fifth objective weight the shove kind
forces into existence; Step 22c added the four the named personalities need. Every one of them is
read by behaviour landing in the same commit that adds the key, which is ADR 0008's legality test
for a profile setting. The five `objective_weight_*` keys are one setting — the per-kind weight
vector — authored one key per name rather than one positional list, so a `.cfg` a human reads names
the kind it is weighting and an omitted one is the parser's own `KEY_MISSING` naming that key.

| Setting | Accepted values | Meaning |
|---|---|---|
| `objective_seek_probability` | finite `0..1` | Probability of go at a due eligible decision |
| `reaction_delay_ticks` | integer `0..4000` | Delay from observation to the next decision |
| `aim_error` | finite `0..0.25` | Bounded perpendicular-to-forward aim perturbation |
| `target_persistence_ticks` | integer `0..4000` | Lifetime of a still-eligible objective key |
| `objective_weight_hill` | finite `0..1` | Preference for a published hill |
| `objective_weight_zone` | finite `0..1` | Preference for a published royale zone |
| `objective_weight_race_gate` | finite `0..1` | Preference for the next race checkpoint |
| `objective_weight_race_recovery` | finite `0..1` | Preference for the centreline recovery point |
| `objective_weight_shove_setup` | finite `0..1` | Preference for a shove setup behind an opponent |
| `risk_tolerance` | finite `0..1` | Fraction of a failed escape screen's penalty ignored |
| `prediction_horizon_ticks` | integer `0..400` | Ticks escape screening and intercept look ahead |
| `charge_screen_diagonal_fraction` | finite `0..1` | Charge screen ray length, in arena diagonals |
| `shield_anticipation_ticks` | integer `0..40` | Ticks of lead on a predicted closing contact |
| `road_caution_fraction` | finite **`(0..1]`** | Race recovery threshold, in road half-widths |
| `arrival_brake_fraction` | finite `0..1` | Share of the relative-velocity brake requested on arrival; reversal is permitted |
| `exposure_preference` | finite `0..1` | How far a shove candidate's opening may scale its preference |
| `minimum_opening` | finite `0..1` | Opening below which a fight is abandoned rather than held |

**`road_caution_fraction` is the one fraction in the family whose zero is refused, and the asymmetry
is deliberate rather than an inconsistency to tidy up.** The race provider recovers toward the
centreline when `nearest.distance > fraction * road->half_width()`, so a zero does not switch
recovery off — it recovers unless the body sits exactly on the line, which *inverts* race behaviour.
A knob whose zero means the opposite of "off" has no legal zero, so this key adopts
`RacerController`'s own domain (`kMinimumRacerCautionFraction` exclusive through
`kMaximumRacerCautionFraction` inclusive) rather than declaring a second pair beside it: it is the
same knob against the same published half-width, and two domains for one meaning is where the two
silently drift apart. Strict positivity buys a second thing the other twelve settings get for free.
Four positional `TacticalProfile::Section` construction sites value-initialize a new trailing
`double` to `0.0` and still compile — `AuthoredObjectiveWeight`'s deleted default constructor
protects the weight vector and nothing protects a bare `double` — so **for this key a missed
construction site is a loud `TacticalProfile::create` throw instead of a silently inverted racer.**

The five weight keys are declared in `TacticalObjectiveKind` ordinal order — hill, zone, race gate,
race recovery, shove setup — in `kConfigFamilyFieldSpecs`, in `TacticalProfile::Section`, and in the
order `TacticalProfile::create` validates, so a section with two bad weights blames the same key at
every layer. `tactical_objective_weight_key` is the one spelling of each name: the configuration
parser's key list reads it rather than repeating the literal, so a key cannot drift between the
schema and the diagnostic that names it.

For example, append this section to a complete application configuration and select
`bots=tactical@steady:1` under `[match]`:

```ini
[bot_profile.steady]
objective_seek_probability=1
reaction_delay_ticks=80
aim_error=0.05
target_persistence_ticks=400
objective_weight_hill=1
objective_weight_zone=1
objective_weight_race_gate=1
objective_weight_race_recovery=1
objective_weight_shove_setup=1
risk_tolerance=0.5
prediction_horizon_ticks=80
charge_screen_diagonal_fraction=0.25
shield_anticipation_ticks=24
road_caution_fraction=0.75
arrival_brake_fraction=0
exposure_preference=0
minimum_opening=0
```

These are authored example values, not defaults. Missing, repeated, unknown, malformed, nonfinite,
or out-of-range input fails at startup; there is no clamping or inert combat setting.

Ten settings-level rejections are the pipeline's and the two combat steps', all
`CONTROLLERS.TACTICAL_PROFILE_*`: `OBJECTIVE_WEIGHT_INVALID`, `RISK_TOLERANCE_INVALID`,
`PREDICTION_HORIZON_INVALID`, `CHARGE_SCREEN_INVALID`, `SHIELD_ANTICIPATION_INVALID`,
`ROAD_CAUTION_INVALID`, `ARRIVAL_BRAKE_INVALID`, `EXPOSURE_PREFERENCE_INVALID` and
`MINIMUM_OPENING_INVALID` name the key that failed, and `OBJECTIVE_WEIGHTS_DEGENERATE` names the
*section*, because no single key is at fault. `ROAD_CAUTION_INVALID` is the only one of the nine
key-named codes with a half-open domain, for the reason given above. The degenerate-weights code is
the only rejection in this library of a combination whose every value is individually legal: a
single zero weight is a real authored answer — "this profile does not care about that objective" —
but **all five at zero is not**, because it makes selection inexpressive, collapsing every score
onto the stable kind ordinal, which is a bot deciding by tie-break. A profile
with genuinely no preference authors equal *positive* weights, which keeps distance ordering; that
is what `config/blob-royale.cfg` ships.

**That rule used to carry a second job it was never equal to, and Step 22b took it away.** It was
also the only thing standing between a C++ construction site that omitted a weight and a bot that
silently ignored an objective, because C++ zero-fills an omitted aggregate or designated initializer
rather than refusing to compile — but it fires only when *every* weight is zero, so an omission
sitting beside four authored numbers passed straight through it, and Step 22a burned a repair on
exactly that. The hole is now closed at the site instead: `TacticalObjectiveWeights` holds
`AuthoredObjectiveWeight` members, a one-value type with a **deleted default constructor**, so an
omitted member is a build failure naming the site that made it. That is the same discipline the
no-`default` switches apply to the kind enum — the compiler closes the set, not a comment.

`risk_tolerance` is not an aggression knob under another name, and the bound is written to keep the
two apart: it only ever scales a penalty *away*, so no value of it can add score to a dangerous
candidate. `prediction_horizon_ticks` is bounded at one second of committed time rather than the
ten the other tick settings allow, because a hosted bot re-decides roughly twenty times a second
and a horizon reaching past twenty of its own future decisions would be a planner's.

**There is still no `aggression` key, and after Step 22b that is settled rather than deferred.** ADR
0008 lists aggression as a *concept* a profile configures, not a key name, and the concept is now
fully spent: its preference half **is** `objective_weight_shove_setup` under the one-key-per-kind
rule above, and its danger-appetite half is precisely what `risk_tolerance`'s one-signedness
forbids. Two authored numbers fighting over one term of `tactical_candidate_score` is the defect
that rule exists to prevent. Charge appetite is likewise `charge_screen_diagonal_fraction`, inverted
— a profile authoring a short screen charges more often — and shield timing error is
`shield_anticipation_ticks`, a one-signed *lead* rather than a signed error.

The two combat bounds carry their reasons in `controllers_limits.hpp`.
`charge_screen_diagonal_fraction` is a fraction of the observed arena diagonal and never world
units, because `Observation::terrain()` already hands the controller validated bounds while an
absolute scalar would mean two orders of magnitude of different things across the configurations in
this tree; **both of its ends are real answers, and they run opposite to the horizon's zero** — one
is the strictest screen a profile can author, since a longer ray can only find more ground endings,
and zero is the permissive end, a screen that examines nothing and therefore refuses nothing.
`shield_anticipation_ticks` is bounded at 40, which is 100 ms: it reuses the one 100 ms precedent
already in that file, `kSpawnRequestRetryTicks`; it is two decision intervals, so the bot
re-observes and corrects twice before the impact it anticipates; and it is the last window in which
the deployed drag error is a fraction rather than a multiple. Its zero means "never anticipates",
mirroring `prediction_horizon_ticks=0`.

Plain `kind:count` retains its meaning and must omit a profile. Profiled selection requires a
configured name. The application retains this catalogue and derives one immutable `NpcCatalogue`
shared by runtime and session admission. Only real configured choices are advertised as
`npc_profiles`; tactical is never a bare `npc_controller_kinds` choice. Lobby seats retain the full
kind/profile declaration through pending, occupied, and vacated states. The reconciler guards queued
joins with that declaration and immediately retires work for a replaced declaration.

Profiled startup rosters require the selected mode's actual `StartMatch` capability, available in
hill, race, and royale. Sandbox has no stable authored-seat identity, so it rejects those rosters
and advertises no profiled choices. Unused profile sections may still be configured there.

## The four named personalities

ADR 0008 § "Tactical personalities without a class per mood" names four. **Step 22c ships them as
authored sections of `config/blob-royale.cfg` and as nothing else.** A branch keyed on a profile's
*numeric value*, or on a *candidate kind*, is permitted; a branch keyed on a profile's **identity**
is forbidden, and there is none. Five profiles now ship — `steady` is unchanged and remains the
neutral reference, authored at whichever end of each new key reproduces its previous behaviour.

| Profile | What it is | The settings that make it that |
|---|---|---|
| `keeper` | Seeks a hill and brakes within its full radius; never hunts | `objective_weight_hill=1`, **`objective_weight_shove_setup=0`** (the one endorsed zero — it skips the opponent provider), **`arrival_brake_fraction=1.0`**, `minimum_opening=1.0` |
| `bully` | Fights whoever is nearest | `objective_weight_shove_setup=1`, `exposure_preference=0` (nearest, not most exposed), `charge_screen_diagonal_fraction=0.25`, `minimum_opening=0` |
| `opportunist` | The fastest reflexes, and the only profile that scores exposure | `reaction_delay_ticks=20`, **`exposure_preference=1.0`**, **`minimum_opening=0.5`**, `objective_weight_shove_setup=0.75` |
| `cautious_racer` | Lowest risk tolerance; avoids *expensive* fights, not all of them | `risk_tolerance=0.125`, `objective_weight_race_gate=1`, `road_caution_fraction=0.6`, a deliberately **non-zero** `objective_weight_shove_setup=0.125` with `minimum_opening=0.75` |

**Cautious Racer's shove weight is positive on purpose, and it is the reason there is no zero-weight
veto anywhere in the selection stage.** Its ADR clause is "avoid *expensive* fights", not "never
fight", so it authors a small positive preference and lets `minimum_opening` decide which fights are
cheap. Only Keeper wants none at all, and Keeper gets that from a provider skip rather than from a
veto — see "The shove provider" below.

**Bully needed no new mechanism.** It is numbers alone over Step 22b's shipped behaviours, which is
what makes it the control against which the other three's mechanisms are read. Its
`charge_screen_diagonal_fraction=0.25` screens about 288 wu of the shipped map's 1154 wu diagonal
for forward clearance. It does not establish stopping or recovery; ADR 0008 records the
acceptable recovery path as deferred.
And Cautious Racer's `road_caution_fraction=0.6` sits above a real floor at 0.5714, the shipped
`[race] checkpoint_radius_world_units` over the 70 wu half-width of `maps/circuit-960x640`: below
it the recovery threshold falls *inside* the gate radius and pulls a bot off a gate it is standing
in.

**Two authoring rules every shipped vector keeps, and no bound in this library can enforce either.**
A profile's race-recovery weight is never below its race-gate weight, because a profile preferring
the gate steers at it from off the road and never comes back. And no profile authors a zero weight
on a kind its running mode produces, because that kind's preference term is then zero on every
candidate the mode can offer and the bot ranks its only objective by tie-break — the inexpressiveness
`OBJECTIVE_WEIGHTS_DEGENERATE` refuses for the whole set, arrived at one kind at a time.
`objective_weight_shove_setup` is the single exemption. A bound cannot see which mode a profile will
be seated in, so both rules are the author's and `config/blob-royale.cfg` states them where the
sections are.

**A name is not inert, which is why the differentiation proof holds it constant.**
`tactical_seed_for` mixes a profile name's length and every one of its bytes, so two differently
*named* profiles already draw and steer differently before a single setting is consulted. A test
showing that `keeper` and `bully` behave differently would therefore prove nothing about their
numbers. The proof that counts holds the name fixed and varies only the vector.

## Tactical objectives and observation timing

`tactical_objective_candidates` has **two provider tables**: one closed table keyed by public
mode-state schema ID, of which exactly one row runs per pass, and one table of unconditional
providers that run beside it under every schema. Hill and zone providers read published centers and
radii, and the hill provider additionally carries the hill's published velocity forward on the
candidate for the arrival brake to read. Race reads the exact published road binding and next
checkpoint, using canonical centreline recovery strictly beyond the **profile's own**
`road_caution_fraction` — Step 22c promoted that threshold off the shared
`kDefaultRacerCautionFraction`, which is why `race()` no longer carries `[[maybe_unused]]` on its
policy parameter and why `TacticalObjectivePolicy` is now every number a provider *or* a screen
reads rather than only the combat screens' argument. Missing progress waits;
finished progress coasts; invalid binding, progress, or geometry fails visibly. No provider reads
private schedules, future ticks, or gameplay code. Candidate count is bounded at 32 **per provider**
before filtering, and unsupported running schemas fail.

**The mode provider owns the disposition, and shove candidates merge only when it is `kReady`.**
That rule decides two cases that would otherwise be decided silently, so it is stated rather than
implied: `race()` returns `kWaiting` for a bot carrying no `RaceProgress` yet — the state
immediately after spawn — and `kFinished` for a racer that has taken its last gate, and the
controller zeroes thrust in both. So a bot cannot shove between its spawn and the race system
attaching progress, and **a finished racer is a stationary target for the rest of the match**. That
is the intended reading of "the mode owns the disposition", not an oversight of it.

**The unconditional provider is a second table rather than a fourth row, because one `Registration`
cannot say "runs in every schema".** A row is matched on a schema id, and there is no spelling of
"all of them" that is not a sentinel a later reader has to be told about. The split is a *policy*
gate and not a data boundary, and the difference matters because the schema table looks like one:
`circles<>` reads `components<Hill>()` and `components<Zone>()` and never touches `mode_state`, and
only `race()` reads `mode_state` at all. The running schema gates which objective a bot pursues,
never which data it may see; the shove provider reads the same published stores under every schema.

**The unsupported-schema throw is unreachable, and the reason lives two layers away.** A sandbox
world publishes `NoModeState{}`, whose schema matches no row, so the collector throws
`CONTROLLERS.TACTICAL_MODE_UNSUPPORTED`; `ControllerHost` catches it and continues, and
`TacticalController` assigns its state only after `decide_next` returns, so `last_completed_tick`
never advances and such a bot would be inert on *every* pass rather than one. It cannot happen:
`application_config.cpp` refuses any profiled bot in a mode that does not accept `kStartMatch`, and
sandbox does not accept it. **That safety rests on a configuration-time rule enforced in
`blob_application`, which this library neither depends on nor can see**, which is why it is written
down here. A `std::visit` over the closed `ModeMatchState` variant would make the throw structurally
unreachable instead of conditionally so; that is a change to the extension point's shape and Step
22b deliberately does not make it.

The target point and straight center segment must pass simulation's `terrain_supports_point` and
`first_support_exit`; numerical failures propagate. This screening is not pathfinding and promises
nothing about momentum, perturbed aim, moving terrain, body-radius clearance, hazards, or combat.
An unexpired lease retains the same eligible key and refreshes its public target without renewing
its acquisition time. Removal, ineligibility, or a changed gate cancels the lease immediately.

**Escape screening** is the second screen and it *ranks down* rather than deletes. For each
candidate that survived the terrain screen, the collector casts one ray from the body along the
approach direction, of length `normal_top_speed × prediction_horizon_ticks × fixed delta` clamped to
the arena diagonal, and asks the same canonical `simulation::first_support_exit` the screen beside
it already calls. **There is no second support predicate anywhere in this library**, and adding one
would break the single-geometry-owner rule `terrain_queries.hpp` states. A candidate whose approach
already left supported ground before the target was deleted by the first screen, so this ray can
only find the end of support *beyond* the target: what it answers is "if I keep going at the room's
published top speed for my horizon, do I run out of ground?". A yes sets `escape_blocked`, which the
score penalises — it does not remove the candidate, because removal would be one more way for the
collector to return nothing and the step requires a decision when every candidate screens badly,
not silence. Top speed is a
published session fact a browser client reads too, so this costs the human/bot symmetry nothing;
drag is not published and is deliberately not modelled, and omitting it overstates travel, which for
a safety screen is the conservative direction.

**Hill intercept and hold** is the one prediction in this domain that needs nothing unpublished, and
that is worth saying out loud because every other prediction this domain might want *does*.
`ComponentPublication<HillMotion>` strips the private retarget schedule and publishes the hill's
**committed velocity**; `hill_movement` integrates that velocity with no drag term; and the hill
entity owns no `PhysicsBody`, so it is never dragged, never accelerated by contact, and never capped
by a speed limit. Its future centre is therefore exactly `center + velocity × horizon × fixed
delta`, closed form, no unpublished term. Contrast the three predictions Step 22b wanted: an
opponent's future position needs `drag_per_second`, which lives on `SimulationConfig` and reaches no
snapshot; a charge needs `charge_speed_fraction`; a parry needs `shield_perfect_window_seconds`.
None of the three became a wire field. The shield closing test extrapolates an opponent anyway and
**publishes its own one-signed early bias** for doing so, which is a different claim from this one:
the hill's intercept is exact, and that one is honest about not being.
Intercept and hold are the same computation — a bot already inside a moving hill measures itself
against the future centre and so keeps station instead of arriving where the hill used to be. A hill
with no published `HillMotion`, or a zero horizon, returns the centre unchanged, so every
stationary-hill behaviour is bit-identical to Step 15's. An extrapolated point outside the
representable `Vector2` domain returns the centre unchanged rather than throwing: a throw here would
be isolated by the host and would silently stop the bot for the pass.

The intercept is computed in the provider, so the point terrain screening and escape screening see
**is** the predicted one: a hill about to roam over a hole is screened out on where it is going, not
on where it is. The lease refresh picks up the recomputed intercept on every pass without renewing
the acquisition window, which is what makes hold work — the target moves under a held lease.

The first eligible running dynamic-body observation, body/generation change, and stun recovery
start reaction timing from the actual observed tick. The timer starts even when no objective is
available; repeated empty observations do not restart it. A lost lease restarts reaction timing,
with zero delay permitting immediate reconsideration. Reaction and persistence use checked absolute
`TickWindow` values. Due decisions schedule from the observed tick, never replay missed decisions.
Waiting retains valid current intent; cancellation emits explicit zero when public body/generation
admission permits. Bodyless Controllable waits, while no entity uses the shared spawn-request rule.
Non-running observations clear tactical work and coast where a dynamic body exists. Active stun
does no decision/RNG work, and generation change also cancels work across an entirely missed stun.

Tactical go has unit-strength direction and coast is explicit zero — with one exception Step 22c
adds and the section below states: a profile that authored an arrival brake emits a **subunit**
direction on the arrived branch, which is a magnitude the wire and the tick already admit. Distance
and seek probability never scale acceleration. Each due, valid, non-arrived target consumes one seek
draw, including at probability zero or one; go is chosen only when
`draw < objective_seek_probability`. Go consumes one additional aim draw even for zero error. It
normalizes the target offset, computes
`e = ((draw * 2) - 1) * aim_error`, then `(ux - e*uy, uy + e*ux)`, and normalizes that pair with
written square-root arithmetic and the canonical component clamp. No trigonometry is used. Missing,
invalid, or arrived targets, bodyless state, non-running phases, and active stun consume no draws.

## The arrival brake

Arrival used to be one thing — coast, an explicit zero thrust. A profile authoring
`arrival_brake_fraction > 0` gets a second thing on that same branch: **a relative-velocity brake
that permits reversal.**

```
brake = clamp_componentwise( -v_relative / (published_acceleration * hold_seconds)
                             * arrival_brake_fraction )
```

**Zero reproduces Step 22b's coast bit for bit**, and the branch tests the fraction before it
computes anything, so a non-braking profile takes the identical `(0, 0)` path it took before. That
is what keeps every shipped `kArrived` assertion and both browser fixtures' motionless pins
unchanged — the tactical-movement spec pins `coaster` to 1e-9 on position, velocity *and*
acceleration — and it is why all four e2e sections author the key at zero explicitly: **the arrived
branch sits *outside* the seek draw, so `objective_seek_probability=0` is no protection against a brake** —
exactly what Step 22b found for the charge, one branch over.

**No division by `|v|`, therefore no `0/0`, no NaN, and no deadband.** The velocity *vector* is
divided componentwise by a positive scalar, so as `v_relative` goes to zero the commanded direction
goes smoothly to `(0, 0)` — which is precisely the exact zero an at-rest arrival must emit and
precisely what the existing arrived assertions already demand. That matters more than it reads: a
NaN here would not be a wrong number, it would be a **permanently inert bot**. `Vector2::create`
refuses a non-finite component, `ControllerHost` catches the throw, and `TacticalController` assigns
its state only after `decide_next` returns, so `last_completed_tick` never advances and the same
throwing pass repeats forever — in the terminal state of every successful hill capture. A deadband
must not become a profile key for the same reason
`kTacticalChargeAlignmentPerpendicularFraction` is not one: it would be a knob whose only effect is
letting a profile switch a safety property off. The two other routes to a zero divisor are closed
the same way and never with an epsilon — `hold_seconds` is strictly positive by construction, and a
published acceleration of zero, which `simulation_limits.hpp` admits, takes the coast, because a
body that cannot thrust has no brake to spend.

**Velocity reversal is permitted within a conditional velocity envelope.** For a stationary
selected hill, continued arrival, constant movement tuning, established regular observation spacing,
next-tick command application, no external forces, and an inactive propulsion limiter, each velocity
component stays within its magnitude at the decision, subject to the simulation's numerical
tolerance. This bounds speed during a hold, not stopping distance or retention inside the hill.
Moving hills, retargeting, live tuning, missed observations, delayed commands, and the first
zero-delay decision are outside that envelope. The assumptions and proof belong to
[`arrival-brake-contract.md`](../../docs/reviews/2026-09-12-arrival-brake-contract.md).

**`hold_seconds` is a denominator estimate, not the scheduled command duration.** With established
regular observation spacing S and reaction delay R, the estimate is `max(R, S)` ticks; the next
decision instead arrives after `ceil(R / S) * S` ticks, or S when R is zero. Past spacing cannot
bound future runtime scheduling. A first zero-delay decision has no previous observation and uses
one tick, so its command can amplify velocity before replacement even under subsequently regular
observations. Repeated qualifying brake holds contract in the ideal arithmetic model with positive
acceleration and brake fraction; exact-zero stopping and universal binary64 convergence are not
promised. Production-loop cases evaluate numerical settling under the contract's explicit envelope.

**Calibrating against the published top speed was rejected on real numbers.** The drafted
`min(1, |v_relative| / normal_top_speed)` measures against a ceiling the body cannot reach: the
reachable one is `min(V, A / D)` and `D` is unpublished, so at both browser fixtures' published
ceiling of 10000 with acceleration 400 and drag 40 the terminal speed is 9 wu/s and that form would
ask for a brake three orders of magnitude too small — inert in exactly the configuration whose
instability motivates a brake at all.

**A subunit thrust is a real command and already ships**, so the brake needs no new command shape,
no wire change and no kernel seam. `normalized_thrust_intent` is a magnitude *clamp* and not a
normaliser — its own header says so in bold — the held intent is re-scaled by the tuning's
acceleration every tick and never re-normalised, the wire bound is per-component, and
`ChaserController` already emits `unit * aggression_weight`.

**`kHill` only — and the zone is the reason, not computability.** "Relative to the objective's
motion" is perfectly computable for `kShoveSetup`: that key's subject is the opponent's `EntityId`
and `tactical_shove_opponent_body` already resolves the body. That kind is **deferred**, not
impossible. What rules out the zone is its arrival radius: a `kZone` candidate's arrival radius is
the zone's own radius, a full zone radius is the arena half-diagonal, and the checked-in
`config/blob-royale.cfg` authors `mode=royale` — so **every bot in the shipped configuration is
inside its zone's arrival radius from the first running tick**, and a zone brake would be a
permanent parking brake on all of them. A gate and a recovery point are authored terrain with no
motion to be relative to.

**The hill's motion is read off the candidate and never rebuilt from `key.subject`.** The provider
already looked the published `HillMotion` up for the intercept, so it writes the velocity onto the
candidate whether or not the horizon predicts over it, and the brake reads it there. Rebuilding an
`EntityId` from a key subject would reproduce the permanently-inert failure one layer up:
`kMinimumEntityId` is 1, so a race checkpoint index is a perfectly legal `EntityId` naming some
foreign entity, and `EntityId::create` *throws* outside its range. `tactical_shove_opponent_body`
refuses the same rebuild for the same reason.

**A braking arrival stays `kArrived` and gains no reason code of its own.** A reason names the
branch a decision came out of, and this is one branch — the arrival test is unchanged and the brake
decides only what thrust that branch emits. A second value would be reachable only by a profile that
authored a positive fraction, which makes it a reason keyed on a *personality* rather than on a
branch; and at rest the two are indistinguishable by construction, because a brake with nothing left
to null emits exactly the coast's `(0, 0)`.

## The shove provider

**A zero `objective_weight_shove_setup` skips this provider outright, and that is a provider-level
skip and not a selection-level veto.** The difference is the whole of why it is safe. The profile
produces *no* shove candidate rather than an unselectable one, so there is no all-vetoed case to
invent a reason code for, no zero-thrust fallback branch, and **no change at all to what a zero
weight means for the four mode kinds** — there it still means "I do not care about that objective",
scored at zero and outranked, exactly as before. It also saves the entire nearest-N opponent scan
and every hazard walk behind it for a profile that could never have acted on the result. A general
zero-weight veto in the selection stage would have bought none of that and would have had to answer
"what does a bot do when everything is vetoed?", a question this pipeline does not otherwise have.
The comparison is exact rather than epsilon'd because the weight is an authored, validated number
and never a computed one. Keeper is the one shipped profile that authors it.

The unconditional provider turns each nearby opponent into one `kShoveSetup` candidate, and **its
target is S, the safe-side standing point — never the opponent and never the hazard**:

```
S = O + unit(O - Hazard) * (r_self + r_opponent + margin)
```

**A later reader will want to "simplify" S to the opponent's position. It breaks two things at
once.** If the target were the opponent, or the hazard beyond it, `escape_blocked` would be true for
*every* shove candidate by construction — the hazard past the target is the entire point of the
objective — and `tactical_candidate_score` would subtract a constant `(1 - risk_tolerance)` from all
of them. A shove-preferring profile would then have to author a high `risk_tolerance` to score any
shove above that penalty, and `risk_tolerance` is defined as the single knob that scales that
penalty away, so **the same authored number that let it shove would cancel its cliff caution on the
race gate**. One profile could not be aggressive toward opponents and careful about ledges at once.
With S the ray `B -> S` continues *away* from the hazard past S, so `escape_blocked` keeps its
shipped meaning verbatim and one `risk_tolerance` keeps one meaning.

S then goes through the existing screening loop **unchanged** — `terrain_supports_point(S)` and
`first_support_exit(B -> S)`, the two calls every candidate already pays. The screen does not delete
a safe-side shove point: S is strictly farther from the hazard than the opponent is, so wherever
ground extends one standoff behind the opponent it is supported by construction. What the screen
does correctly delete is a shove *across* a hole, because `B -> S` then crosses void — the right
answer, and it needs no pathfinder. The `margin` is `kTacticalShoveStandoffDiagonalFraction` of the
published arena diagonal and is strictly positive for a reason: `PhysicsBody::kUndeclaredRadius` is
zero, so with no margin the standing point of two such bodies would collapse exactly onto the
opponent. Arrival is measured against that margin rather than the standoff, because a bot one margin
from S on the opponent's side is exactly `r_self + r_opponent` away, which is contact.

**The badness direction costs no terrain query.** The hazard is the nearest of four
snapshot-visible, mode-independent things, chosen under an explicit total order — smallest published
distance, then a source ordinal, then the source's own stable id, never a store position or a
discovery order: an authored `terrain.holes()` centre; on `kCorridors` ground the outward normal
from the nearest centreline, via the canonical `corridor_project_to_centreline` the race provider
already calls; the nearest entity carrying `LethalOnContact`; and a published `Hill` the opponent is
standing *in*, whose badness runs the other way, so the safe side is the centre's. A corridor edge
and a hill exit are offered only when the opponent is inside them, because shoving a body off a road
it already left is not an objective and would put a zero distance at the head of the order every
pass.

**`disc_clearance` is deliberately not used**, and the earlier claim that no nearest-unsupported
query exists was wrong — it is simply the wrong tool twice. It scans every compiled boundary span,
bounded by `kMaximumTerrainBoundaryElementCount = 8192`, where this walk is bounded by the authored
32 holes and 8 corridors; and `compile_terrain_boundary` pushes the arena envelope as a span whose
`BoundaryFeatureId` carries no shape kind, so a controller could not tell a lethal hole rim from the
harmless outer wall — which outer-map routing makes the one direction a shove accomplishes nothing
in.

**The opponent scan is bounded by 4096, not by 64.** `kMaximumLobbySeatCount = 64` is, in its own
comment, a lobby bound and not a roster bound; every component store is bounded at
`kMaximumEntityCount = 4096`. The tighter operational ceiling is `kSnapshotEntityLimit = 1024`,
already enforced at startup by `match_startup_validation.cpp`, so a bot never scans more entities
than a browser could be sent — the human/bot symmetry rule holding here too. The `PhysicsBody` +
`Controllable` join is `simulation::for_each_entity_with_both`, written in those words for "an
in-process bot's `Observation`", so the scan is one allocation-free ascending pass and not an
O(P*B) nested lookup. What bounds the *candidates* is a fixed-size **stable insertion** ordered by
squared distance and then by `EntityId`, and never `std::nth_element` or `std::partial_sort`:
neither states an order among equal elements, and this domain's contract is that two toolchains
select the same candidate bit for bit. Its width is `kMaximumTacticalShoveCandidateCount`, a
compile-time constant equal to the per-provider budget and **never a profile key**, because an
authored width would move a ceiling this domain says no authored input can move.

### The opening: five published booleans, a fixed order, and no division anywhere

A shove candidate carries `opening`, which is how little the opponent can answer a shove with. It is
a weighted sum of five booleans read in one written order,

```
exposure = stun + shield spent + charge spent + outside the zone + holding the hill
opening  = (exposure * exposure_preference) + (1 - exposure_preference)
```

each term the shared weight in `controllers_limits.hpp` or zero. **A profile that authors no
preference carries an opening of exactly one on every candidate and scores exactly as it did before
this existed**, which is the property that keeps four of the five shipped profiles bit-identical
here.

**Booleans, because every ratio-shaped spelling of this quality is a permanently inert bot.** The
natural forms — exposure ticks over `elimination_grace_ticks`, presence ticks over
`point_interval_ticks`, cooldown remaining over a shield cooldown — each divide by a denominator
this codebase documents as legally zero, and two of those denominators live in `mode_state`, which
the unconditional provider is documented never to read. `std::get` on the wrong variant arm throws
`std::bad_variant_access`; `ControllerHost` catches and continues; `TacticalController` assigns its
state only after `decide_next` returns, so the bot repeats the throwing pass forever. The fix costs
nothing, because `ZoneExposure` and `HillPresence` are **erasure-based presence flags** — the owning
systems remove the entry rather than storing a zero — so "outside the zone" and "holding the hill"
are already booleans with no denominator and no `mode_state` read behind them.

**The five weights are shared constants and deliberately not profile keys**, and the stun term is
worth as much as the other four together. A profile authors *how much* it prefers an exposed target
and *how open* a fight must be; it does not author what "exposed" means, because five per-profile
weights would be five combat knobs whose only joint effect is letting one profile disagree with
every other about which published windows count as an opening at all. Stun is weighted 0.5 because
it is the only one of the five that removes every answer at once — a stunned body cannot thrust
away, cannot raise a shield and cannot charge out for a whole published window — while each of the
other four removes exactly one answer, so the four are equal at 0.125 and nothing in this tree has
measured a ranking among them. Every value is a negative power of two, so the sum is exact in binary
under any association and the `static_assert` that they total 1.0 is a real check rather than a
tolerance.

**The two ability terms are read the way `AbilitySystem` reads them, not as bare presence.** A
shield counts as *spent* only when its cooldown is still live **and** its protection has already
ended: the cooldown starts at activation, so bare presence would have scored a currently protected
body as the most exposed thing on the map — the term would have inverted. A charge carries no
protection window, so its cooldown alone is the whole question.

**Two limits, recorded rather than left to be discovered.** First, `ZoneExposure` is published only
under royale and `HillPresence` only under king of the hill, so under race — and under every mode a
later step adds — the quality degenerates to the stun term plus the two ability cooldowns and can no
longer tell two opponents apart by where they are standing. Its reachable maximum falls with it and
nothing renormalises, because renormalising is division and division is the failure above. Second,
**nothing here is free**: each of the five terms is one linear component-store scan per kept
opponent, so the quality is five scans wide, and what bounds it is the nearest-N filter that already
bounds the provider and nothing else. Only the stun term is cheap to justify — one store, one
window, no mode behind it, and the one term that still means something under a mode this file has
never seen — and it is not free either.

**`minimum_opening` filters here, after the nearest-N filter, and the order is an honest limit.**
The floor is applied to each kept opponent's opening before the hazard walk, so a fight the profile
has already called low-value costs nothing further and yields no candidate. Running it *before*
nearest-N would close a real hole — the filter can discard the most exposed opponent on distance
before the floor ever sees it — but that cancellation needs 33 or more dynamic controllable bodies
against a filter width of `kMaximumTacticalShoveCandidateCount = 32`, and **every fixture in this
tree seats three.** Closing it costs either an O(P x S) rescan or a second N-way merge beside the one
`component_join.hpp` declares itself to be, a primitive whose own header records that every prior
private copy of it was an engine review finding. So the hole is written down instead of paid for,
and it stays written down until a roster in this tree is larger than the width.

**A dropped candidate is a vanished candidate, which is what buys "abandon low-value fights" for
free.** A held key that no longer appears among the screened candidates already releases the lease,
cancels held input and restarts reaction timing under `kLost`; a candidate the floor removed is
indistinguishable from one whose opponent left, so no new reason code and no new path were needed.

## Charge and shield

Both are decided on the **pursuing path**, where a candidate has been selected, and never on the
seek draw: that draw sits inside the not-arrived branch, so gating an ability on it would mean a bot
standing on its objective could never raise a shield — precisely the state ADR 0008's "defend a
stable interior" describes, and precisely when an opponent's charge arrives — while adding a draw on
the arrived branch would consume randomness that does not exist today and move every authored
profile's stream. **Step 22b adds no draw anywhere.** There are still exactly two draw sites, seek
then aim, where Step 15 put them, which is what the `draw_count()` assertions hold still.

**The charge screen (`tactical_charge_screen_admits`) compares exit time to contact time, never
`.has_value()`.** `first_support_exit` returns an `optional<MotionTime>` and the escape screen
beside it discards the value, which is right for an overshoot screen and catastrophic here: a shove
charge points at the hazard *by construction*, so a presence test is always true and would **veto
every shove the step exists to enable**. The admitted form is

```
charge iff no exit, or t_exit.value() > (|O - B| - r_self - r_opponent) / ray_length
```

— "I reach them before I run out of ground" — one exact comparison on a call the pass already
makes. The ray length decides how far the screen can *see* and nothing else: multiplying through,
the test is `t_exit * L > gap`, which carries no `L` at all, so a longer authored screen is
monotonically stricter and a zero-length one examines nothing and refuses nothing. A bot standing in
void is refused, and that falls out rather than being written: an initially unsupported start exits
at `t = 0`.

**The alignment gate (`tactical_charge_alignment_admits`) compares against the resultant, not the
intent.** The burst is *additive*: a body at 600 wu/s along +y charging +x leaves at (450, 600) —
750 wu/s, 53.1 degrees off the commanded ray — so a ray cast due +x screens ground the body never
crosses, and certifying the commanded angle certifies the wrong one. The bot cannot compute the
resultant, because `charge_speed_fraction` reaches no snapshot, so the gate takes the
conservative published-state form: refuse when the component of committed velocity perpendicular to
the commanded direction exceeds `kTacticalChargeAlignmentPerpendicularFraction` of the published
normal ceiling. At a perpendicular component equal to the whole ceiling the resultant is at least 45
degrees off for any burst up to the ceiling. It is a shared unconditional gate and deliberately not
a key: a per-profile alignment tolerance would be a combat knob whose only effect is letting a
profile switch off a safety screen.

**What must stand beside the screen: it screens what is *in* the corridor and is never a claim the
body can stop before leaving it.** Name the drag each figure assumes, because the frightening number
belongs to the development configuration and not to the game. At `config/blob-royale.cfg` (ceiling
600, accel 400, **drag 0**) an aligned charge from the ceiling leaves at 1050 wu/s and needs
`v^2/(2a) = 1378` wu to stop against a 1154 wu arena diagonal — no ray makes that safe. From rest
the same charge needs 253 wu. Under the **deployed** `drag 2.0` the combined stop is 342 wu, 3.4x
inside the arena. Only `charge_speed_fraction` is unpublished, so the bot cannot compute its own
post-burst speed even in principle — the screen is the compensation, not the solution — while
`v^2/(2a)` for the *pre-burst* body is exactly computable, since both
`acceleration_world_units_per_second_squared` and `normal_top_speed_world_units_per_second` are
published in `match.movement.current`.

**The shield closing test (`tactical_opponent_closes_to_contact`) is named as prediction, not as
authored caution.** ADR 0008 authorises exactly this — "perfect-shield anticipation uses visible
trajectories plus profile reaction/error, not a collision callback available only to bots" — and
the owner's line is drawn at *unpublished
physics*, not at arithmetic over published state. The authored caution is the **window**; the test
is a bounded linear extrapolation, and calling it authored caution would be the dressing-up that
same section forbids. It is one evaluation per opponent at the end of the window and never a swept
root, because `swept_geometry` owns the one collision equation in this tree and a bot does not get a
second one; both bodies are carried forward by `position + velocity * window` and contact is the two
published radii, so two bodies that declare no radius never close by it.

**The bias is early and one-signed.** There is no drag term, because `drag_per_second` reaches no
snapshot, so wherever drag is nonzero the predicted separation is smaller than the real one: about
+2.3% at 8 ticks, +8.5% at 32 and +21.7% at 80 under the deployed 2.0, and +56% / +268% / +789% at
the 40 both tactical browser fixtures author, where total coast is 15 world units at 600 wu/s and
the predictor is describing a body that has already stopped. **No bound makes the predictor honest
in every in-tree configuration**; the constant names the one it is honest in and the profile author
owns the rest. Note also that no gate in this tree exercises bot combat prediction at the drag the
deployed game runs at: both tactical browser fixtures author 40, `config/blob-royale.cfg` authors 0,
and the deployment authors 2.0.

**Shield before charge, and for the engine's reason rather than for taste.** `AbilitySystem` spells
its priority `charge_admissible && !shield_eligible`, so a local order preferring offence would be
one the tick contradicts on the pass both were wanted. The closing test is over every published
opponent and not only the selected candidate's, because a shield answers whoever is arriving; a
charge is an action on a chosen target and so is scoped to the selection, and it is aimed at the
**opponent** rather than at S, since a burst along `B -> S` would push nothing anywhere.
`tactical_shove_opponent_body` is the one owner of "which published body is this candidate about?",
exported so the screen, the gate and the heading share one scan; it compares the candidate key's
subject as a raw value and never rebuilds it through `EntityId::create`, because that factory throws
and a throw here would leave `last_completed_tick` unadvanced and repeat on every following pass —
a permanently inert bot rather than one bad decision.

**At most one ability command per pass, and a locally visible cooldown suppresses it.** `Shield` and
`Charge` publish every window verbatim — neither has a `ComponentPublication` specialization — so a
bot reads its own protection, its own shield cooldown and its own charge cooldown and declines a
pulse the tick would refuse anyway. That is not a correctness gate: a refused pulse consumes no
cooldown, queues nothing and throws nothing. It is the honest response to the command-rate asymmetry
recorded under "The two ability helpers" above.

**Abilities sit behind the same reaction gate as everything else, and that derates them.** With the
shipped `steady` profile's `reaction_delay_ticks = 80` against a 20-tick decision spacing, four of
every five passes return `kAwaitingReaction`, so **an ability has roughly a 20% duty cycle**, on top
of the 1-to-21-tick activation jitter a bot cannot observe. There is deliberately no second, faster
reflex path to hide that: ADR 0008 requires reaction to apply here in terms — "visible trajectories
**plus profile reaction/error**".

**The shield is a defensive pulse first and a parry attempt only incidentally, and the arithmetic is
why.** All three `Shield` windows date from one activation, so the next pulse is admissible at
`activation + max(160, 360) = 360` ticks — 0.9 s, eighteen decision passes — and active protection
blocks this bot's own charge for 160 of them, against a payoff window of 32 ticks. A bot would have
to land inside that opening better than one time in eleven for a speculative shield to beat holding
it, and ADR 0008 already concedes it cannot reliably do so. Those three numbers are `[abilities]`
tuning this library links no path to and may not read, which is also why keeping the anticipation
window shorter than the mode's perfect opening is the profile author's job: a window longer than the
opening can produce no parry at all.

## Profile-weighted utility selection

**This stage is the point of Step 22a.** Step 15 selected with `min_element` over nearest squared
distance and consulted the profile at four call sites, none of them selection — so two profiles
differing only in numbers picked the *same* candidate on the same frame, and no differentiation
could be proven no matter how many objective kinds were added. ADR 0008 names the path as "published
observation → objective candidates → safety screening → **utility selection** → steering/actions";
the utility stage is what did not exist.

`tactical_candidate_score` is the one written scoring order every profile shares:

```
preference = weight(kind) * (1 - normalized_distance) * opening
penalty    = escape_blocked ? (1 - risk_tolerance) : 0
score      = (preference - penalty) + held_bonus
```

Multiply, subtract, then add, in that order and never reassociated, so two toolchains select the
same candidate bit for bit. `normalized_distance` is the distance to the target over the **published
arena diagonal**, clamped to `[0,1]`, so one authored weight means the same thing on a 960-unit
fixture map and on a ten-kilometre one. Every term lands in the unit interval when the weights do,
which is what makes the penalty commensurate with the preference. Nothing here draws from the
generator: selection is deterministic, and the seek and aim draws stay exactly where Step 15 put
them, in the same order, so no authored profile's stream moved.

**The opening is a third *factor* and never a fourth additive term, and that is not a taste
question.** An additive exposure term destroys the commensurability the paragraph above rests on:
the escape penalty is at most 1.0, so any positive coefficient on an added exposure could outrank
it, and a maximally cautious profile could be made to prefer a cliff-blocked shove over a clean
gate — the exact statement this file claims is *provable* rather than a tuning accident. A third
factor keeps the product inside `[0,1]` where the penalty already lives, and it still reorders two
same-kind candidates, which is the entire job: with one kind in the set the weight is a common
factor and reorders nothing, so exposure has to enter somewhere the *kind* is held constant.

**The `opening` field defaults to `1.0`, and that default is load-bearing.**
`TacticalObjectiveCandidate` gives its other members zero and false defaults, and the `candidate()`
helper aggregate-initialises only the first four of them, so a `double opening{}` would multiply
every hill, zone, gate and recovery candidate's preference by zero and silently make four kinds
unselectable. That is `AuthoredObjectiveWeight`'s defect one layer down, except that **the compiler
cannot catch this one** — the aggregate is well formed with either initializer — which is why a test
pins a raw mode candidate's score as unchanged across the step.

Exposure is also the first field that runs *against* the ownership rule this file used to state
whole: "a raw provider candidate carries zero and false for both, because only
`collect_tactical_objective_candidates` holds the terrain and the arena". That is now half of a
rule. The collector holds the terrain and the arena but **not** the opponent the nearest-N filter
kept, nor the `HillMotion` the circle provider already looked up, so `opening` and the objective's
published velocity are answers only the *provider* can give; screening copies both through
untouched, and every other kind carries the neutral value the collector could not have computed
either.

`tactical_select_candidate` takes the maximum, and an exact tie falls through to
`tactical_candidate_precedes` — kept from Step 15, no longer the selector but now the *tail* of the
chain. Its order is nearest first, then the key, whose first component is the stable kind ordinal;
keys are unique within one screened set, so this is a strict total order and **no selection can
depend on the order providers happened to push candidates in**.

**It returns `std::optional<std::size_t>`, and that fixed a contract rather than a crash.** There
was no live out-of-bounds read: the loop's first test short-circuited on `best == candidates.size()`,
so on any non-empty set the first iteration assigned index 0 unconditionally and the sentinel was
unreachable. What was wrong was the *signature*, which promised a value its one caller indexed with
— and the reason that mattered is what the promise would have cost the first time anything made the
sentinel reachable, which a drafted zero-weight veto very nearly did. The candidate vector is
`reserve`d at the raw merged count and then filled only with terrain-screen survivors, so whenever
anything is screened out `capacity > size` and index `size()` lands **inside the live allocation**:
a sanitizer reports nothing without container-overflow annotations. The lease would then copy a
candidate whose `key.kind` is an arbitrary byte, and the next pass would index a
`std::array<double, kTacticalObjectiveKindCount>` with it — a second, unbounded read. Silent, not
caught, and Step 22b's review and the plan's Step 22b bullet both called it a sanitizer abort.

**A bare size check was rejected because it is a comment the next caller can ignore.** The optional
makes the compiler close the hole at every call site that does not exist yet, which is the
discipline this domain already applies one file over: `AuthoredObjectiveWeight`'s deleted default
constructor, and the `default`-less switches over the kind enum that turn a sixth kind into a build
failure rather than a silent zero. The one live caller answers the empty optional with
`kNoScreenedCandidate` rather than a new reason code, because "selection chose nothing" and
"screening left nothing" are one outcome and it is already unreachable behind the empty-set branch
above it.

**Hysteresis is the existing lease, given a bonus, not a second memory beside it.** While the
persistence window is open no selection runs at all. At the moment it ends, the candidate it was
holding carries `kTacticalHeldTargetBonus` (0.125, an eighth of the score range) into the one
comparison that can replace it, so a challenger must beat the held target by more than that bonus.
That damps oscillation between two near-equal candidates without pinning a bot to a stale one.
**Target loss is the other half**: a held key that no longer appears among the screened candidates
releases the lease immediately, cancels held input, and restarts reaction timing.

**Step 22b is what first makes these weights matter in production, and that is the point of adding
a fifth kind.** Until the opponent-derived provider existed, every shipped mode yielded at most one
candidate — `circles<>` reads a store holding exactly one `Hill` or exactly one `Zone`, and `race()`
returns exactly one candidate on every `kReady` path — and with a one-element span
`tactical_select_candidate` returns index 0 regardless of score, so **every objective weight was a
common factor that changed no outcome in a running game**. 22a's weights were proven only against
synthetic unit-test candidate sets. The shove provider is the first thing that puts two kinds in one
screened set, and a weight then orders them directly.

The weight was not *inert* even then, and the distinction is worth keeping. With a single kind
the weight is a common factor and cannot reorder two candidates by itself; what it reorders is a
candidate against the *escape penalty* and against the *hysteresis bonus*, neither of which it
scales. `w * (proximity_near - proximity_far) > 1 - tolerance` is the authored decision between a
near objective a bot would overshoot into a cliff and a clear one further away, and the weight alone
settles it. `tests/unit/controllers/tactical_controller_tests.cpp` proves exactly that, and proves
it honestly: the two bots share one profile **name**, so they share a seed, a stream and a draw
count, and the only difference between them is one weight. That matters because `tactical_seed_for`
mixes the name's length and every one of its bytes, so two differently *named* profiles already draw
and steer differently before any setting is consulted, and a test comparing two names would prove
nothing.

## Reason codes and bounded work

`TacticalDecisionReason` is a closed 19-value enum recording **which branch produced the decision**,
and `TacticalTargetHold` a closed 7-value enum recording **what became of the held target** on that
same decision. They are two enums rather than one flattened branch-times-hold product, which would
have to be renamed whenever either half grew. Several distinct branches of `decide_next` return the
same observable answer — an empty command vector, or an explicit zero thrust — and before these
enums a test could only tell them apart by side effects: a draw that did not happen, a window that
did not move. A log line would not have fixed that either: free text cannot be asserted on and
drifts from the code the first time a branch moves.

The movement reasons are `kNotDecided`, `kAwaitingBody`, `kNoControllableBody`, `kMatchNotRunning`,
`kStunned`, `kObjectivesWaiting`, `kObjectivesFinished`, `kNoScreenedCandidate`,
`kAwaitingReaction`, `kArrived`, `kSeekDeclined`, `kPursuing` and `kPursuingUnderRisk`. **Step 22c
appends none**, which is the point: an arrival that brakes is the same branch as an arrival that
coasts, and a zero-opening fight is an absent candidate rather than a refused one, so neither
mechanism earned a value. Step 22b appends a combat group of six: `kShieldAnticipated`,
`kShieldUnavailable`, `kChargeMisaligned`, `kChargeGroundEndsFirst`, `kChargeUnavailable` and
`kChargeCommitted`. The holds are `kNone`,
`kAcquired`, `kRetainedInLease`, `kRetainedByBonus`, `kSwitched`, `kLost` and `kReleased`. Every
value is reachable and the unit tests reach each one; for the combat six that is the coverage intent
recorded under "Verification" below rather than a claim about a gate. `kPursuingUnderRisk` is the
**required fallback made visible**: a bot whose every candidate failed escape screening still
pursues the least bad one rather than throwing or standing still, and this value is how a test tells
that apart from a clean run.

**A combat value supersedes the movement value of the same pass, and that is deliberately not a
fourth flattened axis.** An ability is decided after the movement branch and at most once per pass,
so a combat reason replaces the `kArrived`, `kSeekDeclined`, `kPursuing` or `kPursuingUnderRisk`
that pass would otherwise have recorded. One value per branch-times-ability pair is exactly the
product these two enums exist to refuse, and nothing is lost: the returned commands still carry that
pass's thrust, `target_key()` still names the candidate, and `target()` still names where. What
supersession buys is that every combat refusal names its own cause instead of hiding inside a
movement branch that would read identically whether the bot had considered an ability or not. A pass
with no ability to consider at all keeps its movement reason, which is what keeps an ordinary pass
reading as `kPursuing`.

`decision_reason()`, `target_hold()` and `objective_work()` are exposed exactly as the existing
window accessors are, and are rolled back with them when an observation fails validation and is not
consumed — a reason is decision state, not a log.

`TacticalObjectiveWork` counts bounded work **where it happens, rather than estimating it
afterwards**: `raw_candidate_count`, now the *merged* count over every provider that contributed;
`screened_candidate_count`; and `prediction_step_count`. **The 32-candidate throw is measured on
each provider's own count and never on that sum.** `require_candidate_count` throws
`CONTROLLERS.TACTICAL_CANDIDATE_LIMIT_EXCEEDED` before terrain work, and the host isolates that
throw, so a bot that trips it silently stops acting for the pass — which is exactly why the budget
had to become per-provider: a shared one lets a busy mode provider exhaust the shove provider's
allowance and turn a legal world into a throw. Step 15's number is unchanged; only what it is
measured against is.

**The derived ceiling is 96, and Step 22b re-derived it rather than inheriting `2 × 32`:**

```
mode provider    at most 32 raw, at most one hill intercept each              = 32
shove provider   at most 32 raw, and zero intercepts, because authored
                 caution publishes no opponent extrapolation                  =  0
escape screening one ray per screened candidate over the merged at most 64    = 64
                                                                                ---
                                                                                 96
```

which is `kMaximumTacticalObjectiveCandidateCount * (kTacticalObjectiveProviderCount + 1)`. It stays
*derived* from the candidate bound, the provider count and the loops rather than authored, which is
why it is stated beside those loops rather than in `controllers_limits.hpp`, where the bounds an
authored input must satisfy live. **The shove provider's hazard-direction lookup is deliberately not
counted**: it is arithmetic over authored terrain and published centres, never an extrapolation, and
counting it would dilute what the counter names — prediction — into "work in general". **There is
still no search, no replanning loop and no planner**: a prediction is a fixed number of fixed-delta
steps over a bounded horizon, and a zero horizon performs — and therefore counts — none at all.

**What that ceiling does not bound is time, and the gate does not observe the difference.** Screened
candidates rise from at most 32 to at most 64, so worst-case screening terrain work per bot per pass
doubles. `run-benchmarks-linux` is dropped from this step's Verify line for verified reasons — no
file under `benchmarks/` carries a `[bot_profile]` section and no replay fixture seats a tactical
bot, so no accepted golden and no benchmark hash can move — and the consequence is that **nothing in
the gate sees the doubling**. The per-provider budget is a correctness rule, not a free one, and
saying so is the point of writing it here.

## The four facts a controller still cannot see

This section was written as "what the pipeline deliberately does not decide", when shoving, charge
timing and the trajectory-based shield decision were all absent on purpose and all Step 22b's.
**Step 22b decides all three.** The four facts below did not change, and they are why each of the
three is built the way it is rather than the way it looks like it should be: `Observation` still
carries the published snapshot and nothing else, and this library still links no gameplay.

* **The perfect opening's length is derivable, and the entry that stood here said it was not.** The
  shield component publishes `activation_tick`, `shield_expiry_tick`, `perfect_expiry_tick`,
  `cooldown_expiry_tick` and `parry_stun_duration_ticks` **verbatim** — there is no
  `ComponentPublication<Shield>` specialization — so `perfect_expiry_tick - activation_tick` is
  observable the moment anyone in the room raises a shield. What is genuinely unpublished is the
  length *before* any shield exists, which is the state a bot deciding whether to raise one is in.
  The derivation is symmetric, so a browser reads the same field and no boundary weakens; but the
  sentence as written was wrong, and ADR 0008 § "Owner decision" carries the same correction. What
  follows from the true version is unchanged: a profile authors a *lead*, not a timing error against
  a denominator it does not have, and keeping that lead shorter than the mode's opening is the
  author's job because `[abilities]` lives in a library this one may not link.
* **The charge burst has no published length.** `normal_top_speed` is published;
  `charge_speed_fraction` and `charge_safety_envelope_speed` are not. A bot cannot compute its own
  post-burst velocity, cannot compute a stopping distance, and cannot tell an available charge from
  one the safety envelope will silently refuse. That is why the charge screen is a *screen* — "is
  there ground under the corridor I am about to cross" — and never a claim the body can stop before
  leaving that corridor. The screen is the compensation, not the solution.
* **`drag_per_second` reaches no snapshot and no welcome.** It lives on `SimulationConfig`, and the
  three configurations in this tree author 0, 2.0 and 40. At the deployed 2.0 a linear predictor
  overstates travel by about 7% over 32 ticks and about 21% over 0.2 s, the bias is one-signed —
  it always predicts contact *earlier* than it happens — and it flips with a file the bot cannot
  read. Hill intercept remains the one prediction in this domain that needs nothing unpublished,
  because the hill does not drag. The shield closing test extrapolates an opponent anyway and
  **states its own bias for doing so**, because the owner's line is drawn at unpublished physics
  rather than at arithmetic over published state.
* **Shove force needs `restitution`, which exists on the in-process `PhysicsBody` but not on the
  wire.** A controller reading it would see more than a browser client can, breaking the human/bot
  symmetry `tests/unit/controllers/human_bot_symmetry_tests.cpp` asserts. That is why the shove
  provider computes a standing *point* and never a force or an outcome: it answers "where do I stand
  to push them at that", not "how far will they go".

**And publishing all of them would still not buy a reliable parry.** Hosted bots decide at
presentation cadence — `snapshots_per_second=20` against a 400 Hz tick — so one decision per twenty
committed ticks, on a snapshot that may itself be a publish interval stale, with the command landing
at the next tick's phase 0. The activation tick is `observed + k` for a `k` of roughly 1 to 21 that
the bot cannot observe. **The perfect opening is 32 ticks.** The unobservable activation jitter is
comparable to the entire window and dominates any profile-authored timing error. A bot can raise a
shield in anticipation of a contact; it cannot reliably land the perfect opening, and only a change
to hosted-bot decision cadence would alter that.

**Three combat settings are authored here now, and there is still no `aggression` key.** ADR 0008 §
"Tactical personalities without a class per mood" forbids an inert combat knob before its behaviour
exists — "No inert aggression/charge/shield settings are accepted before their behavior" — and each
of `objective_weight_shove_setup`, `charge_screen_diagonal_fraction` and `shield_anticipation_ticks`
is read by behaviour landing in the same commit. That is the rule satisfied, not waived.

**The seed that was said to enforce that rule does not enforce anything, and the record is corrected
here twice.** `tests/fuzz/corpus/application/rejected-tactical-profile-inert-combat.cfg` authors
`aggression=1` and must keep being rejected. First fact: it **was** migrated with the other seeds —
`git show 5b400cd` adds six lines to it — where this file previously said it deliberately was not.
The mechanism this file gave was right, and is exactly what makes the migration harmless: the parser
refuses an unknown key at the line that carries it, before the missing-key sweep runs at the end of
the document, so adding the family's other keys cannot move which rule the seed trips. Second fact,
and the more useful one: **the seed is not an executable guard at all.**
`verify-fuzz-regressions` replays each corpus member and asserts only that the target does not
crash, and `tests/fuzz/application_config_fuzzer.cpp` catches every typed loader error and returns
zero — so a seed that stopped being rejected and started being *accepted* would still pass. The live
guard is a unit-test row: `ParserFailure{"aim_error=0.05", "aggression=0.05",
kConfigurationKeyUnknown}` in
`tests/unit/application/fixtures/tactical_profile_configuration_fixture.hpp`, which asserts the key
is refused by name. The seed is corpus coverage of that path, which is all it ever was.

The four named personalities ADR 0008 sketches — Keeper, Bully, Opportunist, Cautious Racer — were
split out of 22b after a second read-only preflight round found that three of the four need shared
mechanism rather than authored numbers, and **Step 22c ships them**: see "The four named
personalities" above. Step 22b, like 22a, added settings and not profiles, which is what keeps the
tactical-profiles browser spec's exact published profile list unchanged; Step 22c adds four profiles
to `config/blob-royale.cfg` and to nothing else, for the same reason — that spec pins its own
fixture's two-element list three ways, and the deployment file declares no `[bot_profile]` section
at all.

## What Step 22c named but did not build

Each of these is buildable from published state and simply was not built. They are recorded because
each one reads as a bug to somebody who takes a profile's name at face value, and because amending
an ADR clause away because it was not built is moving the goalposts.

* **Keeper does not yet defend a stable *interior*.** A hill candidate's arrival radius is the
  **full** published hill radius, so a bot reports arrived one world unit inside the rim — where it
  is trivially shoved back out — and **the brake makes that worse rather than better**, because it
  stops the bot exactly where it first arrived instead of letting it drift on toward the centre. The
  value-keyed fix that stays inside ADR 0008's own rule is an `arrival_radius_fraction` scaling the
  published radius, so a keeper reports arrived only near the centre. It is named as owed and is not
  in this step.
* **Bully's charge screen is not an "acceptable recovery path".** The screen asks whether there is
  ground under the corridor the body is about to cross, and its own header says it is never a claim
  the body can stop before leaving that corridor. A recovery path is about getting *back*, and
  nothing models it. Both the pre-burst braking distance and the arena bounds are published, so this
  too was buildable.
* **Nothing makes a Bully shove with ordinary thrust.** The only push in the game is the charge, so
  **a Bully whose charge is on cooldown stands at its safe-side standing point doing nothing** until
  the cooldown expires. Contact between two moving bodies still resolves through the ordinary
  composition, so it is not inert — but it is not pushing either, and the profile's name promises
  more than the mechanism delivers.
* **`minimum_opening` runs after the nearest-N filter**, so a roster of 33 or more dynamic
  controllable bodies could in principle hide the most exposed opponent behind the distance filter.
  The reasoning and the price of closing it are under "The opening" above; no fixture in this tree
  seats more than three.

**What Step 22b is, under the owner's decision.** ADR 0008 § "Owner decision: authored caution for
bot combat, 2026-09-12" chose **authored caution over derived physics**: none of the four facts
above became a v3 wire field, and controllers stay exactly where ADR 0002 puts them, seeing what a
browser sees and nothing more. A profile's combat numbers are therefore a screen length and an
anticipation window, and the code is careful about which half is which — the *numbers* are authored
caution, while the closing test under the window is a prediction over published state, named as one
rather than dressed up as caution. The accepted consequences stand: a bot will sometimes charge into
a wall or a hazard it had no way to predict, and its shield will often be mistimed — now one-signed
early, because a window is a lead and the lateness comes from the unobservable activation jitter
instead of from a signed key. **Neither is a defect to tune away**, and a future reader who "fixes"
one by reaching for gameplay configuration inside a controller is undoing that decision. The
parry-cadence limit above stands under the decision either way.

`risk_tolerance` and the objective weights are read by behaviour Step 22a shipped; the three
combat settings are read by behaviour Step 22b ships; and Step 22c's four are each read by behaviour
landing beside them — `road_caution_fraction` by the race provider's recovery threshold,
`arrival_brake_fraction` by the arrived branch, and `exposure_preference` and `minimum_opening` by
the shove provider's opening. A key read by nothing is precisely the inert knob the ADR refuses, and
no step has authored one.

Full findings: `docs/reviews/2026-09-12-tactical-combat-preflight.md`. The contracts these steps
were built to: `docs/reviews/2026-09-12-tactical-pipeline-contract.md` (22a),
`docs/reviews/2026-09-12-tactical-combat-contract.md` (22b) and
`docs/reviews/2026-09-12-tactical-personalities-contract.md` (22c).

## Determinism

Controllers inherit the **opposite** obligation from systems. A system runs inside the tick and is
bound by `docs/architecture/0003-deterministic-simulation-contract.md`; a controller runs outside it,
so a bot may be nondeterministic, asynchronous, or model-driven without touching replayability —
**replay replays the recorded command log, not the controller**
(`docs/architecture/0002-simulation-architecture.md` § "Consequences").

`WandererController` is nonetheless reproducible on its own, because a reproducible bug report and a
fixture that needs no scripted log are both worth having. It owns one `DeterministicRandom` — the
tree's one generator, reused rather than re-implemented — draws its two heading components into
named locals in a written order so no compiler's argument evaluation order can reorder them, and
avoids `std::cos`/`std::sin` because the standard trigonometric functions are not bit-specified
across libm implementations.

Tactical owns a `DeterministicRandom` seeded by stable authored identity, not allocated controller
or body IDs. `tactical_seed_for` starts with
`h = mix_bits(0x746163746963616c XOR raw_match_seed)`, then mixes XOR lobby ID, seat index,
profile-name length, each unsigned ASCII name byte in order, and public `running_started_tick`.
It reseeds once per newly observed running identity without reseating or reallocating controllers.
There are no draws before running. Profile declaration order is not a seed input; this is
domain-separated variation, not a claim of universal 64-bit collision freedom. Diagnostic factories
retain the unchanged legacy room seed and all prior arithmetic/RNG behavior.

Only tactical rejects observations whose tick is no newer than its last successfully completed
tick. The protected default-true `Controller::accepts_observation` hook runs after controller-ID
validation but before base identity/retry mutation. Tactical's refused observations produce no
commands or state changes, and a failed observation does not consume its tick or partial tactical
state. Diagnostics and literal replay retain their existing repeated-observation behavior.

## Verification

Focused tests are registered under the `blob_controllers_unit_tests` CTest target with the
`unit.controllers.` prefix, mirroring this directory under `tests/unit/controllers/`. Run
`./scripts/verify-focused 'unit.controllers|unit.runtime'`; the canonical gate remains
`./scripts/verify-linux pr`.

The test target additionally links `blob_gameplay`, which the library itself must never link: the
end-to-end pass seats two bots in the real `sandbox` mode behind a real `SimulationRuntime` and
asserts both move, and an in-test mode written to avoid that link would be a second copy of
`SandboxMode` and of `thrust_steering`.

Helper promotion is covered against frozen complete diagnostic behavior on both pinned compiler
lanes. Tactical fixtures separately exercise profile/catalogue validation, seed derivation,
public objective/terrain selection, timing, cancellation, and duplicate-observation admission.
Step 22a adds coverage for the written scoring order and its unit terms, maximum-score selection
with the stable-ordinal tie-break, escape screening ranking down rather than deleting, hill
intercept extrapolating the published committed velocity only, hysteresis holding through a lease
and then by the bonus until a challenger beats it, a decision when every candidate screens badly,
every reason code and every hold outcome, and bounded work at its derived ceiling — plus the
weight-not-seed differentiation proof, which holds the profile **name** constant.

Step 22b's coverage is the combat half: a shove candidate produced, screened and selected on a
published opponent, with its target proved to be S and not the opponent; `escape_blocked` keeping
one meaning across both kinds; per-provider budgets holding and the merged ceiling at 96; the
nearest-N filter stable on both pinned lanes; a charge refused when the exit time precedes the
contact time **and taken when it does not** — both halves, because a bot that never charges would
pass the refusal test alone; the perpendicular-velocity gate refusing a badly aligned burst; a
shield raised on a predicted close and not otherwise; at most one ability per pass; a visible
cooldown suppressing a pulse locally; every new reason code reached; and profile differentiation
attributable to the shove weight in a candidate set that now genuinely holds two kinds — the first
non-vacuous weight proof in the tree.

Step 22c's coverage is the four personalities and the three mechanisms they needed: each of the four
selecting differently from the other three on one identical observation, with the profile **name**
held constant so `tactical_seed_for` cannot make the proof vacuous; `arrival_brake_fraction=0`
bit-identical to Step 22b at `kArrived`, and the brake emitting bounded relative commands and exactly
`(0,0)` at rest. Those command-shape assertions do not prove stopping or motion without overshoot;
the arrival-brake contract defines the separate production-loop envelope. Other cases cover
`exposure_preference=0` scoring identically to Step 22b with
a raw mode candidate unchanged, and exposure reordering two equidistant shove candidates;
`minimum_opening` dropping a closed opening and firing `kLost`, releasing the lease and re-arming
the reaction window; a zero shove weight skipping the provider rather than producing an unselectable
candidate; `tactical_select_candidate` returning an empty optional only where documented;
`road_caution_fraction` at its floor keeping a bot on a gate it is standing in; and the shipped
configuration parsing with exactly five profiles.

**The brake needs a third thrust helper, and neither existing one can carry it.** `require_zero`
demands exactly `(0,0)` and `require_go` demands unit magnitude within 1e-15, so a subunit brake
satisfies neither; the new helper is built on `require_same_bits`, and **neither existing helper was
loosened**, because loosening `require_go` would silently retire the normalisation assertion every
pursuing case rests on.

**The `thrust()` test helper cannot carry an ability case**, and that is worth knowing before the
next one is written. It asserts `REQUIRE(commands.size() == 1)` and backs every `require_go` and
`require_zero` in `tactical_controller_tests.cpp`. The existing fixtures publish no opponents, so
nothing fires through it today; any new case that emits an ability must not route through it.

**A `[bot_profile]` key change breaks four gates outside this library**, which is why Step 22a's
Verify line is wider than Step 15's and 22b and 22c keep the same shape: `unit.application`,
`verify-fuzz-regressions`, `verify-web` and `verify-browser-e2e` all parse an authored profile
section. `run-benchmarks-linux` is not in that line and its absence is verified rather than
inherited: no file under `benchmarks/` carries a `[bot_profile]` section and no replay fixture seats
a tactical bot.

**Correction, 2026-09-12: this file previously said `fixtures` "carries the only C++ lane that reads
`config/blob-royale.cfg`". That was false, and Step 22b's three keys shipped on it.** The lane's
`deployment_fixture_tests.cpp` loads `BLOB_ROYALE_DEPLOYMENT_FIXTURE_DIRECTORY`, which
`tests/fixtures/CMakeLists.txt` defines as `deploy/ubuntu-pc` — a file that declares no
`[bot_profile]` section at all. Until Step 22c, **nothing in the tree parsed the shipped
configuration**: it is referenced only by `scripts/verify-linux` and `scripts/assemble-release-linux`,
so the `steady` section since Step 15, Step 22a's six keys and Step 22b's three all shipped with zero
automated parse coverage. Root verified by hand after Step 22b that the shipped configuration loads
and the server reaches `running`, so nothing was broken — but a one-off check is not a gate, and the
four profiles this step authors would have been the largest untested block yet. Step 22c closes the
hole with a second `fixtures`-lane case, behind
`BLOB_ROYALE_SHIPPED_CONFIGURATION_FIXTURE_DIRECTORY`, that loads `config/blob-royale.cfg` through
`ApplicationConfigLoader` and asserts the catalogue holds exactly the five profile names.

The implementation contracts are `docs/reviews/2026-09-11-tactical-profile-contract.md`
(Step 15), `docs/reviews/2026-09-12-tactical-pipeline-contract.md` (Step 22a),
`docs/reviews/2026-09-12-tactical-combat-contract.md` (Step 22b) and
`docs/reviews/2026-09-12-tactical-personalities-contract.md` (Step 22c); these test descriptions are
coverage intent, not a claim that an unrun gate passed.
