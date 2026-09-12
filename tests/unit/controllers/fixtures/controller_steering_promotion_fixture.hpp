#ifndef BLOB_ROYALE_TESTING_CONTROLLER_STEERING_PROMOTION_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_CONTROLLER_STEERING_PROMOTION_FIXTURE_HPP

#include "../../gameplay/gameplay_test_fixture.hpp"
#include "../controllers_test_fixture.hpp"
#include "commands/despawn_command.hpp"
#include "components/controllable_component.hpp"
#include "components/hill_component.hpp"
#include "components/stun_component.hpp"
#include "controller_observation_queries.hpp"
#include "controller_steering.hpp"
#include "fixed_delta.hpp"
#include "game_simulation_setup.hpp"
#include "input_batch.hpp"
#include "observation.hpp"
#include "tick_window.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace blob_royale::testing::controller_steering_promotion {

struct PromotedOperations final {
  template <typename Component>
  [[nodiscard]] static const Component* component(const simulation::WorldSnapshot& snapshot,
                                                  const simulation::EntityId entity) noexcept {
    return controllers::find_observed_component<Component>(snapshot, entity);
  }
  [[nodiscard]] static const simulation::PlayerSnapshot*
  player(const simulation::WorldSnapshot& snapshot, const simulation::EntityId entity) noexcept {
    return controllers::find_observed_player(snapshot, entity);
  }
  [[nodiscard]] static controllers::ControllerTargetOffset
  offset(const simulation::Vector2& origin, const simulation::Vector2& target) noexcept {
    return controllers::controller_target_offset(origin, target);
  }
  [[nodiscard]] static double squared(const controllers::ControllerTargetOffset delta) noexcept {
    return controllers::controller_squared_magnitude(delta);
  }
  [[nodiscard]] static double magnitude(const controllers::ControllerTargetOffset delta) noexcept {
    return controllers::controller_magnitude(delta);
  }
  [[nodiscard]] static double chaser_clamp(const double component) noexcept {
    return controllers::clamp_controller_direction_component(component);
  }
  [[nodiscard]] static double seeker_clamp(const double component) noexcept {
    return controllers::clamp_controller_direction_component(component);
  }
};

inline constexpr std::uint64_t kController = 7;
inline constexpr std::uint64_t kSelf = 7;
inline constexpr std::uint64_t kBodyOnly = 3;
inline constexpr std::uint64_t kBodyless = 19;
inline constexpr std::uint64_t kAbsent = 31;
inline constexpr std::size_t kRepeatedPassCount = 24;
inline constexpr std::uint64_t kFrozenSpawnRetryTick = 40;
inline constexpr std::array<std::size_t, 10> kLifecycleCommandCounts{1, 0, 0, 1, 0, 0, 0, 1, 0, 0};
inline constexpr std::array<std::uint64_t, 5> kSeeds{0, 1, 7, 20260907, 20260908};
inline constexpr std::array<double, 4> kAggressionWeights{0.0, 0.25, 0.75, 1.0};

struct SeekerWeights final {
  double approach;
  double jitter;
};
inline constexpr std::array<SeekerWeights, 6> kSeekerWeights{
    SeekerWeights{1.0, 0.15}, {1.0, 0.0}, {0.0, 0.0}, {0.0, 1.0}, {0.25, 0.15}, {1.0, 1.0}};

struct GeometryCase final {
  std::string_view name;
  double origin_x;
  double origin_y;
  double target_x;
  double target_y;
};
inline constexpr std::array<GeometryCase, 7> kGeometryCases{
    GeometryCase{"axis", 100.0, 320.0, 500.0, 320.0},
    {"three-four", 100.0, 100.0, 103.0, 104.0},
    {"negative", 10.0, 20.0, -13.0, -37.0},
    {"fractional", 192.125, 352.75, 513.875, 497.375},
    {"coincident", 100.0, 100.0, 100.0, 100.0},
    {"squared-underflow", 0.0, 0.0, 1e-170, -1e-170},
    {"raw-offset-beyond-vector-bound", -1e12, 1e12, 1e12, -1e12}};

[[nodiscard]] inline auto clamp_inputs() {
  return std::array{0.0,
                    -0.0,
                    0.25,
                    -0.25,
                    1.0,
                    -1.0,
                    std::nextafter(1.0, 0.0),
                    std::nextafter(-1.0, 0.0),
                    std::nextafter(1.0, 2.0),
                    std::nextafter(-1.0, -2.0),
                    2.0,
                    -2.0,
                    std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::quiet_NaN()};
}

struct Actor final {
  std::uint64_t entity;
  std::optional<std::uint64_t> controller;
  double x;
  double y;
};
struct HillInput final {
  std::uint64_t entity;
  double x;
  double y;
  double radius;
};
struct Frame final {
  std::string_view name;
  std::vector<Actor> actors;
  std::vector<HillInput> hills;
  bool bodyless_self{false};
  std::optional<std::uint64_t> generation{};
  bool stun{false};
  std::uint64_t ticks{0};
};

[[nodiscard]] inline Frame baseline_frame() {
  // Deliberately unsorted authoring; the snapshot orders the join. Body-only entity3 is nearer
  // than either eligible player, and bodyless entity19 has no player/body projection.
  return {"nearest-tie-and-join",
          {{11, 11, 500.0, 320.0},
           {7, 7, 300.0, 320.0},
           {3, std::nullopt, 301.0, 320.0},
           {2, 2, 100.0, 320.0}},
          {{60, 700.0, 320.0, 100.0}}};
}

[[nodiscard]] inline std::vector<Frame> frames() {
  std::vector<Frame> result{baseline_frame()};
  auto changed = baseline_frame();
  changed.name = "closer-higher-entity";
  changed.actors[0].x = 450.0;
  result.push_back(changed);
  changed = baseline_frame();
  changed.name = "coincident-target";
  changed.actors.back().x = 300.0;
  result.push_back(changed);
  changed = baseline_frame();
  changed.name = "fractional-direction";
  changed.actors.back().x = 192.125;
  changed.actors.back().y = 352.75;
  changed.hills = {{60, 513.875, 497.375, 113.25}};
  result.push_back(changed);
  changed = baseline_frame();
  changed.name = "inside-hill";
  changed.hills = {{60, 350.0, 345.0, 100.0}};
  result.push_back(changed);
  changed.name = "hill-centre";
  changed.hills = {{60, 300.0, 320.0, 100.0}};
  result.push_back(changed);
  changed.name = "exact-hill-radius";
  changed.hills = {{60, 400.0, 320.0, 100.0}};
  result.push_back(changed);
  changed = baseline_frame();
  changed.name = "first-hill-not-nearest";
  changed.hills = {{60, 310.0, 320.0, 100.0}, {50, 750.0, 510.0, 100.0}};
  result.push_back(changed);
  changed = baseline_frame();
  changed.name = "no-hill";
  changed.hills.clear();
  result.push_back(changed);
  changed.name = "alone-with-body-only";
  changed.actors = {{7, 7, 300.0, 320.0}, {3, std::nullopt, 301.0, 320.0}};
  result.push_back(changed);
  changed = baseline_frame();
  changed.name = "bodyless-self";
  changed.actors.erase(changed.actors.begin() + 1);
  changed.bodyless_self = true;
  result.push_back(changed);
  changed.name = "absent-self";
  changed.bodyless_self = false;
  result.push_back(changed);
  changed = baseline_frame();
  changed.name = "replacement-entity";
  changed.actors[1].entity = 17;
  result.push_back(changed);
  changed = baseline_frame();
  changed.name = "observed-active-stun";
  changed.actors.erase(changed.actors.begin() + 2);
  changed.generation = 1;
  changed.stun = true;
  changed.ticks = 1;
  result.push_back(changed);
  changed.name = "exact-stun-expiry";
  changed.ticks = 3;
  result.push_back(changed);
  return result;
}

[[nodiscard]] inline controllers::Observation observation(const Frame& frame) {
  std::vector<simulation::GameWorld::EntitySeed> actors;
  for (const auto& actor : frame.actors) {
    const auto body = simulation::PhysicsBody::create(simulation::Vector2::create(actor.x, actor.y),
                                                      simulation::Vector2::create(0.0, 0.0),
                                                      simulation::Vector2::create(0.0, 0.0));
    actors.push_back({simulation::EntityId::create(actor.entity), body,
                      actor.controller
                          ? std::optional{simulation::ControllerId::create(*actor.controller)}
                          : std::nullopt});
  }
  auto world = simulation::GameWorld::create(std::move(actors));
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      simulation::EntityId::create(kBodyless),
      simulation::Controllable{simulation::ControllerId::create(kBodyless)});
  if (frame.bodyless_self) {
    world.mutable_store<simulation::Controllable>().insert_or_assign(
        simulation::EntityId::create(kSelf),
        simulation::Controllable{simulation::ControllerId::create(kController)});
  }
  if (frame.generation) {
    world.mutable_store<simulation::Controllable>().insert_or_assign(
        simulation::EntityId::create(kSelf),
        simulation::Controllable{simulation::ControllerId::create(kController),
                                 {},
                                 {},
                                 simulation::TickSequence::create(*frame.generation)});
  }
  if (frame.stun) {
    world.mutable_store<simulation::Stun>().insert_or_assign(
        simulation::EntityId::create(kSelf),
        simulation::Stun{simulation::TickWindow::create(simulation::TickSequence::create(1), 2)});
  }
  for (const auto& hill : frame.hills) {
    world.mutable_store<simulation::Hill>().insert_or_assign(
        simulation::EntityId::create(hill.entity),
        simulation::Hill{simulation::Vector2::create(hill.x, hill.y), hill.radius});
  }
  auto game = simulation::GameSimulation::create(
      gameplay_configuration(), std::move(world),
      simulation::GameSimulationSetup::engine_defaults().with_map(gameplay_map(4)));
  for (std::uint64_t tick = 0; tick < frame.ticks; ++tick) {
    static_cast<void>(
        game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()));
  }
  return controllers::Observation::create(
      std::make_shared<const simulation::WorldSnapshot>(game.snapshot()),
      simulation::ControllerId::create(kController));
}

// Real engine publications, with independently authored lifecycle commands rather than commands
// produced by the bots under comparison. Repeated snapshots and 39/40 elapsed ticks pin the old
// canonical spawn retry; body ownership changes are real spawn/despawn operations.
[[nodiscard]] inline std::vector<controllers::Observation> lifecycle_observations() {
  ControllersFixture fixture(controllers_map_of(4), 0);
  std::vector<controllers::Observation> observations;
  const auto record = [&] {
    observations.push_back(fixture.observation_at_committed(kController));
  };
  record();
  record();
  for (std::uint64_t tick = 1; tick < kFrozenSpawnRetryTick; ++tick) {
    static_cast<void>(fixture.commit());
  }
  record();
  static_cast<void>(fixture.commit());
  record();
  record();
  static_cast<void>(fixture.commit({spawn_command(kController)}));
  record();
  record();
  const auto entity = *observations.back().entity();
  static_cast<void>(fixture.commit({simulation::Command{simulation::DespawnCommand{entity}}}));
  record();
  record();
  static_cast<void>(fixture.commit({spawn_command(kController)}));
  record();
  return observations;
}

} // namespace blob_royale::testing::controller_steering_promotion

#endif
