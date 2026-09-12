#include "chaser_controller.hpp"

#include "commands/thrust_command.hpp"
#include "controller_observation_queries.hpp"
#include "controller_steering.hpp"
#include "controllers_validation_error.hpp"
#include "player_snapshot.hpp"
#include "simulation_limits.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace blob_royale::controllers {
std::unique_ptr<Controller> ChaserController::create(const simulation::ControllerId controller,
                                                     const std::uint64_t seed) {
  return create(controller, seed, Personality{});
}

std::unique_ptr<Controller> ChaserController::create(const simulation::ControllerId controller,
                                                     const std::uint64_t /*seed*/,
                                                     const Personality personality) {
  if (!std::isfinite(personality.aggression_weight)) {
    throw ControllersValidationError(ControllersValidationCode::kChaserAggressionWeightNotFinite,
                                     "chaser_controller.personality.aggression_weight",
                                     "an aggression weight must be a finite number");
  }
  if (personality.aggression_weight < kMinimumChaserAggressionWeight ||
      personality.aggression_weight > kMaximumChaserAggressionWeight) {
    throw ControllersValidationError(
        ControllersValidationCode::kChaserAggressionWeightOutOfRange,
        "chaser_controller.personality.aggression_weight",
        "aggression weight " + std::to_string(personality.aggression_weight) +
            " is outside the accepted range [" + std::to_string(kMinimumChaserAggressionWeight) +
            ", " + std::to_string(kMaximumChaserAggressionWeight) + "]");
  }
  return std::make_unique<ChaserController>(controller, personality);
}

ChaserController::ChaserController(const simulation::ControllerId controller,
                                   const Personality personality) noexcept
    : Controller(controller), personality_(personality) {}

std::vector<simulation::Command>
ChaserController::decide_from_observation(const Observation& observation) {
  if (!observation.has_live_entity()) {
    target_.reset();
    return request_body(observation);
  }

  const simulation::EntityId self = *observation.entity();
  const std::span<const simulation::PlayerSnapshot> players = observation.snapshot().players();
  const simulation::PlayerSnapshot* const own = find_observed_player(observation.snapshot(), self);
  if (own == nullptr) {
    // The controller carries a `Controllable` but no `PhysicsBody` in this snapshot. Seating gives
    // both, so this is a state the engine does not currently reach; it is a defined empty answer
    // rather than a precondition, because a body a mode chose to remove is exactly the kind of
    // world-state disagreement a command source is required to survive.
    target_.reset();
    return {};
  }

  // `players()` is ascending `EntityId`, and the comparison below is strict, so the first candidate
  // at the minimum distance wins: the tie-break is the lowest `EntityId`.
  const simulation::PlayerSnapshot* nearest = nullptr;
  double nearest_squared_distance = 0.0;
  for (const simulation::PlayerSnapshot& candidate : players) {
    if (candidate.entity_id() == self) {
      continue;
    }
    const auto delta = controller_target_offset(own->position(), candidate.position());
    const double squared_distance = controller_squared_magnitude(delta);
    if (nearest == nullptr || squared_distance < nearest_squared_distance) {
      nearest = &candidate;
      nearest_squared_distance = squared_distance;
    }
  }

  if (nearest == nullptr) {
    // Alone in the world. No target, and therefore no thrust: the stored acceleration is left
    // exactly as it was.
    target_.reset();
    return {};
  }
  target_ = nearest->entity_id();

  const auto delta = controller_target_offset(own->position(), nearest->position());
  const double delta_x = delta.x;
  const double delta_y = delta.y;
  // `sqrt(x * x + y * y)` written out rather than `std::hypot`, which computes a different binary64
  // value for the same inputs, so a chaser and the steering system it feeds agree on what a
  // magnitude is (`src/gameplay/README.md` § "Steering").
  const double magnitude = controller_magnitude(delta);
  if (!(magnitude > 0.0)) {
    // Exactly co-located with the target: no direction toward it exists. Deciding nothing is the
    // only honest answer; any chosen direction would be arbitrary.
    return {};
  }

  const double scale = personality_.aggression_weight / magnitude;
  const simulation::Vector2 direction =
      simulation::Vector2::create(clamp_controller_direction_component(delta_x * scale),
                                  clamp_controller_direction_component(delta_y * scale));

  return request_thrust(observation, direction);
}

} // namespace blob_royale::controllers
