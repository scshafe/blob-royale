#include "chaser_controller.hpp"

#include "commands/thrust_command.hpp"
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
namespace {

// The published body of one entity in this snapshot's player projection, or nullptr when the
// snapshot published none for it.
[[nodiscard]] const simulation::PlayerSnapshot*
find_player(const std::span<const simulation::PlayerSnapshot> players,
            const simulation::EntityId entity) noexcept {
  for (const simulation::PlayerSnapshot& player : players) {
    if (player.entity_id() == entity) {
      return &player;
    }
  }
  return nullptr;
}

// A thrust direction component is a unit-interval intent and the `CommandSink` refuses one outside
// `[-1, 1]`, so the boundary's range is enforced at the source rather than discovered as a refusal.
// It is not the steering magnitude clamp, which happens exactly once, in the mode's steering system
// (`src/gameplay/README.md` § "Steering"): this only keeps a correctly rounded `delta / magnitude`
// -- whose true value is at most one -- from landing one unit in the last place above it.
[[nodiscard]] double clamped_direction_component(const double component) noexcept {
  const double limit = simulation::kMaximumThrustDirectionComponentMagnitude;
  if (component > limit) {
    return limit;
  }
  if (component < -limit) {
    return -limit;
  }
  return component;
}

} // namespace

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
  const simulation::PlayerSnapshot* const own = find_player(players, self);
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
    const double delta_x = candidate.position().x() - own->position().x();
    const double delta_y = candidate.position().y() - own->position().y();
    const double squared_distance = (delta_x * delta_x) + (delta_y * delta_y);
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

  const double delta_x = nearest->position().x() - own->position().x();
  const double delta_y = nearest->position().y() - own->position().y();
  // `sqrt(x * x + y * y)` written out rather than `std::hypot`, which computes a different binary64
  // value for the same inputs, so a chaser and the steering system it feeds agree on what a
  // magnitude is (`src/gameplay/README.md` § "Steering").
  const double magnitude = std::sqrt((delta_x * delta_x) + (delta_y * delta_y));
  if (!(magnitude > 0.0)) {
    // Exactly co-located with the target: no direction toward it exists. Deciding nothing is the
    // only honest answer; any chosen direction would be arbitrary.
    return {};
  }

  const double scale = personality_.aggression_weight / magnitude;
  const simulation::Vector2 direction = simulation::Vector2::create(
      clamped_direction_component(delta_x * scale), clamped_direction_component(delta_y * scale));

  return request_thrust(observation, direction);
}

} // namespace blob_royale::controllers
