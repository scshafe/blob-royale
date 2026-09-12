#ifndef BLOB_ROYALE_CONTROLLERS_TACTICAL_OBJECTIVE_CANDIDATES_HPP
#define BLOB_ROYALE_CONTROLLERS_TACTICAL_OBJECTIVE_CANDIDATES_HPP

#include "controllers_limits.hpp"
#include "observation.hpp"
#include "physics_body.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace blob_royale::controllers {

// Explicit stable ordinals break equal-utility ties, never variant or registration order. They are
// also the index of a profile's per-kind objective weight, which is the second reason the values
// are written out here rather than left implicit.
enum class TacticalObjectiveKind : std::uint8_t {
  kHill = 0,
  kZone = 1,
  kRaceGate = 2,
  kRaceRecovery = 3,
  kShoveSetup = 4
};
inline constexpr std::size_t kTacticalObjectiveKindCount = 5;
[[nodiscard]] constexpr std::size_t
tactical_objective_kind_ordinal(const TacticalObjectiveKind kind) noexcept {
  return static_cast<std::size_t>(kind);
}
static_assert(tactical_objective_kind_ordinal(TacticalObjectiveKind::kShoveSetup) + 1 ==
                  kTacticalObjectiveKindCount,
              "a profile authors one weight per kind, so the count must follow the last ordinal");

struct TacticalObjectiveKey final {
  TacticalObjectiveKind kind;
  // Hill/zone entity, or the race's next gate index. Recovery changes kind but keeps that gate. A
  // shove's subject is the opponent's entity, which is what lets the charge screen recover the body
  // the standing point was derived from without the candidate carrying a second position.
  std::uint64_t subject;
  friend auto operator<=>(const TacticalObjectiveKey&, const TacticalObjectiveKey&) = default;
};

// canonical: tactical_objective_candidate -- one objective, the two screening answers selection
// reads, and the two answers only the provider that built it can give.
// `squared_distance` stays the raw geometry the arrival test compares against a radius;
// `normalized_distance` is that same distance divided by the published arena diagonal, so one
// authored weight and one authored risk tolerance mean the same thing on this 960-unit fixture map
// and on a ten-kilometre one. A raw provider candidate carries zero and false for the screening
// pair: only `collect_tactical_objective_candidates` holds the terrain and the arena, so only it
// may answer them, and a candidate that never reached screening is never selected from.
//
// **`opening` and the published motion run the other way, and that is the amendment.** The
// ownership rule above used to be the whole rule; it is now half of one. The exposure a shove
// candidate carries and the velocity a hill candidate is chasing are both answers *only the
// provider* can give -- the collector holds the terrain and the arena but not the opponent the
// nearest-N filter kept, nor the `HillMotion` the circle provider already looked up -- so screening
// copies both through untouched and every other kind carries the neutral value the collector could
// not have computed for it either.
struct TacticalObjectiveCandidate final {
  TacticalObjectiveKey key;
  simulation::Vector2 target;
  double arrival_radius;
  double squared_distance;
  // Distance to `target` over the published arena diagonal, clamped to [0,1] by screening.
  double normalized_distance{};
  // Support ends along the horizon ray toward `target`; screening writes it. See the collector.
  bool escape_blocked{};
  // How little the objective can answer a shove with, in [0,1], already interpolated by the
  // profile's `exposure_preference`. **The default is one and must stay one**: it multiplies the
  // preference term, the four mode kinds have no opponent to be exposed and the `candidate()`
  // helper aggregate-initialises only the first four members, so a `double opening{}` would
  // silently score every hill, zone, gate and recovery candidate at zero. That is
  // `AuthoredObjectiveWeight`'s deleted default constructor one layer down, except that the
  // compiler cannot catch this one -- the aggregate is well formed with either initializer.
  double opening{1.0};
  // The objective's own published velocity, flattened to scalars for the reason `ShoveOpponent`
  // is: `simulation::Vector2` has no default constructor, and a member without a default member
  // initializer would break the four-member aggregate initialization every provider uses.
  //
  // **The hill provider writes it and the arrival brake reads it, so nothing ever rebuilds an
  // `EntityId` from `key.subject` to ask again.** `kMinimumEntityId` is 1, so a race gate index is
  // a perfectly legal `EntityId` naming some foreign entity, and `EntityId::create` throws outside
  // its range -- and a throw on the arrival path is the permanently inert bot the brake exists to
  // avoid, because `ControllerHost` catches it and `TacticalController` never advances its state.
  // kZone, kRaceGate and kRaceRecovery carry a structural zero: no zone, gate or recovery point in
  // this tree publishes motion of any kind. kShoveSetup carries a *deferred* zero -- an opponent's
  // velocity is published and `tactical_shove_opponent_body` already resolves it -- and the brake
  // is kHill only, so writing it today would be data no reader has.
  double objective_velocity_x{};
  double objective_velocity_y{};
  friend bool operator==(const TacticalObjectiveCandidate&,
                         const TacticalObjectiveCandidate&) = default;
};

// canonical: tactical_objective_policy -- a profile's numbers, without the profile.
//
// Screening and selection take plain validated numbers rather than a `TacticalProfile&`, for two
// reasons. The scoring rule is then exactly testable without constructing a configuration section,
// which is what lets the differentiation proof hold the profile *name* constant -- and it must,
// because `tactical_seed_identity.hpp` mixes the name's length and every one of its bytes, so two
// differently named profiles already draw and steer differently before a weight is consulted. And
// the dependency runs one way: `tactical_profile.hpp` may know the objective kinds, this file does
// not know the profile. `tactical_controller.cpp` holds the single adapter that fills this in.
//
// It carries the combat numbers too, because **a provider and a screen receive only the policy**.
// The shove provider, the charge screen and the shield closing test are all reached with this value
// and the observation and nothing else, so a setting one of them reads has to arrive here or be a
// second argument list beside it.
//
// **And a *mode* provider now reads a profile number too, not only the combat screens.** `race()`
// took its recovery threshold from the shared `kDefaultRacerCautionFraction` and carried
// `[[maybe_unused]]` on its policy parameter; it reads `road_caution_fraction` from here instead,
// so "the policy is what a provider is allowed to know about a profile" is now a rule the mode
// table obeys as well as the combat path. What has not changed is the direction of the dependency:
// this is still numbers without a profile.
//
// **Every field's default is the value that reproduces the behaviour before it existed**, which is
// why `road_caution_fraction` is the one member here that does not default to zero. A zero road
// caution does not disable recovery, it inverts it (`controllers_limits.hpp`), so a default-
// constructed policy would drive a racer off the road rather than leave it where it was.
struct TacticalObjectivePolicy final {
  // Indexed by `tactical_objective_kind_ordinal`. A weight scales the proximity term only.
  std::array<double, kTacticalObjectiveKindCount> objective_weights{};
  // 1.0 ignores escape screening entirely; 0.0 applies the full unit penalty. Between them it
  // scales how far a screened-but-marginal candidate is ranked down, and nothing else.
  double risk_tolerance{};
  // Committed ticks a prediction may look ahead. Zero performs, and therefore counts, none at all.
  std::uint64_t prediction_horizon_ticks{};
  // The charge screen's ray length as a fraction of the *observed arena diagonal*, never world
  // units: `Observation::terrain()` already hands a controller the validated bounds, while an
  // absolute scalar would mean two orders of magnitude of different things across the
  // configurations already in this tree. See `tactical_charge_screen_admits` for what the length
  // does and does not decide.
  double charge_screen_diagonal_fraction{};
  // Committed ticks the shield closing test extrapolates published motion over. Zero anticipates
  // nothing at all and is a real authored answer, exactly as a zero `prediction_horizon_ticks` is.
  std::uint64_t shield_anticipation_ticks{};
  // The race provider's recovery threshold, as a fraction of the published road half-width. It is
  // strictly positive where a profile authors it, and it defaults to the same shared constant the
  // provider used to name directly, so a policy nobody filled in races exactly as it did before.
  double road_caution_fraction{kDefaultRacerCautionFraction};
  // How far a shove candidate's opening is allowed to move it: zero leaves every opening at one
  // and reproduces the score before this existed, one hands the exposure quality through verbatim.
  double exposure_preference{};
  // The opening a shove candidate must clear before the shove provider yields it at all. Zero
  // admits every fight. This is a *provider* filter and never a selection veto, so a fight below
  // it is an absent candidate rather than an unselectable one.
  double minimum_opening{};
  friend bool operator==(const TacticalObjectivePolicy&, const TacticalObjectivePolicy&) = default;
};

// Bounded work, counted where it happens rather than estimated afterwards. `raw_candidate_count` is
// the *merged* count over every provider that contributed; the 32-candidate throw is measured on
// each provider's own count, before any terrain work, and never on this sum.
struct TacticalObjectiveWork final {
  std::size_t raw_candidate_count{};
  std::size_t screened_candidate_count{};
  std::size_t prediction_step_count{};
  friend bool operator==(const TacticalObjectiveWork&, const TacticalObjectiveWork&) = default;
};

// How many providers may contribute to one merged candidate set: exactly one schema-keyed mode
// provider, chosen by the running schema id, plus every unconditional provider beside it. The
// collector static-asserts its two tables against this number rather than letting the two drift.
inline constexpr std::size_t kTacticalObjectiveProviderCount = 2;

// One pass extrapolates at most one moving-hill centre per raw mode candidate and casts at most one
// escape ray per screened candidate, and screened candidates are a subset of the merged raw ones,
// which `require_candidate_count` already throws above 32 on **per provider**. This ceiling is
// therefore *derived* rather than authored, which is why it is stated beside the loops that cannot
// exceed it rather than in `controllers_limits.hpp`: that file states the bounds an authored input
// must satisfy, and no authored input can move this one -- it follows from the candidate bound
// stated there, from the provider count above, and from the two loops below.
//
// The derivation, written out because Step 22b re-derived it and the old `2 * 32` is now wrong:
//
//   mode provider    at most 32 raw, at most one hill intercept each            = 32
//   shove provider   at most 32 raw, and **zero** intercepts, because authored
//                    caution publishes no opponent extrapolation                =  0
//   escape screening one ray per screened candidate over the merged at most 64  = 64
//                                                                                 ---
//                                                                                  96
//
// which is `kMaximumTacticalObjectiveCandidateCount * (kTacticalObjectiveProviderCount + 1)`.
//
// **The shove provider's hazard-direction lookup is deliberately not counted.** It is arithmetic
// over authored terrain and published centres -- a hole's centre, a corridor centreline, a lethal
// entity's position, a hill's centre -- and never an extrapolation of anything into the future.
// Counting it would dilute what this counter names, which is prediction, into "work in general".
//
// **What this number does not bound is time.** Screened candidates rise from at most 32 to at most
// 64, so worst-case screening terrain work per bot per pass doubles, and nothing in this step's
// verification gate observes that: `run-benchmarks-linux` is dropped from the Verify line because
// no file under `benchmarks/` carries a `[bot_profile]` section and no replay fixture seats a
// tactical bot. The per-provider budget is a correctness rule, not a free one.
inline constexpr std::size_t kMaximumTacticalPredictionStepCount =
    kMaximumTacticalObjectiveCandidateCount * (kTacticalObjectiveProviderCount + 1);

// The shove standoff's margin, as a fraction of the published arena diagonal.
//
// A shove candidate stands one `r_self + r_opponent + margin` behind its opponent, and this is that
// margin. It is a fraction of the arena rather than a world-unit scalar for the reason
// `normalized_distance` is: a hundredth of the diagonal is about eleven world units on the 960-unit
// fixture map and scales with a ten-kilometre one, where a written constant would be a different
// thing in each. **It must be strictly positive**, and that is why the margin exists at all rather
// than the standoff being the two radii: `PhysicsBody::kUndeclaredRadius` is zero and every body
// built by the three-argument `PhysicsBody::create` carries it, so with no margin the standing
// point of two such bodies would collapse exactly onto the opponent -- the one place the paragraph
// below says a shove target must never be.
inline constexpr double kTacticalShoveStandoffDiagonalFraction = 0.01;

enum class TacticalObjectiveDisposition { kReady, kWaiting, kFinished };
struct TacticalObjectiveCandidates final {
  TacticalObjectiveDisposition disposition{TacticalObjectiveDisposition::kReady};
  std::vector<TacticalObjectiveCandidate> candidates{};
  TacticalObjectiveWork work{};
};

// canonical: tactical_objective_candidates -- closed public-schema providers, the unconditional
// opponent-derived provider beside them, terrain screening, escape screening, and the moving hill's
// intercept point.
//
// The body is the caller's resolved owned dynamic body. At most 32 raw candidates **per provider**,
// checked before terrain work; malformed objectives and unsupported schemas throw CONTROLLERS.*.
// Canonical terrain numerical failures propagate, never become a partial or silently repaired
// result.
//
// **Escape screening is an overshoot screen, and that is the whole of what it claims.** A candidate
// whose straight approach leaves supported ground before the target is already *deleted* here, by
// the `first_support_exit` filter Step 15 wrote, so a horizon ray along that same approach can only
// find the end of support *beyond* the target. What it therefore answers is "if I keep going at the
// room's published top speed for the profile's horizon, do I run out of ground?", and a candidate
// that answers yes is ranked down by `tactical_candidate_score` rather than removed -- removal
// would be a fifth way for this function to return nothing, and the step requires a decision when
// every candidate screens badly, not silence. The ray asks `simulation::first_support_exit`, the
// same canonical query the screen beside it already calls: there is no second support predicate
// anywhere in this library, and adding one would break the one-geometry-owner rule that
// `terrain_queries.hpp` states.
//
// The ray's length is the published `MovementTuning::normal_top_speed` times the horizon in
// seconds, clamped to the arena diagonal because no supported point lies beyond it. Top speed is a
// published session fact a browser client reads too (`protocol_v3_json_encoding.cpp`), so this
// costs the human/bot symmetry nothing. Drag is *not* published and is deliberately not modelled:
// omitting it overstates travel, and for a safety screen an overstatement is the conservative
// direction. Rejected alternative: the body's own committed velocity, which is published and exact
// but is zero for a bot at rest, so it would screen nothing at the moment a bot commits to a
// target -- which is the only moment worth screening.
//
// ## Two providers, and why one of them is not a table row
//
// **The mode provider owns the disposition.** Exactly one schema-keyed provider runs per pass, its
// disposition is this function's disposition, and shove candidates merge **only** when that
// disposition is `kReady`. That rule decides two cases that would otherwise be decided silently, so
// they are stated here: `race()` returns `kWaiting` for a bot carrying no `RaceProgress` yet, which
// is the state immediately after spawn, and `kFinished` for a racer that has taken its last gate,
// and `TacticalController` zeroes thrust in both. So **a bot cannot shove between its spawn and the
// race system attaching progress, and a finished racer is a stationary target for the rest of the
// match.** That is the intended reading of "the mode owns the disposition", not an oversight of it.
//
// **The mode/shove split is a policy gate and not a data boundary**, and the difference matters
// because the schema table looks like one. `circles<>` reads `components<Hill>()` and
// `components<Zone>()` and never touches `mode_state`; only `race()` reads `mode_state` at all. The
// running schema therefore gates which *objective* a bot pursues, never which data it may see, and
// the shove provider reads the same published stores under every schema.
//
// **The unconditional provider is a second table, because one `Registration` cannot say "runs in
// every schema".** A schema id is a string a row is matched against; there is no spelling of that
// row meaning "all of them" that is not a sentinel a later reader would have to know about.
//
// **A zero `kShoveSetup` weight skips the shove provider outright, and that is a provider-level
// skip and not a selection-level veto.** The difference is the whole of why it is safe: the
// profile produces *no* shove candidate rather than an unselectable one, so there is no all-vetoed
// case to invent a reason code for, no zero-thrust fallback branch, and no change at all to what a
// zero weight means for the four mode kinds -- there it still means "I do not care about that
// objective", scored at zero and outranked, exactly as before. It also saves the entire nearest-N
// opponent scan and every hazard walk behind it for a profile that could never have acted on the
// result. A general zero-weight veto in the selection stage would have bought none of that and
// would have had to answer "what does a bot do when everything is vetoed?", which is a question
// this pipeline does not otherwise have.
//
// **The unsupported-schema throw is unreachable, and the reason lives two layers away.** A sandbox
// world publishes `NoModeState{}`, whose schema matches no row, so this function throws
// `CONTROLLERS.TACTICAL_MODE_UNSUPPORTED`; `ControllerHost` catches it and continues, and
// `TacticalController` assigns its state only after `decide_next` returns, so `last_completed_tick`
// never advances and such a bot would be inert on *every* pass rather than one. It cannot happen:
// `application_config.cpp` refuses any profiled bot in a mode that does not accept `kStartMatch`,
// and `sandbox_mode.hpp` does not accept it. **That safety rests on a configuration-time rule
// enforced in `blob_application`, which `blob_controllers` neither depends on nor can see**, which
// is why it is written here rather than left to the reader to find. A `std::visit` over the closed
// `ModeMatchState` variant would make the throw structurally unreachable instead of conditionally
// so; that is a change to the extension point's shape and is deliberately not made in this step.
//
// ## The shove candidate's target is S, the safe-side standing point
//
//   S = O + unit(O - Hazard) * (r_self + r_opponent + margin)
//
// **It is never the opponent and never the hazard, and a later reader will want to "simplify" it to
// the opponent's position.** Do not. If the target were the opponent or the hazard beyond it,
// `escape_blocked` would be true for *every* shove candidate by construction -- the hazard past the
// target is the entire point of the objective -- and `tactical_candidate_score` would subtract a
// constant `(1 - risk_tolerance)` from all of them. A shove-preferring profile would then have to
// author a high `risk_tolerance` to score any shove above that penalty, and
// `controllers_limits.hpp` defines `risk_tolerance` as the single knob that scales that penalty
// away, so **the same authored number that let it shove would cancel its cliff caution on the race
// gate**. One profile could not be aggressive toward opponents and careful about ledges at once.
// With S the ray `B -> S`
// continues *away* from the hazard past S, `escape_blocked` keeps its shipped meaning verbatim, and
// one `risk_tolerance` keeps one meaning.
//
// S also survives the screen it is put through. It is strictly farther from the hazard than the
// opponent is, so wherever ground extends one standoff behind the opponent it is supported by
// construction; what the screen does correctly delete is a shove *across* a hole, because `B -> S`
// then crosses void. That is the right answer and it needs no pathfinder.
//
// **The badness direction costs no terrain query.** The hazard is the nearest of four
// snapshot-visible, mode-independent things, chosen by smallest published distance under an
// explicit total order whose tail is a stable id: an authored `terrain.holes()` centre; on
// `kCorridors` ground the outward normal from the nearest centreline, via the canonical
// `corridor_project_to_centreline` the race provider already calls; the nearest entity carrying
// `LethalOnContact`; and a published `Hill` the opponent is standing in, whose badness runs the
// other way -- out of it -- so the safe side is the centre's. `disc_clearance` is deliberately not
// used: it scans every compiled boundary span, bounded by `kMaximumTerrainBoundaryElementCount`,
// where this walk is bounded by the authored 32 holes and 8 corridors, and
// `compile_terrain_boundary` pushes the arena envelope as a span whose `BoundaryFeatureId` carries
// no shape kind, so a controller could not tell a lethal hole rim from the harmless outer wall --
// which outer-map routing makes the one direction a shove accomplishes nothing in.
//
// ## The opening: five published booleans, a fixed order, and no division anywhere
//
// A shove candidate carries `opening`, which is how little the opponent can answer with. It is a
// weighted sum of five booleans read in one written order,
//
//   exposure = stun + shield spent + charge spent + outside the zone + holding the hill
//
// each term the shared weight in `controllers_limits.hpp` or zero, and the opening the profile's
// own interpolation of it,
//
//   opening = (exposure * exposure_preference) + (1 - exposure_preference)
//
// so a profile that authors no preference carries an opening of exactly one on every candidate and
// scores exactly as it did before this existed.
//
// **Booleans, because every ratio-shaped quality here is a permanently inert bot.** The natural
// spellings -- exposure ticks over `elimination_grace_ticks`, presence ticks over
// `point_interval_ticks`, cooldown remaining over a shield cooldown -- each divide by a denominator
// this codebase documents as legally zero, and two of those denominators live in `mode_state`,
// which this provider is documented never to read. `std::get` on the wrong variant arm throws
// `std::bad_variant_access`; `ControllerHost` catches it and continues; `TacticalController`
// assigns its state only after `decide_next` returns, so `last_completed_tick` never advances and
// the bot repeats the same throwing pass forever. The fix costs nothing, because `ZoneExposure` and
// `HillPresence` are **erasure-based presence flags** -- the systems erase the entry rather than
// storing a zero -- so "outside the zone" and "holding the hill" are already booleans that need no
// denominator and no `mode_state` read at all.
//
// The two ability terms are the opponent's two published escapes, and each is read the way
// `AbilitySystem` reads it rather than as bare presence. A shield counts as spent only when its
// cooldown is still live *and* its protection has already ended: during protection the cooldown is
// also live, and a guarded opponent is the least exposed thing on the map, so bare presence would
// have inverted the term. A charge has no protection window and counts as spent on its cooldown
// alone.
//
// **Two limits, recorded rather than left to be discovered.** First, `ZoneExposure` is published
// only under royale and `HillPresence` only under king of the hill, so under race -- and under
// every mode a later step adds -- the quality degenerates to the stun term and the two ability
// cooldowns and can no longer tell two opponents apart by where they are standing. Its reachable
// maximum falls with it, and nothing renormalises, because renormalising is division and division
// is the failure above. Second, **nothing here is free**: each of the five terms is one linear scan
// of one published component store, per kept opponent, so the quality is five scans wide and what
// bounds it is the nearest-N filter that already bounds the provider and nothing else. The stun
// term is the cheapest of the five to justify -- one store, one window, no mode behind it, and the
// only term that still means something under a mode this file has never seen -- which is why it is
// written first and weighted heaviest. It is not free either.
//
// **`minimum_opening` filters here, after the nearest-N filter, and the order is an honest
// limit.** The floor is applied to each kept opponent's opening, before the hazard walk, so a
// fight below it costs nothing further and produces no candidate. Running it *before* nearest-N
// would close a real hole -- the filter can discard the most exposed opponent on distance before
// the floor ever sees it -- but that cancellation needs 33 or more dynamic controllable bodies
// against a filter width of `kMaximumTacticalShoveCandidateCount = 32`, and every fixture in this
// tree seats three. Closing it costs either an O(P x S) rescan or a second N-way merge beside the
// one `component_join.hpp` declares itself to be, a primitive whose own header records that every
// prior private copy of it was an engine review finding. So the hole is written down instead of
// paid for, and it stays written down until a roster in this tree is larger than the width.
//
// ## Bounded work
//
// **The budget is per provider.** `require_candidate_count` is applied to each provider's own raw
// count and never to the merged one, because a shared budget lets one provider exhaust the other's
// and turn a legal world into a throw the host isolates, leaving the bot silently inert for a pass.
//
// **The opponent scan's ceiling is 4096, not 64.** `kMaximumLobbySeatCount = 64` is, in its own
// comment, a lobby bound and not a roster bound; every component store is bounded at
// `kMaximumEntityCount = 4096`. The tighter operational ceiling is `kSnapshotEntityLimit = 1024`,
// already enforced at startup by `match_startup_validation.cpp`, so a bot never scans more entities
// than a browser could be sent -- the human/bot symmetry rule, holding here too.
//
// **The nearest-N pre-filter is a fixed-size stable insertion**, ordered by squared distance and
// then by `EntityId`, and not `std::nth_element` or `std::partial_sort`. Neither of those gives a
// reproducible order among equal elements, and this file's stated contract is that two toolchains
// select the same candidate bit for bit; two opponents at identical squared distance would
// otherwise filter differently per toolchain. N is `kMaximumTacticalShoveCandidateCount`, a
// compile-time constant and never a profile key: an authored N would move a derived ceiling this
// header says no authored input can move.
//
// The `PhysicsBody` + `Controllable` join is `simulation::for_each_entity_with_both`, which
// `component_join.hpp` was written for in those words -- "an in-process bot's `Observation`" -- so
// the scan is one allocation-free ascending pass and not an O(P*B) nested lookup.
// related: ../simulation/terrain_queries.hpp -- the one owner of support geometry.
// related: ../simulation/component_join.hpp -- the one ordered merge of two ascending stores.
// related: ../simulation/components/zone_exposure_component.hpp -- the erasure rule that makes two
// of the opening's five terms booleans instead of ratios.
// related: tactical_controller.hpp -- the pipeline that scores what this returns.
[[nodiscard]] TacticalObjectiveCandidates
collect_tactical_objective_candidates(const Observation& observation,
                                      const simulation::PhysicsBody& body,
                                      const TacticalObjectivePolicy& policy);

// canonical: tactical_hill_intercept -- the one prediction in this domain that needs nothing
// unpublished.
//
// The future centre of a roaming hill is its published committed velocity times the compile-time
// fixed delta times the horizon, and nothing else: `hill_motion_component.hpp` publishes the
// committed velocity, `hill_movement` integrates it with no drag term, and the hill owns no
// `PhysicsBody`, so it is never dragged, never accelerated by contact and never bounded by a
// speed cap. **Say this out loud, because every other prediction this domain wants does need
// something unpublished** and the next reader will assume this one does too: an opponent's
// position needs `drag_per_second`, which lives on `SimulationConfig` and reaches no snapshot; a
// charge needs `charge_speed_fraction`; a parry needs `shield_perfect_window_seconds`. The shield
// closing test below extrapolates an opponent anyway, and states its own bias for doing so.
//
// Intercept and hold are the same computation. A bot already inside a moving hill measures itself
// against the future centre, so it keeps station instead of arriving at where the hill used to be;
// a hill with no published `HillMotion`, or a zero horizon, returns the centre unchanged and every
// stationary-hill behaviour is bit-identical to Step 15's.
//
// Returns `center` unchanged when the extrapolated point is not a representable `Vector2`, which a
// pathological published velocity at a long horizon can reach. That is a real branch, not a
// defensive one, and refusing to predict is the honest answer: a throw here would be isolated by
// the host and would silently stop the bot for the pass.
[[nodiscard]] simulation::Vector2 tactical_predicted_center(const simulation::Vector2& center,
                                                            const simulation::Vector2& velocity,
                                                            std::uint64_t horizon_ticks);

// The published body a shove candidate was derived from, or nullptr when the candidate is of
// another kind or its opponent has left the snapshot -- an elimination, or a body destroyed and not
// yet respawned. It is exported because three readers want it in one pass, and one owner of "which
// body is this candidate about?" is cheaper to keep correct than three private copies of the scan.
//
// **The subject is compared as a raw value and never rebuilt through `EntityId::create`.** That
// factory throws a `SIMULATION.*` error outside the safe-integer range, and a candidate key is a
// plain integer a caller may have authored; `ControllerHost` catches a controller throw and
// continues, and `TacticalController` assigns its state only after `decide_next` returns, so one
// throw here would leave `last_completed_tick` unadvanced and repeat on every following pass. The
// failure mode is not a bad decision, it is a permanently inert bot.
[[nodiscard]] const simulation::PhysicsBody*
tactical_shove_opponent_body(const simulation::WorldSnapshot& snapshot,
                             const TacticalObjectiveCandidate& candidate) noexcept;

// canonical: tactical_charge_screen -- "I reach them before I run out of ground", as one exact
// comparison on a query the pass already makes.
//
//   admit iff no exit, or `t_exit.value() > (|O - B| - r_self - r_opponent) / ray_length`
//
// **Never `.has_value()`.** `first_support_exit` returns an `optional<MotionTime>` and the escape
// screen beside it discards the value, which is right for an overshoot screen and catastrophic
// here: a shove charge points at the hazard *by construction*, so a presence test is always true
// and would **veto every shove this step exists to enable**. `MotionTime` exposes `value()` and a
// strong ordering, so comparing the exit against the contact time costs nothing beyond the call.
//
// **The ray length decides how far the screen can see and nothing else.** With `L = fraction *
// diagonal` and a gap `g = |O - B| - r_self - r_opponent`, the admitted comparison `t_exit > g / L`
// is exactly `t_exit * L > g`, which is "the ground ends farther away than the opponent's near
// surface" and carries no `L` at all. `L` only bounds what the ray finds: ground ending beyond `L`
// is not seen and the charge is admitted. A longer authored screen is therefore monotonically
// stricter, and `charge_screen_diagonal_fraction = 1.0` is the strictest one a profile can author.
// A zero-length screen examined nothing, so it refuses nothing.
//
// A bot standing in void is refused, and that falls out rather than being written: an initially
// unsupported start exits at `t = 0`, which is greater than no non-negative gap.
//
// Returns false for a candidate that is not `kShoveSetup`, for a subject whose body is no longer
// published, and for an opponent exactly at the bot's own position, all three because there is no
// charge to admit rather than because something failed.
[[nodiscard]] bool tactical_charge_screen_admits(const Observation& observation,
                                                 const simulation::PhysicsBody& body,
                                                 const TacticalObjectiveCandidate& candidate,
                                                 const TacticalObjectivePolicy& policy);

// canonical: tactical_charge_alignment -- the gate that compares against the resultant rather than
// against the intent.
//
// **The burst is additive**, so a body already moving at 600 wu/s along +y that charges +x leaves
// at (450, 600): 750 wu/s, 53.1 degrees off the commanded ray. A ray cast due +x screens ground the
// body never crosses, and certifying the commanded angle certifies the wrong one. A bot cannot
// compute the resultant -- `charge_speed_fraction` is not published -- so this is the conservative
// published-state form of the same question: **refuse when the component of the committed velocity
// perpendicular to the commanded direction exceeds `kTacticalChargeAlignmentPerpendicularFraction`
// of the published normal ceiling.** At a perpendicular component equal to the whole ceiling the
// resultant is at least 45 degrees off for any burst up to the ceiling, which is why the fraction
// is a fraction of that ceiling and is below one.
//
// It is a shared unconditional gate and deliberately not a profile key: a per-profile alignment
// tolerance would be a combat knob whose only effect is to let a profile disable a safety screen.
[[nodiscard]] bool
tactical_charge_alignment_admits(const Observation& observation,
                                 const simulation::PhysicsBody& body,
                                 const TacticalObjectiveCandidate& candidate) noexcept;

// canonical: tactical_shield_closing -- a bounded linear extrapolation of published motion, named
// as the prediction it is.
//
// ADR 0008 authorises exactly this -- "perfect-shield anticipation uses visible trajectories plus
// profile reaction/error, not a collision callback available only to bots" -- and the owner's line
// is drawn at *unpublished physics*, not at arithmetic over published state. The authored caution
// is the **window**; the closing test itself is extrapolation, and calling it authored caution
// would be the dressing-up that same section forbids.
//
// One evaluation per opponent at the end of the window, never a swept root: `swept_geometry` owns
// the one collision equation in this tree and a bot does not get a second one. Both bodies are
// carried forward by `position + velocity * window`, and contact is the two published radii, so two
// bodies that declare no radius (`PhysicsBody::kUndeclaredRadius`) never close by this test.
//
// **The bias is early, and it is one-signed.** There is no drag term, because `drag_per_second`
// reaches no snapshot, so wherever drag is nonzero the predicted separation is smaller than the
// real one: about +2.3% at 8 ticks, +8.5% at 32 and +21.7% at 80 under the deployed 2.0, and +56% /
// +268% / +789% at the 40 both tactical browser fixtures author, where total coast is 15 world
// units at 600 wu/s and the predictor is describing a body that has already stopped. No bound makes
// this honest in every configuration in this tree; `kMaximumTacticalShieldAnticipationTicks` says
// which of them it is honest in, and the profile author owns the rest.
//
// A zero window anticipates nothing and returns false before extrapolating, exactly as a zero
// `prediction_horizon_ticks` casts no ray. This performs no `TacticalObjectiveWork` prediction
// step, because it is not the collector's work and is not counted in the collector's ceiling; its
// own bound is the one ascending join pass it makes, with no terrain query in it.
[[nodiscard]] bool tactical_opponent_closes_to_contact(const Observation& observation,
                                                       const simulation::PhysicsBody& body,
                                                       const TacticalObjectivePolicy& policy);

// canonical: tactical_candidate_utility -- the one written scoring order every profile shares.
//
//   preference = weight(kind) * (1 - normalized_distance) * opening
//   penalty    = escape_blocked ? (1 - risk_tolerance) : 0
//   score      = (preference - penalty) + held_bonus
//
// Multiply, subtract, then add, in that order and never reassociated, so two toolchains select the
// same candidate bit for bit. Every term lands in the unit interval when the weights do, which is
// the range every other controller weight in `controllers_limits.hpp` authors, and that is what
// makes the penalty commensurate: a zero-tolerance profile always prefers a candidate it can stop
// short of to one it cannot, and a full-tolerance profile ignores the screen entirely. Nothing here
// draws from the generator -- selection is deterministic, and the profile's seek and aim draws stay
// exactly where Step 15 put them, in the same order, so no authored profile's stream moves.
//
// **The opening multiplies and is never a fourth additive term**, and that is not a taste
// question. An additive exposure term destroys the commensurability the paragraph above rests on:
// the escape penalty is at most 1.0, so any positive coefficient on an added exposure could outrank
// it, and a maximally cautious profile could be made to prefer a cliff-blocked shove over a clean
// gate -- the exact statement this file claims is provable rather than a tuning accident. A third
// factor keeps the product inside [0,1] where the penalty already lives, and it still reorders two
// same-kind candidates, which is the entire job: with one kind in the set the weight is a common
// factor and reorders nothing, so exposure has to enter somewhere the *kind* is held constant.
//
// **A weight orders two kinds directly as soon as two kinds are in one set**, which the opponent-
// derived provider is the first thing to arrange: before it, every shipped mode yielded at most one
// candidate, a single kind's weight was a common factor over the whole set, and no weight could
// change a production outcome. What a weight reordered even then was a candidate against the escape
// penalty and against the hysteresis bonus, neither of which it scales:
// `w * (proximity_near - proximity_far) > 1 - tolerance` is the authored decision between the near
// objective a bot would overshoot into a cliff and the clear one further away.
//
// Rejected: nearest-first with a weight applied afterwards, which cannot express "prefer the hill
// even though the gate is nearer" -- that is the whole selection stage ADR 0008 names and Step 15
// did not have. Also rejected: normalising distance against the farthest candidate in the set,
// which is scale-free without an arena but lets an irrelevant third candidate reorder the first
// two.
[[nodiscard]] double tactical_candidate_score(const TacticalObjectiveCandidate& candidate,
                                              const TacticalObjectivePolicy& policy,
                                              bool held) noexcept;

// The score gap a challenger must beat before a held target is abandoned at the end of its lease.
// It is expressed in the same unit score space as everything else: an eighth of the full range is
// several decision passes of a bot's own drift toward its target at the published top speed, and
// far less than the preference a profile can author between two kinds, so hysteresis damps
// oscillation between two near-equal candidates without ever pinning a bot to a stale one.
inline constexpr double kTacticalHeldTargetBonus = 0.125;

// The index of the winning candidate, or an empty optional for an empty set -- which the caller
// has already handled as its own branch, because "nothing survived screening" is a different
// decision from "this one won". The maximum score wins; an exact tie falls to
// `tactical_candidate_precedes`, whose chain ends at the stable kind ordinal, so no selection can
// depend on the order providers happened to push candidates in.
//
// **The optional replaced a `candidates.size()` sentinel, and it fixed a contract rather than a
// crash.** There was no live out-of-bounds read to fix: the loop's first test short-circuited on
// `best == candidates.size()`, so on any non-empty set the first iteration assigned index 0
// unconditionally and the sentinel was unreachable. What was wrong was the *signature*, which
// promised a value the one caller indexed with. The reason that mattered is what the promise would
// have cost the first time anything made it reachable -- a zero-weight veto very nearly did. This
// vector is `reserve`d at the raw merged count and then filled only with terrain-screen survivors,
// so whenever anything is screened out `capacity > size` and index `size()` lands *inside* the live
// allocation: a sanitizer reports nothing without container-overflow annotations. The lease would
// then copy a candidate whose `key.kind` is an arbitrary byte, and the following pass would index a
// `std::array<double, kTacticalObjectiveKindCount>` with it -- a second, unbounded read. Silent,
// not caught.
//
// **A bare size check was rejected because it is a comment the next caller can ignore.** The
// optional makes the compiler close the hole at every call site that does not exist yet, which is
// the discipline this domain already applies one file over: `AuthoredObjectiveWeight`'s deleted
// default constructor, and the `default`-less switches over the kind enum that turn a sixth kind
// into a build failure rather than a silent zero.
[[nodiscard]] std::optional<std::size_t>
tactical_select_candidate(std::span<const TacticalObjectiveCandidate> candidates,
                          const TacticalObjectivePolicy& policy,
                          const std::optional<TacticalObjectiveKey>& held) noexcept;

// Kept from Step 15 deliberately, and no longer the selector: it is now the *tail* of the
// selection chain, the total order that breaks an exact score tie. Nearest first, then the key,
// whose first component is the stable kind ordinal. Keys are unique within one screened set -- one
// candidate per published hill or zone entity, one per race gate, one per screened opponent -- so
// this is a strict total order over that set and two runs cannot disagree.
[[nodiscard]] bool tactical_candidate_precedes(const TacticalObjectiveCandidate& left,
                                               const TacticalObjectiveCandidate& right) noexcept;

} // namespace blob_royale::controllers

#endif
