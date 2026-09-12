#include "tactical_controller.hpp"

#include "components/charge_component.hpp"
#include "components/controllable_component.hpp"
#include "components/shield_component.hpp"
#include "components/stun_component.hpp"
#include "controller_observation_queries.hpp"
#include "controller_steering.hpp"
#include "controllers_validation_error.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace blob_royale::controllers {
namespace {

// canonical: tactical_profile_policy -- the one adapter from an authored profile to the plain
// numbers screening and selection read.
//
// It exists so that exactly one place in this library knows both types. Scoring takes a
// `TacticalObjectivePolicy` of validated doubles, which is what lets the utility rule be tested
// without building a configuration section, and what keeps the differentiation proof honest: the
// same profile *name* with a different weight is a different policy and the same seed, so any
// behaviour difference is attributable to the weight and not to `tactical_seed_for`, which mixes
// the name's length and every one of its bytes.
[[nodiscard]] TacticalObjectivePolicy policy_of(const TacticalProfile& profile) noexcept {
  TacticalObjectivePolicy policy;
  policy.objective_weights[tactical_objective_kind_ordinal(TacticalObjectiveKind::kHill)] =
      profile.objective_weight(TacticalObjectiveKind::kHill);
  policy.objective_weights[tactical_objective_kind_ordinal(TacticalObjectiveKind::kZone)] =
      profile.objective_weight(TacticalObjectiveKind::kZone);
  policy.objective_weights[tactical_objective_kind_ordinal(TacticalObjectiveKind::kRaceGate)] =
      profile.objective_weight(TacticalObjectiveKind::kRaceGate);
  policy.objective_weights[tactical_objective_kind_ordinal(TacticalObjectiveKind::kRaceRecovery)] =
      profile.objective_weight(TacticalObjectiveKind::kRaceRecovery);
  policy.objective_weights[tactical_objective_kind_ordinal(TacticalObjectiveKind::kShoveSetup)] =
      profile.objective_weight(TacticalObjectiveKind::kShoveSetup);
  policy.risk_tolerance = profile.risk_tolerance();
  policy.prediction_horizon_ticks = profile.prediction_horizon_ticks();
  policy.charge_screen_diagonal_fraction = profile.charge_screen_diagonal_fraction();
  policy.shield_anticipation_ticks = profile.shield_anticipation_ticks();
  return policy;
}

// canonical: tactical_visible_ability_refusal -- this bot's own published ability windows, read in
// the order `AbilitySystem` reads them.
//
// Not a correctness gate: a pulse the tick refuses consumes no cooldown, queues nothing and throws
// nothing, so nothing breaks if these are wrong. They exist because a bot pays none of the rate
// cost a human's socket pays, and declining a pulse it can already see will be refused is what
// keeps that asymmetry from reading as an advantage.
//
// A shield is refused by its own protection -- one activation cannot be raised twice -- and by its
// own cooldown, and admission requires both, which is exactly why an expired-protection `Shield`
// with a live cooldown is still published rather than swept.
[[nodiscard]] bool shield_pulse_refused(const simulation::WorldSnapshot& snapshot,
                                        const simulation::EntityId self,
                                        const simulation::TickSequence now) noexcept {
  const auto* shield = find_observed_component<simulation::Shield>(snapshot, self);
  return shield != nullptr &&
         (shield->shield_window().contains(now) || !shield->cooldown_window().expired(now));
}

// A charge is refused by its own cooldown and by this body's own live protection -- `AbilitySystem`
// spells that second term `!protection_active`, because a guarded body may not charge out of its
// own guard. A live *shield cooldown* is deliberately not one of them: the two cooldowns are
// separate keys on separate components and neither gates the other.
[[nodiscard]] bool charge_pulse_refused(const simulation::WorldSnapshot& snapshot,
                                        const simulation::EntityId self,
                                        const simulation::TickSequence now) noexcept {
  const auto* shield = find_observed_component<simulation::Shield>(snapshot, self);
  if (shield != nullptr && shield->shield_window().contains(now)) {
    return true;
  }
  const auto* charge = find_observed_component<simulation::Charge>(snapshot, self);
  return charge != nullptr && !charge->cooldown_window().expired(now);
}

} // namespace

std::unique_ptr<Controller> TacticalController::create(const simulation::ControllerId controller,
                                                       const TacticalProfile& profile,
                                                       const TacticalSeedIdentity identity) {
  return std::make_unique<TacticalController>(controller, profile, identity);
}

TacticalController::TacticalController(const simulation::ControllerId controller,
                                       TacticalProfile profile, const TacticalSeedIdentity identity)
    : Controller(controller), profile_(std::move(profile)), identity_(identity) {
  validate_tactical_seed_identity(identity_);
}

bool TacticalController::accepts_observation(const Observation& observation) const noexcept {
  return !state_.last_completed_tick || observation.tick_sequence() > *state_.last_completed_tick;
}

void TacticalController::clear_work(State& state) noexcept {
  state.reaction.reset();
  state.lease.reset();
  state.held_direction.reset();
}

void TacticalController::clear_for(State& state, const TacticalDecisionReason reason) noexcept {
  state.hold = state.lease ? TacticalTargetHold::kReleased : TacticalTargetHold::kNone;
  state.reason = reason;
  state.work = {};
  clear_work(state);
}

std::vector<simulation::Command>
TacticalController::decide_from_observation(const Observation& observation) {
  // Provider and checked-window failures leave the generator, lease, reason, and completed tick
  // intact. No failed observation is consumed, and retrying corrected public data at that tick is
  // legal.
  State next = state_;
  auto commands = decide_next(observation, next);
  next.last_completed_tick = observation.tick_sequence();
  state_ = std::move(next);
  return commands;
}

std::vector<simulation::Command> TacticalController::decide_next(const Observation& observation,
                                                                 State& next) {
  const auto now = observation.tick_sequence();
  const auto zero = simulation::Vector2::create(0.0, 0.0);
  if (!observation.entity()) {
    clear_for(next, TacticalDecisionReason::kAwaitingBody);
    next.body.reset();
    next.observed_stun = false;
    return request_body(observation);
  }
  const auto self = *observation.entity();
  const auto& snapshot = observation.snapshot();
  const auto* body = find_observed_component<simulation::PhysicsBody>(snapshot, self);
  const auto* controllable = find_observed_component<simulation::Controllable>(snapshot, self);
  if (body == nullptr || body->is_static() || controllable == nullptr) {
    clear_for(next, TacticalDecisionReason::kNoControllableBody);
    next.body.reset();
    next.observed_stun = false;
    return {};
  }
  if (snapshot.match().phase() != simulation::MatchPhase::kRunning) {
    clear_for(next, TacticalDecisionReason::kMatchNotRunning);
    next.body.reset();
    next.observed_stun = false;
    return request_thrust(observation, zero);
  }
  const BodyIdentity observed_body{self, controllable->input_generation};
  const auto* stun = find_observed_component<simulation::Stun>(snapshot, self);
  if (stun != nullptr && stun->window.contains(now)) {
    clear_for(next, TacticalDecisionReason::kStunned);
    next.body = observed_body;
    next.observed_stun = true;
    return {};
  }
  const auto running_tick = snapshot.match().running_started_tick();
  if (running_tick > now) {
    throw ControllersValidationError(ControllersValidationCode::kTacticalRunningTickInvalid,
                                     "tactical_controller.running_started_tick",
                                     "running identity is later than the observed tick");
  }
  bool cancelled = next.held_direction.has_value();
  const bool new_running_identity = next.seeded_running_tick != running_tick;
  if (new_running_identity) {
    clear_work(next);
    next.body.reset();
    next.random = simulation::DeterministicRandom::create(
        tactical_seed_for(identity_, profile_.name(), running_tick));
    next.seeded_running_tick = running_tick;
  }
  const bool changed_body = next.body != observed_body || next.observed_stun;
  next.body = observed_body;
  next.observed_stun = false;
  if (changed_body) {
    clear_work(next);
  } else if (!new_running_identity) {
    cancelled = false;
  }
  if (!next.reaction) {
    // Eligibility is a running dynamic body, not the presence of a target. Empty publications
    // cannot postpone this first absolute deadline forever.
    next.reaction = simulation::TickWindow::create(now, profile_.reaction_delay_ticks());
  }
  const auto policy = policy_of(profile_);
  const auto objectives = collect_tactical_objective_candidates(observation, *body, policy);
  next.work = objectives.work;
  if (objectives.disposition == TacticalObjectiveDisposition::kWaiting) {
    if (next.lease) {
      next.reaction = simulation::TickWindow::create(now, profile_.reaction_delay_ticks());
    }
    next.hold = next.lease ? TacticalTargetHold::kReleased : TacticalTargetHold::kNone;
    next.reason = TacticalDecisionReason::kObjectivesWaiting;
    next.lease.reset();
    next.held_direction.reset();
    return request_thrust(observation, zero);
  }
  if (objectives.disposition == TacticalObjectiveDisposition::kFinished) {
    next.hold = next.lease ? TacticalTargetHold::kReleased : TacticalTargetHold::kNone;
    next.reason = TacticalDecisionReason::kObjectivesFinished;
    next.lease.reset();
    next.held_direction = zero;
    return request_thrust(observation, zero);
  }
  next.hold = next.lease ? TacticalTargetHold::kRetainedInLease : TacticalTargetHold::kNone;
  if (next.lease) {
    const auto found = std::ranges::find(objectives.candidates, next.lease->candidate.key,
                                         &TacticalObjectiveCandidate::key);
    if (found == objectives.candidates.end()) {
      next.lease.reset();
      next.held_direction.reset();
      next.reaction = simulation::TickWindow::create(now, profile_.reaction_delay_ticks());
      next.hold = TacticalTargetHold::kLost;
      cancelled = true;
    } else {
      // Refresh moving public targets, including a moving hill's recomputed intercept point,
      // without renewing their original acquisition window.
      next.lease->candidate = *found;
    }
  }
  if (objectives.candidates.empty()) {
    next.reason = TacticalDecisionReason::kNoScreenedCandidate;
    next.held_direction.reset();
    return cancelled ? request_thrust(observation, zero) : std::vector<simulation::Command>{};
  }
  if (!next.reaction->expired(now)) {
    next.reason = TacticalDecisionReason::kAwaitingReaction;
    return cancelled ? request_thrust(observation, zero) : std::vector<simulation::Command>{};
  }
  // One observed decision, not one decision for every missed interval. Check windows before RNG.
  next.reaction = simulation::TickWindow::create(now, profile_.reaction_delay_ticks());
  if (!next.lease || next.lease->window.expired(now)) {
    // The lease is the hysteresis: while its window is open no selection runs at all, and at the
    // moment it ends the candidate it was holding carries `kTacticalHeldTargetBonus` into the one
    // comparison that can replace it. That is the whole anti-oscillation mechanism, and it is the
    // existing memory rather than a second one beside it.
    const std::optional<TacticalObjectiveKey> held =
        next.lease ? std::optional{next.lease->candidate.key} : std::nullopt;
    const auto chosen = tactical_select_candidate(objectives.candidates, policy, held);
    const auto& winner = objectives.candidates[chosen];
    if (!held.has_value()) {
      next.hold = TacticalTargetHold::kAcquired;
    } else if (winner.key == *held) {
      next.hold = TacticalTargetHold::kRetainedByBonus;
    } else {
      next.hold = TacticalTargetHold::kSwitched;
    }
    next.lease =
        Lease{winner, simulation::TickWindow::create(now, profile_.target_persistence_ticks())};
  }
  const auto& selected = next.lease->candidate;
  simulation::Vector2 direction = zero;
  if (selected.squared_distance > selected.arrival_radius * selected.arrival_radius) {
    const double choice = next.random->next_unit_interval();
    if (choice < profile_.objective_seek_probability()) {
      const auto delta = controller_target_offset(body->position(), selected.target);
      const double magnitude = controller_magnitude(delta);
      const double ux = delta.x / magnitude;
      const double uy = delta.y / magnitude;
      const double aim = next.random->next_unit_interval();
      const double error = ((aim * 2.0) - 1.0) * profile_.aim_error();
      const double rx = ux - error * uy;
      const double ry = uy + error * ux;
      const double rotated_magnitude = std::sqrt(rx * rx + ry * ry);
      direction =
          simulation::Vector2::create(clamp_controller_direction_component(rx / rotated_magnitude),
                                      clamp_controller_direction_component(ry / rotated_magnitude));
      next.reason = selected.escape_blocked ? TacticalDecisionReason::kPursuingUnderRisk
                                            : TacticalDecisionReason::kPursuing;
    } else {
      next.reason = TacticalDecisionReason::kSeekDeclined;
    }
  } else {
    next.reason = TacticalDecisionReason::kArrived;
  }
  next.held_direction = direction;
  auto commands = request_thrust(observation, direction);
  // The ability is decided here, after the movement branch and outside both draws, and it rides
  // beside the thrust rather than replacing it: a bot that is steering somewhere is still the bot
  // that has to defend itself on the way. `request_ability` returns zero or one command and only
  // the first is taken, so "at most one ability per pass" is enforced at the join rather than
  // asserted about the branches above it.
  const auto ability = request_ability(observation, *body, policy, selected, next);
  if (!ability.empty()) {
    commands.push_back(ability.front());
  }
  return commands;
}

std::vector<simulation::Command>
TacticalController::request_ability(const Observation& observation,
                                    const simulation::PhysicsBody& body,
                                    const TacticalObjectivePolicy& policy,
                                    const TacticalObjectiveCandidate& selected, State& next) const {
  const auto& snapshot = observation.snapshot();
  const auto self = *observation.entity();
  const auto now = observation.tick_sequence();
  // **Shield before charge, and for the engine's reason rather than for taste.** `AbilitySystem`
  // spells its priority `charge_admissible && !shield_eligible`, so a local order that preferred
  // offence would be one the tick contradicts on the pass both were wanted. The closing test is
  // over every published opponent and not only the selected candidate's: a shield answers whoever
  // is arriving, and scoping it to the target this bot chose to pursue would be the same defect as
  // gating it on the seek draw, one branch further down.
  if (tactical_opponent_closes_to_contact(observation, body, policy)) {
    if (shield_pulse_refused(snapshot, self, now)) {
      next.reason = TacticalDecisionReason::kShieldUnavailable;
      return {};
    }
    next.reason = TacticalDecisionReason::kShieldAnticipated;
    return request_shield(observation);
  }
  // A charge is an action on a chosen target, so unlike the shield it is scoped to the selection.
  // One lookup answers both ways that can fail: the collector's exported
  // `tactical_shove_opponent_body` returns nullptr for a candidate of any other kind and for an
  // opponent that has left the snapshot, and in both this bot has nothing to aim a burst at. It is
  // the one owner of "which body is this candidate about?", so this file does not keep a second
  // copy of that scan for the sake of the heading it needs below.
  const auto* opponent = tactical_shove_opponent_body(snapshot, selected);
  if (opponent == nullptr) {
    return {};
  }
  const auto delta = controller_target_offset(body.position(), opponent->position());
  const double distance = controller_magnitude(delta);
  if (!(distance > 0.0)) {
    // Two published centres at the same point leave no commanded direction, so there is nothing for
    // either gate below to be about and no cause for a reason code to name. The movement branch's
    // reason stands, because this pass did not refuse a charge -- it never had one to refuse.
    return {};
  }
  if (!tactical_charge_alignment_admits(observation, body, selected)) {
    next.reason = TacticalDecisionReason::kChargeMisaligned;
    return {};
  }
  if (!tactical_charge_screen_admits(observation, body, selected, policy)) {
    next.reason = TacticalDecisionReason::kChargeGroundEndsFirst;
    return {};
  }
  // Last, so that a bot on cooldown still reports the more specific refusal when it has one and
  // this branch is only reached by a charge that would otherwise have been sent.
  if (charge_pulse_refused(snapshot, self, now)) {
    next.reason = TacticalDecisionReason::kChargeUnavailable;
    return {};
  }
  next.reason = TacticalDecisionReason::kChargeCommitted;
  // **The burst is aimed at the opponent, not at the standing point.** S is where the bot stands to
  // shove *from* -- one standoff behind the opponent, on the side away from the hazard -- so a
  // charge along `B -> S` would push nothing anywhere. Normalised in this file's written order,
  // subtract then magnitude then divide, and clamped for the reason the seek path clamps: a
  // component rounded a bit past one is a hard `InputBatch::create` refusal, and the clamp is the
  // cheaper of the two places to notice.
  const double heading_x = clamp_controller_direction_component(delta.x / distance);
  const double heading_y = clamp_controller_direction_component(delta.y / distance);
  return request_charge(observation, simulation::Vector2::create(heading_x, heading_y));
}

} // namespace blob_royale::controllers
