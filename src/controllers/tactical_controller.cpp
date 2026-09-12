#include "tactical_controller.hpp"

#include "components/controllable_component.hpp"
#include "components/stun_component.hpp"
#include "controller_observation_queries.hpp"
#include "controller_steering.hpp"
#include "controllers_validation_error.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace blob_royale::controllers {

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

std::vector<simulation::Command>
TacticalController::decide_from_observation(const Observation& observation) {
  // Provider and checked-window failures leave the generator, lease, and completed tick intact.
  // No failed observation is consumed, and retrying corrected public data at that tick is legal.
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
    clear_work(next);
    next.body.reset();
    next.observed_stun = false;
    return request_body(observation);
  }
  const auto self = *observation.entity();
  const auto& snapshot = observation.snapshot();
  const auto* body = find_observed_component<simulation::PhysicsBody>(snapshot, self);
  const auto* controllable = find_observed_component<simulation::Controllable>(snapshot, self);
  if (body == nullptr || body->is_static() || controllable == nullptr) {
    clear_work(next);
    next.body.reset();
    next.observed_stun = false;
    return {};
  }
  if (snapshot.match().phase() != simulation::MatchPhase::kRunning) {
    clear_work(next);
    next.body.reset();
    next.observed_stun = false;
    return request_thrust(observation, zero);
  }
  const BodyIdentity observed_body{self, controllable->input_generation};
  const auto* stun = find_observed_component<simulation::Stun>(snapshot, self);
  if (stun != nullptr && stun->window.contains(now)) {
    clear_work(next);
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
  const auto objectives = collect_tactical_objective_candidates(observation, *body);
  if (objectives.disposition == TacticalObjectiveDisposition::kWaiting) {
    if (next.lease) {
      next.reaction = simulation::TickWindow::create(now, profile_.reaction_delay_ticks());
    }
    next.lease.reset();
    next.held_direction.reset();
    return request_thrust(observation, zero);
  }
  if (objectives.disposition == TacticalObjectiveDisposition::kFinished) {
    next.lease.reset();
    next.held_direction = zero;
    return request_thrust(observation, zero);
  }
  if (next.lease) {
    const auto found = std::ranges::find(objectives.candidates, next.lease->candidate.key,
                                         &TacticalObjectiveCandidate::key);
    if (found == objectives.candidates.end()) {
      next.lease.reset();
      next.held_direction.reset();
      next.reaction = simulation::TickWindow::create(now, profile_.reaction_delay_ticks());
      cancelled = true;
    } else {
      // Refresh moving public targets without renewing their original acquisition window.
      next.lease->candidate = *found;
    }
  }
  if (objectives.candidates.empty()) {
    next.held_direction.reset();
    return cancelled ? request_thrust(observation, zero) : std::vector<simulation::Command>{};
  }
  if (!next.reaction->expired(now)) {
    return cancelled ? request_thrust(observation, zero) : std::vector<simulation::Command>{};
  }
  // One observed decision, not one decision for every missed interval. Check windows before RNG.
  next.reaction = simulation::TickWindow::create(now, profile_.reaction_delay_ticks());
  if (!next.lease || next.lease->window.expired(now)) {
    const auto best = std::ranges::min_element(objectives.candidates, tactical_candidate_precedes);
    next.lease =
        Lease{*best, simulation::TickWindow::create(now, profile_.target_persistence_ticks())};
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
    }
  }
  next.held_direction = direction;
  return request_thrust(observation, direction);
}

} // namespace blob_royale::controllers
