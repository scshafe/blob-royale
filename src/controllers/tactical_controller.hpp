#ifndef BLOB_ROYALE_CONTROLLERS_TACTICAL_CONTROLLER_HPP
#define BLOB_ROYALE_CONTROLLERS_TACTICAL_CONTROLLER_HPP

#include "controller.hpp"
#include "deterministic_random.hpp"
#include "tactical_objective_candidates.hpp"
#include "tactical_profile.hpp"
#include "tactical_seed_identity.hpp"
#include "tick_window.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace blob_royale::controllers {

// canonical: tactical_decision_reason -- the closed set of branches one decision can come out of.
//
// `decide_next` returns an empty command vector from five branches and a zero thrust from four, and
// before this a test could only tell them apart by side effects: a draw that did not happen, a
// window that did not move. That is not a diagnosis, and neither is a log line -- free text cannot
// be asserted on and drifts from the code the first time a branch moves. This is the branch that
// produced the decision, `target_key()` is which candidate won, and `TacticalTargetHold` is what
// became of the held one; three closed answers rather than one flattened enum of every
// branch-times-hold pair, which would have to be renamed whenever either half grew.
//
// **A combat pass supersedes its movement branch, and that is deliberately not a fourth flattened
// axis.** An ability is decided after the movement branch and at most once per pass, so a value
// from the combat group below replaces the `kArrived`, `kSeekDeclined`, `kPursuing` or
// `kPursuingUnderRisk` that pass would otherwise have recorded. One value per
// branch-times-ability pair is exactly the product the paragraph above refuses, and nothing is
// lost by superseding: the returned commands still carry that pass's thrust, `target_key()` still
// names the candidate it was steering at, and `target()` still names where. What supersession buys
// is that every combat refusal names its own cause instead of hiding inside a movement branch that
// would read identically whether the bot had considered an ability or not.
//
// Every value is reachable and `tests/unit/controllers/tactical_controller_tests.cpp` reaches each
// one. A reason is decision state, so it is rolled back with everything else when an observation
// fails validation and is not consumed.
enum class TacticalDecisionReason : std::uint8_t {
  // No decision has completed yet, which is the state of a controller that has never been handed a
  // legal observation. Distinct from every decision below, none of which can produce it.
  kNotDecided = 0,
  kAwaitingBody,        // No owned entity: the decision is a spawn request, or its retry wait.
  kNoControllableBody,  // The owned entity carries no dynamic controllable body.
  kMatchNotRunning,     // Lobby, countdown or ended: hold still and keep no work.
  kStunned,             // An observed active stun window owns the input; decide nothing.
  kObjectivesWaiting,   // The provider has no published objective yet, and may later.
  kObjectivesFinished,  // The provider says this bot is done, as a finished race is.
  kNoScreenedCandidate, // Terrain screening left nothing to choose between.
  kAwaitingReaction,    // The profile's reaction window has not expired yet.
  kArrived,             // Inside the selected candidate's arrival radius: coast.
  kSeekDeclined,        // The profile's seek draw declined this pass: a personality, not a fault.
  kPursuing,            // Thrusting toward the selected candidate.
  kPursuingUnderRisk,   // Pursuing a candidate whose approach failed escape screening. A profile
                        // with weight enough to outrun the penalty reaches this deliberately, and
                        // a bot whose every candidate failed reaches it necessarily -- which is
                        // the required fallback: only bad options still produce the least bad
                        // decision, never a throw and never an empty pass.

  // The combat group. Each is decided after the movement branch above, at most one per pass, and
  // each names a cause rather than a stage so that a test can reach it on purpose.
  kShieldAnticipated,     // The closing test predicts contact inside the profile's window: pulse.
  kShieldUnavailable,     // That pulse was wanted and this bot's own published `Shield` windows --
                          // live protection, or a cooldown that has not expired -- already refuse
                          // it. The pass stops here rather than substituting a charge: wanting a
                          // shield and wanting a burst are different answers to the same tick.
  kChargeMisaligned,      // Perpendicular committed velocity puts the additive resultant too far
                          // off the commanded ray for the screened corridor to mean anything.
  kChargeGroundEndsFirst, // Support along the commanded ray ends before the opponent's near
                          // surface: the burst would leave the ground before it arrived.
  kChargeUnavailable,     // The burst was wanted and this bot's own published `Charge` cooldown, or
                          // its own live protection, already refuses it.
  kChargeCommitted        // Screened, aligned and admissible: a burst at the selected opponent.
};

// canonical: tactical_target_hold -- what became of the held target, on the same decision.
// The lease is the only target memory in this controller and hysteresis is a bonus applied to it,
// not a second memory alongside it; these values narrate that one lease.
enum class TacticalTargetHold : std::uint8_t {
  kNone = 0,        // No target was held before this decision and none is held after it.
  kAcquired,        // Nothing was held; utility selection chose one.
  kRetainedInLease, // The persistence window is still open, so no selection ran at all.
  kRetainedByBonus, // The window ended and the hysteresis bonus kept the same candidate.
  kSwitched,        // The window ended and a challenger beat the held candidate by more than it.
  kLost,            // The held candidate is no longer published; the lease is released.
  kReleased         // Waiting, finished, or an ineligible body cleared the lease.
};

// canonical: tactical_controller -- published observation, objective candidates, safety screening,
// profile-weighted utility selection, steering, and the two combat pulses. One algorithm for all
// profiles; no private schedules, future ticks, or pathfinder.
//
// The selection stage is what makes a profile mean something: Step 15 chose the nearest candidate
// with a kind ordinal breaking ties, so two profiles differing only in numbers chose the *same*
// candidate on the same frame and no differentiation could be proven. Scoring lives in
// `tactical_objective_candidates.hpp` as a pure function of a `TacticalObjectivePolicy`, and this
// class holds the one adapter from a profile to that policy.
//
// **Bounded work, and no planner.** A pass collects at most 32 raw candidates *per provider* -- a
// hard throw, not a truncation -- performs at most one closed-form prediction per raw mode
// candidate for the moving hill's intercept point and one per screened candidate for its escape
// ray, and casts at most one further terrain ray for a charge it is considering. There is no
// search, no replanning loop and no iteration over ticks: `objective_work()` publishes the
// collector's counts and `kMaximumTacticalPredictionStepCount` is the ceiling they cannot pass.
//
// **Combat is decided on the pursuing path, where a candidate has been selected, and never on the
// seek draw.** That draw sits inside the not-arrived branch, so gating an ability on it would mean
// a bot standing on its objective could never raise a shield -- precisely the state ADR 0008's
// "defend a stable interior" describes, and precisely when an opponent's charge arrives -- while
// adding a draw on the arrived branch would consume randomness that does not exist today and move
// every authored profile's stream. **This step adds no draw anywhere.** There are exactly two draw
// sites, the seek draw and the aim draw, in that order, where Step 15 put them, which is what the
// `draw_count()` assertions exist to hold still.
//
// **At most one ability command per pass, and a locally visible cooldown suppresses it.** `Shield`
// and `Charge` publish every window verbatim -- there is no `ComponentPublication` specialization
// for either -- so a bot reads its own protection, its own shield cooldown and its own charge
// cooldown and declines a pulse the tick would refuse anyway. That is the honest response to a real
// asymmetry rather than a second rate authority: the per-session token bucket is capacity 30,
// refill 20/s, charged per inbound frame before parsing, and it lives on `SessionWebSocketSession`.
// A bot goes `ControllerHost::decide_once -> CommandSink::submit` and never enters `blob_server`,
// so **a bot pays no rate cost at all**, while a human emitting thrust plus shield plus charge at
// twenty passes a second would drain the bucket in about 1.5 s and be disconnected -- and that
// human's client further self-limits at 50 ms for thrust and 300 ms for abilities. The command kind
// mask *is* symmetric; this is a denial-of-service control on an untrusted socket that an
// in-process bot does not need, and never a gameplay advantage.
//
// **Abilities sit behind the same reaction gate as everything else, and that derates them.** With
// the shipped `steady` profile's `reaction_delay_ticks = 80` against a 20-tick decision spacing,
// four of every five passes return `kAwaitingReaction`, so an ability has roughly a 20% duty cycle
// on top of the 1-to-21-tick activation jitter. There is deliberately no second, faster reflex path
// to hide that: ADR 0008 requires reaction to apply here in terms -- "visible trajectories **plus
// profile reaction/error**".
//
// **The shield is a defensive pulse first and a parry attempt only incidentally, and the arithmetic
// is why.** All three `Shield` windows date from one activation, so the next pulse is admissible at
// `activation + max(160, 360) = 360` ticks -- 0.9 s, eighteen decision passes -- and active
// protection blocks this bot's own charge for 160 of them, against a payoff window of 32 ticks. A
// bot would have to land inside that opening better than one time in eleven for a speculative
// shield to beat holding it, and ADR 0008 already concedes it cannot reliably do so. Those three
// numbers are `config/blob-royale.cfg`'s `[abilities]` tuning, which this library links no path to
// and may not read; that is also why keeping the anticipation window shorter than the mode's
// perfect opening is the profile author's job and not a constant here -- a window longer than the
// opening cannot produce a parry at all.
// related: tactical_objective_candidates.hpp -- providers, screening, and the scoring rule.
// related: tactical_profile.hpp -- the authored numbers, and the only thing a personality is.
// related: controller.hpp -- `request_shield` and `request_charge`, and the suppression they share.
class TacticalController final : public Controller {
public:
  static constexpr std::string_view kControllerKind = "tactical";
  // Copies the profile and authored identity. Invalid identity throws CONTROLLERS.*.
  [[nodiscard]] static std::unique_ptr<Controller> create(simulation::ControllerId controller,
                                                          const TacticalProfile& profile,
                                                          TacticalSeedIdentity identity);
  TacticalController(simulation::ControllerId controller, TacticalProfile profile,
                     TacticalSeedIdentity identity);
  [[nodiscard]] std::string_view kind() const noexcept override { return kControllerKind; }
  [[nodiscard]] const TacticalProfile& profile() const& noexcept { return profile_; }
  const TacticalProfile& profile() const&& = delete;
  [[nodiscard]] TacticalSeedIdentity seed_identity() const noexcept { return identity_; }
  [[nodiscard]] std::uint64_t draw_count() const noexcept {
    return state_.random ? state_.random->draw_count() : 0;
  }
  [[nodiscard]] std::optional<std::uint64_t> current_seed() const noexcept {
    return state_.random ? std::optional{state_.random->seed()} : std::nullopt;
  }
  [[nodiscard]] std::optional<simulation::TickSequence> last_completed_tick() const noexcept {
    return state_.last_completed_tick;
  }
  [[nodiscard]] std::optional<TacticalObjectiveKey> target_key() const noexcept {
    return state_.lease ? std::optional{state_.lease->candidate.key} : std::nullopt;
  }
  [[nodiscard]] std::optional<simulation::Vector2> target() const noexcept {
    return state_.lease ? std::optional{state_.lease->candidate.target} : std::nullopt;
  }
  [[nodiscard]] std::optional<simulation::TickWindow> reaction_window() const noexcept {
    return state_.reaction;
  }
  [[nodiscard]] std::optional<simulation::TickWindow> persistence_window() const noexcept {
    return state_.lease ? std::optional{state_.lease->window} : std::nullopt;
  }
  // Why the most recent completed decision came out the way it did, what became of the held
  // target, and what the pass cost. Exposed exactly as the windows above are, and rolled back with
  // them when an observation fails.
  [[nodiscard]] TacticalDecisionReason decision_reason() const noexcept { return state_.reason; }
  [[nodiscard]] TacticalTargetHold target_hold() const noexcept { return state_.hold; }
  [[nodiscard]] TacticalObjectiveWork objective_work() const noexcept { return state_.work; }

protected:
  [[nodiscard]] bool accepts_observation(const Observation& observation) const noexcept override;

private:
  struct BodyIdentity final {
    simulation::EntityId entity;
    std::optional<simulation::TickSequence> generation;
    friend bool operator==(const BodyIdentity&, const BodyIdentity&) = default;
  };
  struct Lease final {
    TacticalObjectiveCandidate candidate;
    simulation::TickWindow window;
  };
  struct State final {
    std::optional<simulation::TickSequence> last_completed_tick{};
    std::optional<simulation::TickSequence> seeded_running_tick{};
    std::optional<simulation::DeterministicRandom> random{};
    std::optional<BodyIdentity> body{};
    bool observed_stun{false};
    std::optional<simulation::TickWindow> reaction{};
    std::optional<Lease> lease{};
    std::optional<simulation::Vector2> held_direction{};
    TacticalDecisionReason reason{TacticalDecisionReason::kNotDecided};
    TacticalTargetHold hold{TacticalTargetHold::kNone};
    TacticalObjectiveWork work{};
  };
  [[nodiscard]] std::vector<simulation::Command>
  decide_from_observation(const Observation& observation) override;
  [[nodiscard]] std::vector<simulation::Command> decide_next(const Observation& observation,
                                                             State& next);
  // The pass's at-most-one ability command, decided after the movement branch and beside the thrust
  // rather than instead of it. Records the combat reason when it reached one and leaves the
  // movement branch's reason standing when there was no ability to consider at all, which is what
  // keeps an ordinary pass reading as `kPursuing` rather than as a refusal.
  [[nodiscard]] std::vector<simulation::Command>
  request_ability(const Observation& observation, const simulation::PhysicsBody& body,
                  const TacticalObjectivePolicy& policy, const TacticalObjectiveCandidate& selected,
                  State& next) const;
  static void clear_work(State& state) noexcept;
  // Clears the pass's work and records the branch that ended it. A lease that existed was released
  // by that clearing, which is why the hold is read before the clear and not after.
  static void clear_for(State& state, TacticalDecisionReason reason) noexcept;

  TacticalProfile profile_;
  TacticalSeedIdentity identity_;
  State state_{};
};

} // namespace blob_royale::controllers

#endif
