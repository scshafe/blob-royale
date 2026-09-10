#include "racer_controller.hpp"

#include "commands/thrust_command.hpp"
#include "components/race_progress_component.hpp"
#include "controllers_validation_error.hpp"
#include "mode_states/race_mode_state.hpp"
#include "physics_body.hpp"
#include "vector2.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <variant>

namespace blob_royale::controllers {
namespace {

struct CourseTarget final {
  simulation::Vector2 point;
  double distance;
};

// This is a steering target in the published polyline, not a gameplay bounds decision. Its
// projection keeps RaceCourse's written arithmetic, but returns the point the bot must seek.
// No gameplay dependency is allowed here: controllers can see only published simulation values.
[[nodiscard]] CourseTarget nearest_course_target(const std::span<const simulation::Vector2> track,
                                                 const simulation::Vector2& position) {
  CourseTarget nearest{track.front(), std::numeric_limits<double>::infinity()};
  for (std::size_t index = 1; index < track.size(); ++index) {
    const simulation::Vector2& a = track[index - 1];
    const simulation::Vector2& b = track[index];
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double wx = position.x() - a.x();
    const double wy = position.y() - a.y();
    if (dx == 0.0 && dy == 0.0) {
      throw ControllersValidationError(ControllersValidationCode::kRacerCourseInvalid,
                                       "racer_controller.observation.track",
                                       "published course contains coincident consecutive nodes");
    }
    double t = (wx * dx + wy * dy) / (dx * dx + dy * dy);
    if (t < 0.0) {
      t = 0.0;
    } else if (t > 1.0) {
      t = 1.0;
    }
    const double cx = a.x() + (dx * t);
    const double cy = a.y() + (dy * t);
    const double ex = position.x() - cx;
    const double ey = position.y() - cy;
    const double distance = std::sqrt(ex * ex + ey * ey);
    if (distance < nearest.distance) {
      nearest = {simulation::Vector2::create(cx, cy), distance};
    }
  }
  return nearest;
}

} // namespace

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
  const simulation::PhysicsBody* body = nullptr;
  for (const auto& entry : snapshot.components<simulation::PhysicsBody>()) {
    if (entry.entity == self) {
      body = &entry.value;
      break;
    }
  }
  const simulation::RaceProgress* progress = nullptr;
  for (const auto& entry : snapshot.components<simulation::RaceProgress>()) {
    if (entry.entity == self) {
      progress = &entry.value;
      break;
    }
  }
  if (body == nullptr || progress == nullptr) {
    // Respawning or waiting on the grid. Only checkpoint_progress begins the race for this bot.
    return {};
  }
  if (course->track.size() < 2 || course->checkpoints.empty() ||
      !std::isfinite(course->track_half_width) || course->track_half_width <= 0.0 ||
      progress->next_checkpoint > course->checkpoints.size()) {
    throw ControllersValidationError(ControllersValidationCode::kRacerCourseInvalid,
                                     "racer_controller.observation.course",
                                     "published race geometry or checkpoint progress is invalid");
  }

  simulation::Vector2 direction = simulation::Vector2::create(0.0, 0.0);
  if (progress->next_checkpoint < course->checkpoints.size()) {
    const CourseTarget nearest = nearest_course_target(course->track, body->position());
    const simulation::Vector2& target =
        nearest.distance > personality_.caution_fraction * course->track_half_width
            ? nearest.point
            : course->checkpoints[static_cast<std::size_t>(progress->next_checkpoint)];
    const double dx = target.x() - body->position().x();
    const double dy = target.y() - body->position().y();
    const double magnitude = std::sqrt(dx * dx + dy * dy);
    if (magnitude > 0.0) {
      direction = simulation::Vector2::create(dx / magnitude, dy / magnitude);
    }
  }
  return {simulation::Command{simulation::ThrustCommand{.entity = self, .direction = direction}}};
}

} // namespace blob_royale::controllers
