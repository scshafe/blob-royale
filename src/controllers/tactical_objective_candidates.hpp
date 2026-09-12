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
  kRaceRecovery = 3
};
inline constexpr std::size_t kTacticalObjectiveKindCount = 4;
[[nodiscard]] constexpr std::size_t
tactical_objective_kind_ordinal(const TacticalObjectiveKind kind) noexcept {
  return static_cast<std::size_t>(kind);
}
static_assert(tactical_objective_kind_ordinal(TacticalObjectiveKind::kRaceRecovery) + 1 ==
                  kTacticalObjectiveKindCount,
              "a profile authors one weight per kind, so the count must follow the last ordinal");

struct TacticalObjectiveKey final {
  TacticalObjectiveKind kind;
  // Hill/zone entity, or the race's next gate index. Recovery changes kind but keeps that gate.
  std::uint64_t subject;
  friend auto operator<=>(const TacticalObjectiveKey&, const TacticalObjectiveKey&) = default;
};

// canonical: tactical_objective_candidate -- one objective and the two screening answers selection
// reads. `squared_distance` stays the raw geometry the arrival test compares against a radius;
// `normalized_distance` is that same distance divided by the published arena diagonal, so one
// authored weight and one authored risk tolerance mean the same thing on this 960-unit fixture map
// and on a ten-kilometre one. A raw provider candidate carries zero and false for both: only
// `collect_tactical_objective_candidates` holds the terrain and the arena, so only it may answer
// them, and a candidate that never reached screening is never selected from.
struct TacticalObjectiveCandidate final {
  TacticalObjectiveKey key;
  simulation::Vector2 target;
  double arrival_radius;
  double squared_distance;
  // Distance to `target` over the published arena diagonal, clamped to [0,1] by screening.
  double normalized_distance{};
  // Support ends along the horizon ray toward `target`; screening writes it. See the collector.
  bool escape_blocked{};
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
struct TacticalObjectivePolicy final {
  // Indexed by `tactical_objective_kind_ordinal`. A weight scales the proximity term only.
  std::array<double, kTacticalObjectiveKindCount> objective_weights{};
  // 1.0 ignores escape screening entirely; 0.0 applies the full unit penalty. Between them it
  // scales how far a screened-but-marginal candidate is ranked down, and nothing else.
  double risk_tolerance{};
  // Committed ticks a prediction may look ahead. Zero performs, and therefore counts, none at all.
  std::uint64_t prediction_horizon_ticks{};
  friend bool operator==(const TacticalObjectivePolicy&, const TacticalObjectivePolicy&) = default;
};

// Bounded work, counted where it happens rather than estimated afterwards. `raw_candidate_count` is
// what the 32-candidate throw is measured against, before any terrain work.
struct TacticalObjectiveWork final {
  std::size_t raw_candidate_count{};
  std::size_t screened_candidate_count{};
  std::size_t prediction_step_count{};
  friend bool operator==(const TacticalObjectiveWork&, const TacticalObjectiveWork&) = default;
};

// One pass extrapolates at most one moving-hill centre per raw candidate and casts at most one
// escape ray per screened candidate, and screened candidates are a subset of raw ones, which
// `require_candidate_count` already throws above 32 on. This ceiling is therefore *derived* rather
// than authored, which is why it is stated beside the loops that cannot exceed it rather than in
// `controllers_limits.hpp`: that file states the bounds an authored input must satisfy, and no
// authored input can move this one -- it follows from the candidate bound stated there and from
// the two loops below.
inline constexpr std::size_t kMaximumTacticalPredictionStepCount =
    2 * kMaximumTacticalObjectiveCandidateCount;

enum class TacticalObjectiveDisposition { kReady, kWaiting, kFinished };
struct TacticalObjectiveCandidates final {
  TacticalObjectiveDisposition disposition{TacticalObjectiveDisposition::kReady};
  std::vector<TacticalObjectiveCandidate> candidates{};
  TacticalObjectiveWork work{};
};

// canonical: tactical_objective_candidates -- closed public-schema providers, terrain screening,
// escape screening, and the moving hill's intercept point.
//
// The body is the caller's resolved owned dynamic body. At most 32 raw candidates, checked before
// terrain work; malformed objectives and unsupported schemas throw CONTROLLERS.*. Canonical terrain
// numerical failures propagate, never become a partial or silently repaired result.
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
// **The 32-candidate throw is left exactly as Step 15 wrote it.** It is enforced on the raw
// per-provider count before terrain work, and it is honest today only because exactly one provider
// runs per running schema, so "raw count" and "this provider's count" are the same number. Step
// 22b's opponent-derived provider is the first that can run alongside another one and the first
// bounded by 64 seats rather than by 32 candidates; it must make the accounting per-provider, or
// one provider will exhaust the shared budget and the host will isolate the throw and leave the bot
// silently inert for the pass. Escape screening adds no per-opponent candidate, so this step cannot
// trip the bound and does not raise it.
// related: ../simulation/terrain_queries.hpp -- the one owner of support geometry.
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
// charge needs `charge_speed_fraction`; a parry needs `shield_perfect_window_seconds`. Those are
// Step 22b's, and `docs/reviews/2026-09-12-tactical-combat-preflight.md` records why.
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

// canonical: tactical_candidate_utility -- the one written scoring order every profile shares.
//
//   preference = weight(kind) * (1 - normalized_distance)
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
// **A weight is not an inert knob today, even though one running schema publishes one kind.** With
// a single kind in the set the weight is a common factor and cannot reorder two candidates by
// itself; what it reorders is a candidate against the escape penalty and against the hysteresis
// bonus, neither of which it scales. `w * (proximity_near - proximity_far) > 1 - tolerance` is the
// authored decision between the near objective a bot would overshoot into a cliff and the clear one
// further away, and it is settled by the weight alone. Step 22b's opponent-derived provider is the
// first that publishes two kinds at once, and then the weight orders them directly.
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

// The index of the winning candidate, or `candidates.size()` for an empty set -- which the caller
// has already handled as its own branch, because "nothing survived screening" is a different
// decision from "this one won". The maximum score wins; an exact tie falls to
// `tactical_candidate_precedes`, whose chain ends at the stable kind ordinal, so no selection can
// depend on the order providers happened to push candidates in.
[[nodiscard]] std::size_t
tactical_select_candidate(std::span<const TacticalObjectiveCandidate> candidates,
                          const TacticalObjectivePolicy& policy,
                          const std::optional<TacticalObjectiveKey>& held) noexcept;

// Kept from Step 15 deliberately, and no longer the selector: it is now the *tail* of the
// selection chain, the total order that breaks an exact score tie. Nearest first, then the key,
// whose first component is the stable kind ordinal. Keys are unique within one screened set -- one
// candidate per published hill or zone entity, one per race gate -- so this is a strict total order
// over that set and two runs cannot disagree.
[[nodiscard]] bool tactical_candidate_precedes(const TacticalObjectiveCandidate& left,
                                               const TacticalObjectiveCandidate& right) noexcept;

} // namespace blob_royale::controllers

#endif
