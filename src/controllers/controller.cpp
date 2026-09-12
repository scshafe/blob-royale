#include "controller.hpp"

#include "commands/spawn_command.hpp"
#include "commands/thrust_command.hpp"
#include "components/controllable_component.hpp"
#include "components/stun_component.hpp"
#include "controllers_limits.hpp"
#include "controllers_validation_error.hpp"
#include "physics_body.hpp"

#include <string>
#include <vector>

namespace blob_royale::controllers {

Controller::Controller(const simulation::ControllerId controller) noexcept
    : controller_(controller) {}

std::vector<simulation::Command> Controller::decide(const Observation& observation) {
  if (observation.controller() != controller_) {
    throw ControllersValidationError(ControllersValidationCode::kObservationControllerMismatch,
                                     "controller.decide.observation",
                                     "controller " + std::to_string(controller_.value()) +
                                         " was handed an observation built for controller " +
                                         std::to_string(observation.controller().value()));
  }
  if (!accepts_observation(observation)) {
    return {};
  }
  entity_ = observation.entity();
  if (entity_.has_value()) {
    // Seated. A later elimination asks again immediately rather than waiting out an interval that
    // was started before this body existed.
    last_spawn_request_tick_.reset();
  }
  return decide_from_observation(observation);
}

std::vector<simulation::Command> Controller::request_body(const Observation& observation) {
  const simulation::TickSequence observed = observation.tick_sequence();
  if (last_spawn_request_tick_.has_value() &&
      observed.value() < last_spawn_request_tick_->value() + kSpawnRequestRetryTicks) {
    // A request is still plausibly in flight. Asking again here is what would give one controller
    // two bodies.
    return {};
  }
  last_spawn_request_tick_ = observed;

  std::vector<simulation::Command> commands;
  commands.reserve(1);
  commands.push_back(simulation::Command{simulation::SpawnCommand{controller_}});
  return commands;
}

std::vector<simulation::Command>
Controller::request_thrust(const Observation& observation,
                           const simulation::Vector2& direction) const {
  if (observation.controller() != controller_ || !observation.entity().has_value()) {
    return {};
  }
  const auto self = *observation.entity();
  const auto& snapshot = observation.snapshot();
  const simulation::PhysicsBody* body = nullptr;
  for (const auto& entry : snapshot.components<simulation::PhysicsBody>()) {
    if (entry.entity == self) {
      body = &entry.value;
      break;
    }
  }
  if (body == nullptr || body->is_static()) {
    return {};
  }
  for (const auto& entry : snapshot.components<simulation::Stun>()) {
    if (entry.entity == self && entry.value.window.contains(observation.tick_sequence())) {
      return {};
    }
  }
  for (const auto& entry : snapshot.components<simulation::Controllable>()) {
    if (entry.entity == self && entry.value.controller_id == controller_) {
      return {simulation::Command{
          simulation::ThrustCommand{self, direction, entry.value.input_generation}}};
    }
  }
  return {};
}

} // namespace blob_royale::controllers
