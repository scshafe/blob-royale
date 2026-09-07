#include "wanderer_controller.hpp"

#include "commands/thrust_command.hpp"
#include "controllers_validation_error.hpp"
#include "entity_id.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::controllers {

std::unique_ptr<Controller> WandererController::create(const simulation::ControllerId controller,
                                                       const std::uint64_t seed) {
  return create(controller, seed, Personality{});
}

std::unique_ptr<Controller> WandererController::create(const simulation::ControllerId controller,
                                                       const std::uint64_t seed,
                                                       const Personality personality) {
  if (personality.reaction_delay_frames > kMaximumWandererReactionDelayFrames) {
    throw ControllersValidationError(ControllersValidationCode::kWandererReactionDelayOutOfRange,
                                     "wanderer_controller.personality.reaction_delay_frames",
                                     "reaction delay " +
                                         std::to_string(personality.reaction_delay_frames) +
                                         " frames exceeds the accepted maximum " +
                                         std::to_string(kMaximumWandererReactionDelayFrames));
  }
  return std::make_unique<WandererController>(controller, seed, personality);
}

WandererController::WandererController(const simulation::ControllerId controller,
                                       const std::uint64_t seed,
                                       const Personality personality) noexcept
    : Controller(controller), random_(simulation::DeterministicRandom::create(seed)),
      personality_(personality), heading_(simulation::Vector2::create(0.0, 0.0)) {}

std::vector<simulation::Command>
WandererController::decide_from_observation(const Observation& observation) {
  if (!observation.has_live_entity()) {
    // Pending or eliminated. Ask for a body and draw nothing, so the generator's stream stays a
    // function of the decisions this wanderer actually took rather than of how many passes its
    // seating happened to take.
    return request_body(observation);
  }

  if (passes_until_new_heading_ > 0) {
    // Still committed. Sending nothing is the honest expression of that: a thrust writes
    // `PhysicsBody::acceleration`, which persists until the next thrust, so re-sending the same
    // heading would change no committed value and would occupy a mailbox slot for no reason
    // (`src/simulation/commands/thrust_command.hpp`).
    --passes_until_new_heading_;
    return {};
  }

  heading_ = draw_heading();
  passes_until_new_heading_ = personality_.reaction_delay_frames;

  std::vector<simulation::Command> commands;
  commands.reserve(1);
  commands.push_back(simulation::Command{
      simulation::ThrustCommand{.entity = *observation.entity(), .direction = heading_}});
  return commands;
}

simulation::Vector2 WandererController::draw_heading() {
  // **Two draws in a written order, into named locals.** Writing this as
  // `Vector2::create(component(), component())` would make the heading depend on the compiler's
  // unspecified argument evaluation order, so one seed would produce two different bots on two
  // toolchains -- exactly the reproducibility this controller exists to have.
  //
  // The components are drawn directly rather than an angle put through `std::cos`/`std::sin`,
  // because the standard trigonometric functions are not bit-specified across libm implementations.
  // A controller is outside the tick and so is not bound by ADR 0003, but a bot whose heading
  // differs between two Linux images for no gameplay reason is a debugging cost with no benefit.
  // The consequence is a square rather than a circular heading distribution and headings of varying
  // magnitude, which is a wander either way: the mode's steering system scales a magnitude at most
  // one and normalizes anything above it (`src/gameplay/shared/thrust_steering_system.hpp`).
  const double x = (random_.next_unit_interval() * 2.0) - 1.0;
  const double y = (random_.next_unit_interval() * 2.0) - 1.0;
  return simulation::Vector2::create(x, y);
}

} // namespace blob_royale::controllers
