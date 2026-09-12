#ifndef BLOB_ROYALE_CONTROLLERS_CONTROLLERS_LIMITS_HPP
#define BLOB_ROYALE_CONTROLLERS_CONTROLLERS_LIMITS_HPP

#include "runtime_limits.hpp"
#include "simulation_limits.hpp"

#include <cstddef>
#include <cstdint>

namespace blob_royale::controllers {

// canonical: controllers_limits -- every bound this library's values enforce.
//
// The same discipline `runtime_limits.hpp` applies to a network boundary, applied to a composition
// boundary: a bot roster and a recorded command log are authored inputs, so every unbounded thing
// one of them can ask for is bounded here rather than in the value that consumes it, and each bound
// is tied by a static assertion to the limit below it that it must not exceed.
// related: runtime_limits.hpp -- the runtime's own ceilings, which these defer to.
// related: controller_host.hpp -- the host that enforces the roster bound.

// Controllers one host may hold at once. A hosted bot opens a session in the `ControllerDirectory`
// exactly as a network player does, so the honest ceiling is the directory's: a roster larger than
// that could not all obtain a `ControllerId` in the first place.
inline constexpr std::size_t kMaximumHostedControllerCount =
    runtime::kMaximumControllerDirectoryEntryCount;

static_assert(kMaximumHostedControllerCount <= runtime::kMaximumControllerDirectoryEntryCount,
              "a hosted controller holds a ControllerDirectory entry, so a host cannot hold more "
              "controllers than the directory holds entries");

// One `ScriptedReplayController`'s recorded log, in decision passes. A fixture authors this list
// literally, so the bound exists to make a runaway generator a named rejection at construction
// rather than an allocation failure at some later pass.
inline constexpr std::size_t kMaximumScriptedReplayStepCount = 65'536;

// How many decision passes one `WandererController` heading may be held. A reaction delay is a
// presentation-frame count, so the bound is generous rather than tight; what it rules out is a
// roster value that would make a bot decide once and then never again for the life of the process.
inline constexpr std::uint32_t kMaximumWandererReactionDelayFrames = 1'000;

// Committed ticks a controller waits before repeating a spawn request that has gone unanswered.
//
// **It is neither one-shot nor every-pass, and both extremes are wrong.** Asking once and never
// again would strand a bot silently and permanently whenever its spawn was refused -- a full
// mailbox or a closed session -- which is the degraded success this codebase refuses. Asking every
// pass creates a *second body for one controller*: a request is in flight for at least one tick, so
// a bot deciding faster than the world publishes would ask again while the engine was already
// seating it. A hundred milliseconds at the fixed 400 Hz tick rate is long enough that a request
// has been drained, applied, and published many times over, and short enough that a genuinely
// dropped spawn is recovered before a player would notice.
//
// **This narrows a gap it cannot close.** Nothing in the engine refuses a spawn from a controller
// that already drives a body -- `SpawnCommand` addresses a `ControllerId`, `InputBatch` keeps at
// most one spawn per controller *per tick*, and phase 0 creates an entity for each -- so a network
// client that sent two spawns in two ticks would get two blobs exactly as a bot would. The rule
// that closes it belongs in kernel phase 0 or in a mode's `SpawnPolicy`, not here.
inline constexpr std::uint64_t kSpawnRequestRetryTicks = simulation::kSimulationTicksPerSecond / 10;

static_assert(kSpawnRequestRetryTicks >= 2,
              "a spawn request is in flight for at least one tick, so a retry interval below two "
              "committed ticks would ask again while the engine was already seating the body");

// The inclusive range of a `ChaserController` aggression weight. It scales a unit direction, and a
// thrust direction component is a unit-interval intent, so a weight above one would produce a
// command the `CommandSink` refuses (`simulation_limits.hpp`
// `kMaximumThrustDirectionComponentMagnitude`).
inline constexpr double kMinimumChaserAggressionWeight = 0.0;
inline constexpr double kMaximumChaserAggressionWeight = 1.0;

// The inclusive range of a `HillSeekerController` weight, for the same reason: the approach weight
// scales a heading of magnitude at most one and the jitter weight bounds a per-component offset,
// and the sum is clamped to the thrust component range, so a weight above one would only ever be
// clamped away.
inline constexpr double kMinimumHillSeekerWeight = 0.0;
inline constexpr double kMaximumHillSeekerWeight = 1.0;

// A racer's recovery threshold is a positive fraction of the published road half-width.
inline constexpr double kMinimumRacerCautionFraction = 0.0;
inline constexpr double kMaximumRacerCautionFraction = 1.0;
inline constexpr double kDefaultRacerCautionFraction = 0.75;

// Tactical profile controls are active policy bounds, not future combat configuration.
inline constexpr double kMaximumTacticalObjectiveSeekProbability = 1.0;
inline constexpr std::uint64_t kMaximumTacticalReactionDelayTicks = 4'000;
inline constexpr double kMaximumTacticalAimError = 0.25;
inline constexpr std::uint64_t kMaximumTacticalTargetPersistenceTicks = 4'000;
inline constexpr std::size_t kMaximumTacticalObjectiveCandidateCount = 32;
inline constexpr std::uint64_t kTacticalSeedDomain = 0x7461'6374'6963'616cULL;

// The inclusive range of one per-kind objective weight, zero through this bound.
//
// A weight is the *preference* term of the utility score
// (`tactical_objective_candidates.hpp` `tactical_candidate_score`), so the same reasoning that
// bounds `kMaximumChaserAggressionWeight` and `kMaximumHillSeekerWeight` at one applies: a unit
// weight against a unit-normalised distance keeps every term of that score, and the escape penalty
// it is compared against, in one commensurate range. That is what lets the selection stage state a
// written operation order with no overflow or infinity case, and what makes "a zero-tolerance
// profile always prefers a candidate it can stop short of" a provable statement rather than a
// tuning accident.
//
// Zero is an authored value -- "this profile does not care about that objective" -- but *every*
// weight at zero is not, because that is the one weight set indistinguishable from an unauthored
// one: see `CONTROLLERS.TACTICAL_PROFILE_OBJECTIVE_WEIGHTS_DEGENERATE` in `tactical_profile.cpp`.
// How *many* weights a profile authors is not a bound and is not declared here; it is
// `kTacticalObjectiveKindCount`, beside the enum it counts.
inline constexpr double kMaximumTacticalObjectiveWeight = 1.0;

// The inclusive range of a profile's risk tolerance, zero through this bound.
//
// It scales away the penalty a screened-but-marginal candidate carries: at zero the penalty applies
// in full, at one it is cancelled and the escape screen is ignored. **It cannot run the other
// way.** No value of it adds score to a dangerous candidate, because a knob that did would be an
// appetite for danger -- which is Step 22b's aggression, and ADR 0008 § "Tactical profiles" forbids
// an inert aggression, charge or shield setting before its behavior exists
// (`docs/reviews/2026-09-12-tactical-pipeline-contract.md` § "Profile settings"). Stating the
// direction as a bound, rather than only in prose, is what keeps the two apart in review.
inline constexpr double kMaximumTacticalRiskTolerance = 1.0;

// Committed ticks a profile's prediction may look ahead, zero through this bound inclusive.
//
// **One second of committed time, and deliberately not the ten seconds the other tick bounds here
// allow.** A hosted bot decides at presentation cadence, roughly twenty times a second against the
// 400 Hz tick, so a one-second horizon already reaches past twenty of the bot's own future
// decisions, each of which re-observes and corrects. A horizon longer than that is a planner's, and
// this step ships no planner (`docs/reviews/2026-09-12-tactical-pipeline-contract.md` § "Bounded
// work"). It also keeps an honest claim honest: the only prediction this step performs is over the
// published `hill_motion` velocity, which does not drag, while the preflight measured a linear
// predictor of a *dragged* body diverging about 21% by 0.2 s at the deployed `drag_per_second=2.0`
// (`docs/reviews/2026-09-12-tactical-combat-preflight.md`). A future dragged predictor must argue
// for its own horizon rather than inherit a target-persistence-sized one.
inline constexpr std::uint64_t kMaximumTacticalPredictionHorizonTicks =
    simulation::kSimulationTicksPerSecond;

static_assert(kMaximumTacticalPredictionHorizonTicks <= kMaximumTacticalTargetPersistenceTicks,
              "a prediction informs the decision that holds a target, so a horizon reaching past "
              "the longest lease a profile may author would predict a world its own next decision "
              "has already replaced");

// How many opponents the shove provider may turn into candidates in one pass.
//
// **It is a filter width, not an authored setting, and it may never become one.** The opponent scan
// walks a published component store bounded at `simulation::kMaximumEntityCount = 4096` -- not at
// `kMaximumLobbySeatCount`, which its own comment calls a lobby bound and not a roster bound -- and
// the nearest-N pre-filter is what turns that into bounded per-pass work. An authored N would
// therefore move `kMaximumTacticalPredictionStepCount`, which
// `tactical_objective_candidates.hpp` states is derived precisely because no authored input can
// move it.
//
// It is set to the per-provider candidate budget rather than to some smaller round number, so the
// filter and the budget agree by construction: the shove provider cannot trip its own
// `require_candidate_count`, and the derived prediction ceiling counts the same 32 the throw does.
inline constexpr std::size_t kMaximumTacticalShoveCandidateCount =
    kMaximumTacticalObjectiveCandidateCount;

static_assert(kMaximumTacticalShoveCandidateCount <= kMaximumTacticalObjectiveCandidateCount,
              "the nearest-N opponent filter feeds one provider's candidate list, so a width above "
              "the per-provider budget would make that provider throw on a legal world");

// The inclusive range of a profile's charge screen length, zero through this bound.
//
// **It is a fraction of the observed arena diagonal, not a world-unit distance.**
// `Observation::terrain()` already hands a controller the validated bounds, so a fraction is
// computable from published state and means the same thing on the 960-unit fixture map and on a
// ten-kilometre one; an absolute scalar would mean two orders of magnitude of different things
// across the configurations already in this tree, and `simulation::kMaximumWorldDimension = 1e9`
// would be the only thing bounding it, which is to say nothing at all. One is a real ceiling rather
// than an arbitrary stop: a screen the size of the whole map is the strictest screen a profile can
// author, because the ray's length decides only how far the screen can see and a longer one can
// only find more ground endings (`tactical_objective_candidates.hpp`
// `tactical_charge_screen_admits` writes that monotonicity out). Zero is the other real answer --
// a screen that examined nothing
// and therefore refuses nothing.
inline constexpr double kMaximumTacticalChargeScreenDiagonalFraction = 1.0;

// Committed ticks a profile's shield anticipation may look ahead, zero through this bound
// inclusive.
//
// **A hundred milliseconds, for three reasons and not one.** It reuses the one 100 ms precedent
// already in this file, `kSpawnRequestRetryTicks`, rather than inventing a second time scale. It is
// two decision intervals at twenty snapshots a second, so the bot re-observes and corrects twice
// before the impact it is anticipating -- the same argument this file already makes for the
// 400-tick prediction horizon. And it is the last window in which the deployed `drag_per_second`
// error is a fraction rather than a multiple: the drag-free predictor is early by about +2.3% at 8
// ticks, +8.5% at 32 and +21.7% at 80 under the deployed 2.0.
//
// **No bound makes that predictor honest in every configuration in this tree**, and the constant
// should not pretend otherwise: at the 40 both tactical browser fixtures author, even 8 ticks is
// +56%. What a bound can do is name the configuration it is honest in and leave the rest to the
// profile author, who authors the window.
//
// **It is deliberately not the mode's perfect opening.** `blob_controllers` links only
// `blob_runtime` and `blob_simulation`, and `AbilityConfiguration` lives in `blob_gameplay`, so
// `shield_perfect_window_seconds` is unreachable from here; writing its current value into this
// file would be a second authoring home for a number an operator retunes in `[abilities]`. An
// anticipation window longer than that opening cannot produce a parry at all, and keeping the two
// in a sane relationship is the profile author's job precisely because the controller may not read
// the value.
inline constexpr std::uint64_t kMaximumTacticalShieldAnticipationTicks =
    simulation::kSimulationTicksPerSecond / 10;

static_assert(kMaximumTacticalShieldAnticipationTicks <= kMaximumTacticalPredictionHorizonTicks,
              "anticipating a contact is a prediction, so its window cannot reach past the longest "
              "horizon this library lets a profile predict over");

// How much of the published normal ceiling a charge's perpendicular velocity may reach.
//
// **It is stated here and authored nowhere.** The charge burst is additive, so a body already
// moving across the commanded ray leaves along the resultant and not along the ray the screen cast;
// the alignment gate refuses when the component of committed velocity perpendicular to the
// commanded direction exceeds this fraction of `MovementTuning::normal_top_speed`. At a
// perpendicular component equal to the whole ceiling the resultant is at least 45 degrees off the
// ray for any burst up to the ceiling, which is the angle at which the screened corridor stops
// describing where the body goes; a half of it holds the worst case to about 26.6 degrees. This is
// the one number in that gate, and it sits with the other bounds rather than beside the predicate
// so that a reader looking for what a profile may move finds it and finds that it is not one: a
// per-profile alignment tolerance would be a combat knob whose only effect is letting a profile
// switch off a safety screen.
inline constexpr double kTacticalChargeAlignmentPerpendicularFraction = 0.5;

static_assert(kTacticalChargeAlignmentPerpendicularFraction < 1.0,
              "at a perpendicular component equal to the published ceiling the resultant is at "
              "least 45 degrees off the commanded ray, so the gate must refuse below that");

} // namespace blob_royale::controllers

#endif
