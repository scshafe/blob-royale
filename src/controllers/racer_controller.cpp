#include "racer_controller.hpp"

#include "commands/thrust_command.hpp"
#include "components/race_progress_component.hpp"
#include "controller_observation_queries.hpp"
#include "controller_steering.hpp"
#include "controllers_validation_error.hpp"
#include "mode_states/race_mode_state.hpp"
#include "physics_body.hpp"
#include "terrain_queries.hpp"
#include "vector2.hpp"

#include <cmath>
#include <cstddef>
#include <variant>

namespace blob_royale::controllers {

std::unique_ptr<Controller> RacerController::create(const simulation::ControllerId controller,
                                                    const std::uint64_t seed) {
  return create(controller, seed, Personality{});
}

std::unique_ptr<Controller> RacerController::create(const simulation::ControllerId controller,
                                                    const std::uint64_t seed,
                                                    const Personality personality) {
  if (!std::isfinite(personality.caution_fraction)) {
    throw ControllersValidationError(ControllersValidationCode::kRacerCautionFractionNotFinite,
                                     "racer_controller.personality.caution_fraction",
                                     "caution fraction must be finite");
  }
  if (personality.caution_fraction <= kMinimumRacerCautionFraction ||
      personality.caution_fraction > kMaximumRacerCautionFraction) {
    throw ControllersValidationError(ControllersValidationCode::kRacerCautionFractionOutOfRange,
                                     "racer_controller.personality.caution_fraction",
                                     "caution fraction must be in (0, 1]");
  }
  return std::make_unique<RacerController>(controller, seed, personality);
}

RacerController::RacerController(const simulation::ControllerId controller,
                                 const std::uint64_t seed, const Personality personality) noexcept
    : Controller(controller), seed_(seed), personality_(personality) {}

std::vector<simulation::Command>
RacerController::decide_from_observation(const Observation& observation) {
  if (!observation.entity().has_value()) {
    return request_body(observation);
  }
  const simulation::WorldSnapshot& snapshot = observation.snapshot();
  const auto* const course = std::get_if<simulation::RaceModeState>(&snapshot.match().mode_state());
  if (course == nullptr) {
    return {};
  }

  const simulation::EntityId self = *observation.entity();
  const auto* body = find_observed_component<simulation::PhysicsBody>(snapshot, self);
  const auto* progress = find_observed_component<simulation::RaceProgress>(snapshot, self);
  if (body == nullptr || progress == nullptr) {
    // Respawning or waiting on the grid. Only checkpoint_progress begins the race for this bot.
    return {};
  }
  const auto* road = observation.terrain().find_corridor(course->road.value());
  if (road == nullptr) {
    throw ControllersValidationError(ControllersValidationCode::kRacerCourseInvalid,
                                     "racer_controller.observation.road",
                                     "published race road is absent from the observation terrain");
  }
  if (course->checkpoints.empty() || progress->next_checkpoint > course->checkpoints.size()) {
    throw ControllersValidationError(ControllersValidationCode::kRacerCourseInvalid,
                                     "racer_controller.observation.course",
                                     "published race geometry or checkpoint progress is invalid");
  }

  simulation::Vector2 direction = simulation::Vector2::create(0.0, 0.0);
  if (progress->next_checkpoint < course->checkpoints.size()) {
    const auto nearest = simulation::corridor_project_to_centreline(*road, body->position());
    const simulation::Vector2& target =
        nearest.distance > personality_.caution_fraction * road->half_width()
            ? nearest.point
            : course->checkpoints[static_cast<std::size_t>(progress->next_checkpoint)];
    const auto delta = controller_target_offset(body->position(), target);
    const double dx = delta.x;
    const double dy = delta.y;
    const double magnitude = controller_magnitude(delta);
    if (magnitude > 0.0) {
      direction = simulation::Vector2::create(dx / magnitude, dy / magnitude);
    }
  }
  return request_thrust(observation, direction);
}

} // namespace blob_royale::controllers
