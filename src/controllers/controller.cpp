#include "controller.hpp"

#include "commands/charge_command.hpp"
#include "commands/shield_command.hpp"
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
namespace {

// canonical: controller_command_suppression -- the one condition every authored command shares.
//
// The observed `Controllable` a command may be authored through, or nullptr when this observation
// suppresses every command: a foreign observation, no seated entity, a missing or `is_static()`
// `PhysicsBody`, an observed `Stun` whose window contains the observed tick, or no `Controllable`
// naming this controller. One function rather than one copy per request helper, because a thrust
// and an ability that disagreed here would emit the ability on the pass the stun had just taken the
// body's input away -- and the stores are read in the same order for all three, so the suppression
// cannot drift by kind either.
[[nodiscard]] const simulation::Controllable*
commandable_controllable(const Observation& observation,
                         const simulation::ControllerId controller) noexcept {
  if (observation.controller() != controller || !observation.entity().has_value()) {
    return nullptr;
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
    return nullptr;
  }
  for (const auto& entry : snapshot.components<simulation::Stun>()) {
    if (entry.entity == self && entry.value.window.contains(observation.tick_sequence())) {
      return nullptr;
    }
  }
  for (const auto& entry : snapshot.components<simulation::Controllable>()) {
    if (entry.entity == self && entry.value.controller_id == controller) {
      return &entry.value;
    }
  }
  return nullptr;
}

} // namespace

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
  const auto* controllable = commandable_controllable(observation, controller_);
  if (controllable == nullptr) {
    return {};
  }
  return {simulation::Command{
      simulation::ThrustCommand{*observation.entity(), direction, controllable->input_generation}}};
}

std::vector<simulation::Command> Controller::request_shield(const Observation& observation) const {
  const auto* controllable = commandable_controllable(observation, controller_);
  if (controllable == nullptr) {
    return {};
  }
  return {simulation::Command{
      simulation::ShieldCommand{*observation.entity(), controllable->input_generation}}};
}

std::vector<simulation::Command>
Controller::request_charge(const Observation& observation,
                           const simulation::Vector2& direction) const {
  const auto* controllable = commandable_controllable(observation, controller_);
  if (controllable == nullptr) {
    return {};
  }
  return {simulation::Command{
      simulation::ChargeCommand{*observation.entity(), direction, controllable->input_generation}}};
}

} // namespace blob_royale::controllers
