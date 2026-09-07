#include "controller.hpp"

#include "commands/spawn_command.hpp"
#include "controllers_limits.hpp"
#include "controllers_validation_error.hpp"

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

} // namespace blob_royale::controllers
