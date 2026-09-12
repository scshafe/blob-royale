#include "hill_seeker_controller.hpp"

#include "commands/thrust_command.hpp"
#include "component_store.hpp"
#include "components/hill_component.hpp"
#include "controller_observation_queries.hpp"
#include "controller_steering.hpp"
#include "controllers_validation_error.hpp"
#include "player_snapshot.hpp"
#include "simulation_limits.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace blob_royale::controllers {
namespace {

void require_weight(const double weight, const std::string& context) {
  if (!std::isfinite(weight)) {
    throw ControllersValidationError(ControllersValidationCode::kHillSeekerWeightNotFinite, context,
                                     "a hill seeker weight must be a finite number");
  }
  if (weight < kMinimumHillSeekerWeight || weight > kMaximumHillSeekerWeight) {
    throw ControllersValidationError(
        ControllersValidationCode::kHillSeekerWeightOutOfRange, context,
        "weight " + std::to_string(weight) + " is outside the accepted range [" +
            std::to_string(kMinimumHillSeekerWeight) + ", " +
            std::to_string(kMaximumHillSeekerWeight) + "]");
  }
}

} // namespace

std::unique_ptr<Controller> HillSeekerController::create(const simulation::ControllerId controller,
                                                         const std::uint64_t seed) {
  return create(controller, seed, Personality{});
}

std::unique_ptr<Controller> HillSeekerController::create(const simulation::ControllerId controller,
                                                         const std::uint64_t seed,
                                                         const Personality personality) {
  require_weight(personality.approach_weight, "hill_seeker_controller.personality.approach_weight");
  require_weight(personality.jitter_weight, "hill_seeker_controller.personality.jitter_weight");
  return std::make_unique<HillSeekerController>(controller, seed, personality);
}

HillSeekerController::HillSeekerController(const simulation::ControllerId controller,
                                           const std::uint64_t seed,
                                           const Personality personality) noexcept
    : Controller(controller), random_(simulation::DeterministicRandom::create(seed)),
      personality_(personality) {}

std::vector<simulation::Command>
HillSeekerController::decide_from_observation(const Observation& observation) {
  if (!observation.has_live_entity()) {
    // Pending or knocked out. Ask for a body and draw nothing, so the generator's stream is a
    // function of the decisions this seeker actually took.
    hill_.reset();
    return request_body(observation);
  }

  const simulation::EntityId self = *observation.entity();
  const simulation::WorldSnapshot& snapshot = observation.snapshot();
  const simulation::PlayerSnapshot* const own = find_observed_player(snapshot, self);
  if (own == nullptr) {
    // A `Controllable` with no `PhysicsBody`: `respawn` leaves exactly this behind while a timer
    // runs, and the observation still resolves the entity. Nothing to steer until the seat.
    hill_.reset();
    return {};
  }

  const std::span<const simulation::ComponentStore<simulation::Hill>::Entry> hills =
      snapshot.components<simulation::Hill>();
  if (hills.empty()) {
    // No hill in this world. A seeker with nothing to seek decides nothing; the stored
    // acceleration is left exactly as it was.
    hill_.reset();
    return {};
  }
  const simulation::ComponentStore<simulation::Hill>::Entry& hill_entry = hills.front();
  hill_ = hill_entry.entity;
  const simulation::Hill& hill = hill_entry.value;

  const auto delta = controller_target_offset(own->position(), hill.center);
  const double delta_x = delta.x;
  const double delta_y = delta.y;
  // `sqrt(x * x + y * y)` written out rather than `std::hypot`, for the reason
  // `chaser_controller.cpp` gives: the steering system this feeds computes magnitudes the same way.
  const double magnitude = controller_magnitude(delta);
  // The divisor is the larger of the distance and the radius: a unit heading outside the hill and
  // a proportional pull inside it, reaching zero at the centre. A hill of no radius is a
  // configuration the mode refuses, and is guarded so a zero divisor cannot produce a NaN heading.
  const double divisor = std::max(magnitude, hill.radius);
  double approach_x = 0.0;
  double approach_y = 0.0;
  if (divisor > 0.0) {
    const double scale = personality_.approach_weight / divisor;
    approach_x = delta_x * scale;
    approach_y = delta_y * scale;
  }

  // Two draws in a written order, into named locals, so the heading does not depend on the
  // compiler's argument evaluation order (`wanderer_controller.cpp`).
  const double jitter_x = personality_.jitter_weight * ((random_.next_unit_interval() * 2.0) - 1.0);
  const double jitter_y = personality_.jitter_weight * ((random_.next_unit_interval() * 2.0) - 1.0);

  const simulation::Vector2 direction =
      simulation::Vector2::create(clamp_controller_direction_component(approach_x + jitter_x),
                                  clamp_controller_direction_component(approach_y + jitter_y));

  return request_thrust(observation, direction);
}

} // namespace blob_royale::controllers
