#ifndef BLOB_ROYALE_TESTING_CONTROLLER_STEERING_FROZEN_REFERENCE_HPP
#define BLOB_ROYALE_TESTING_CONTROLLER_STEERING_FROZEN_REFERENCE_HPP

#include "../../simulation/fixtures/deterministic_random_frozen_reference.hpp"

#include "components/hill_component.hpp"
#include "components/race_progress_component.hpp"
#include "controller.hpp"
#include "controllers_validation_error.hpp"
#include "mode_states/race_mode_state.hpp"
#include "observation.hpp"
#include "physics_body.hpp"
#include "player_snapshot.hpp"
#include "terrain_queries.hpp"
#include "vector2.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace blob_royale::testing::controller_steering_reference {

// Frozen from 1f151f2's Chaser/HillSeeker/Racer decision paths. No production observation query,
// steering helper, RNG constant, or RNG finalizer supplies the oracle. The operation parameter
// permits a second test-only instantiation to substitute ONLY the proposed pure helpers; default
// instantiations remain the independent reference. Unchanged Controller owns identity/spawn/thrust
// authoring in both paths, exactly as the existing racer promotion proof does.
struct FrozenOperations final {
  struct Offset final {
    double x;
    double y;
  };

  template <typename Component>
  [[nodiscard]] static const Component* component(const simulation::WorldSnapshot& snapshot,
                                                  const simulation::EntityId entity) noexcept {
    for (const auto& entry : snapshot.components<Component>()) {
      if (entry.entity == entity) {
        return &entry.value;
      }
    }
    return nullptr;
  }

  [[nodiscard]] static const simulation::PlayerSnapshot*
  player(const simulation::WorldSnapshot& snapshot, const simulation::EntityId entity) noexcept {
    for (const simulation::PlayerSnapshot& player : snapshot.players()) {
      if (player.entity_id() == entity) {
        return &player;
      }
    }
    return nullptr;
  }

  [[nodiscard]] static Offset offset(const simulation::Vector2& origin,
                                     const simulation::Vector2& target) noexcept {
    const double delta_x = target.x() - origin.x();
    const double delta_y = target.y() - origin.y();
    return {delta_x, delta_y};
  }

  [[nodiscard]] static double squared(const Offset delta) noexcept {
    const double delta_x = delta.x;
    const double delta_y = delta.y;
    return (delta_x * delta_x) + (delta_y * delta_y);
  }

  [[nodiscard]] static double magnitude(const Offset delta) noexcept {
    const double delta_x = delta.x;
    const double delta_y = delta.y;
    return std::sqrt((delta_x * delta_x) + (delta_y * delta_y));
  }

  [[nodiscard]] static double chaser_clamp(const double component) noexcept {
    const double limit = 1.0;
    if (component > limit) {
      return limit;
    }
    if (component < -limit) {
      return -limit;
    }
    return component;
  }

  [[nodiscard]] static double seeker_clamp(const double component) noexcept {
    const double limit = 1.0;
    return std::clamp(component, -limit, limit);
  }
};

template <typename Operations = FrozenOperations>
class Chaser final : public controllers::Controller {
public:
  Chaser(const simulation::ControllerId controller, const double aggression)
      : Controller(controller), aggression_(aggression) {}

  [[nodiscard]] std::string_view kind() const noexcept override { return "chaser"; }
  [[nodiscard]] std::optional<simulation::EntityId> target() const noexcept { return target_; }

private:
  [[nodiscard]] std::vector<simulation::Command>
  decide_from_observation(const controllers::Observation& observation) override {
    if (!observation.has_live_entity()) {
      target_.reset();
      return request_body(observation);
    }
    const simulation::EntityId self = *observation.entity();
    const auto players = observation.snapshot().players();
    const simulation::PlayerSnapshot* const own = Operations::player(observation.snapshot(), self);
    if (own == nullptr) {
      target_.reset();
      return {};
    }
    const simulation::PlayerSnapshot* nearest = nullptr;
    double nearest_squared_distance = 0.0;
    for (const simulation::PlayerSnapshot& candidate : players) {
      if (candidate.entity_id() == self) {
        continue;
      }
      const auto delta = Operations::offset(own->position(), candidate.position());
      const double squared_distance = Operations::squared(delta);
      if (nearest == nullptr || squared_distance < nearest_squared_distance) {
        nearest = &candidate;
        nearest_squared_distance = squared_distance;
      }
    }
    if (nearest == nullptr) {
      target_.reset();
      return {};
    }
    target_ = nearest->entity_id();
    const auto delta = Operations::offset(own->position(), nearest->position());
    const double delta_x = delta.x;
    const double delta_y = delta.y;
    const double magnitude = Operations::magnitude(delta);
    if (!(magnitude > 0.0)) {
      return {};
    }
    const double scale = aggression_ / magnitude;
    const simulation::Vector2 direction = simulation::Vector2::create(
        Operations::chaser_clamp(delta_x * scale), Operations::chaser_clamp(delta_y * scale));
    return request_thrust(observation, direction);
  }

  double aggression_;
  std::optional<simulation::EntityId> target_;
};

template <typename Operations = FrozenOperations>
class HillSeeker final : public controllers::Controller {
public:
  HillSeeker(const simulation::ControllerId controller, const std::uint64_t seed,
             const double approach, const double jitter)
      : Controller(controller),
        random_(deterministic_random_reference::DeterministicRandom::create(seed)),
        approach_(approach), jitter_(jitter) {}

  [[nodiscard]] std::string_view kind() const noexcept override { return "hill_seeker"; }
  [[nodiscard]] std::optional<simulation::EntityId> hill() const noexcept { return hill_; }
  [[nodiscard]] std::uint64_t draw_count() const noexcept { return random_.draw_count(); }

private:
  [[nodiscard]] std::vector<simulation::Command>
  decide_from_observation(const controllers::Observation& observation) override {
    if (!observation.has_live_entity()) {
      hill_.reset();
      return request_body(observation);
    }
    const simulation::EntityId self = *observation.entity();
    const auto& snapshot = observation.snapshot();
    const simulation::PlayerSnapshot* const own = Operations::player(snapshot, self);
    if (own == nullptr) {
      hill_.reset();
      return {};
    }
    const auto hills = snapshot.components<simulation::Hill>();
    if (hills.empty()) {
      hill_.reset();
      return {};
    }
    const auto& hill_entry = hills.front();
    hill_ = hill_entry.entity;
    const simulation::Hill& hill = hill_entry.value;
    const auto delta = Operations::offset(own->position(), hill.center);
    const double delta_x = delta.x;
    const double delta_y = delta.y;
    const double magnitude = Operations::magnitude(delta);
    const double divisor = std::max(magnitude, hill.radius);
    double approach_x = 0.0;
    double approach_y = 0.0;
    if (divisor > 0.0) {
      const double scale = approach_ / divisor;
      approach_x = delta_x * scale;
      approach_y = delta_y * scale;
    }
    const double jitter_x = jitter_ * ((random_.next_unit_interval() * 2.0) - 1.0);
    const double jitter_y = jitter_ * ((random_.next_unit_interval() * 2.0) - 1.0);
    const simulation::Vector2 direction =
        simulation::Vector2::create(Operations::seeker_clamp(approach_x + jitter_x),
                                    Operations::seeker_clamp(approach_y + jitter_y));
    return request_thrust(observation, direction);
  }

  deterministic_random_reference::DeterministicRandom random_;
  double approach_;
  double jitter_;
  std::optional<simulation::EntityId> hill_;
};

// Complete accepted Racer path, frozen before observation/steering helper promotion. The already
// promoted terrain projection and unchanged Controller authoring are shared infrastructure, not
// either helper under test. The default operations independently retain both ordered component
// joins, raw offsets, and written sqrt; only the candidate substitutes the proposed helpers.
template <typename Operations = FrozenOperations>
class Racer final : public controllers::Controller {
public:
  Racer(const simulation::ControllerId controller, const double caution_fraction)
      : Controller(controller), caution_fraction_(caution_fraction) {}

  [[nodiscard]] std::string_view kind() const noexcept override { return "racer"; }

private:
  [[nodiscard]] std::vector<simulation::Command>
  decide_from_observation(const controllers::Observation& observation) override {
    if (!observation.entity().has_value()) {
      return request_body(observation);
    }
    const simulation::WorldSnapshot& snapshot = observation.snapshot();
    const auto* const course =
        std::get_if<simulation::RaceModeState>(&snapshot.match().mode_state());
    if (course == nullptr) {
      return {};
    }
    const simulation::EntityId self = *observation.entity();
    const auto* body = Operations::template component<simulation::PhysicsBody>(snapshot, self);
    const auto* progress = Operations::template component<simulation::RaceProgress>(snapshot, self);
    if (body == nullptr || progress == nullptr) {
      return {};
    }
    const auto* road = observation.terrain().find_corridor(course->road.value());
    if (road == nullptr) {
      throw controllers::ControllersValidationError(
          controllers::ControllersValidationCode::kRacerCourseInvalid,
          "racer_controller.observation.road",
          "published race road is absent from the observation terrain");
    }
    if (course->checkpoints.empty() || progress->next_checkpoint > course->checkpoints.size()) {
      throw controllers::ControllersValidationError(
          controllers::ControllersValidationCode::kRacerCourseInvalid,
          "racer_controller.observation.course",
          "published race geometry or checkpoint progress is invalid");
    }
    simulation::Vector2 direction = simulation::Vector2::create(0.0, 0.0);
    if (progress->next_checkpoint < course->checkpoints.size()) {
      const auto nearest = simulation::corridor_project_to_centreline(*road, body->position());
      const simulation::Vector2& target =
          nearest.distance > caution_fraction_ * road->half_width()
              ? nearest.point
              : course->checkpoints[static_cast<std::size_t>(progress->next_checkpoint)];
      const auto delta = Operations::offset(body->position(), target);
      const double dx = delta.x;
      const double dy = delta.y;
      const double magnitude = Operations::magnitude(delta);
      if (magnitude > 0.0) {
        direction = simulation::Vector2::create(dx / magnitude, dy / magnitude);
      }
    }
    return request_thrust(observation, direction);
  }

  double caution_fraction_;
};

} // namespace blob_royale::testing::controller_steering_reference

#endif
