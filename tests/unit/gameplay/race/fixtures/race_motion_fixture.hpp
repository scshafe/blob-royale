#ifndef BLOB_ROYALE_TESTING_RACE_MOTION_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_RACE_MOTION_FIXTURE_HPP

#include "race/race_mode.hpp"
#include "race/race_test_fixture.hpp"

#include "components/race_progress_component.hpp"
#include "mode_states/race_mode_state.hpp"

#include <utility>

namespace blob_royale::testing::race_motion_fixture {

inline constexpr double kFastSpeed = 200'000.0;
inline constexpr double kFinishX = 300.0;
inline constexpr double kCenterY = 320.0;
inline constexpr std::uint64_t kFirstEntity = 1;
inline constexpr std::uint64_t kSecondEntity = 2;
inline constexpr std::uint64_t kHazardEntity = 20;

[[nodiscard]] inline simulation::EntityId entity(const std::uint64_t id = kFirstEntity) {
  return simulation::EntityId::create(id);
}
[[nodiscard]] inline simulation::Vector2 point(const double x, const double y = kCenterY) {
  return simulation::Vector2::create(x, y);
}
[[nodiscard]] inline simulation::Vector2 zero() { return point(0.0, 0.0); }
[[nodiscard]] inline simulation::MapDefinition one_gate_map() {
  return race_test_map("race_motion_finish", {point(kFinishX)});
}
[[nodiscard]] inline simulation::GameWorld
moving_world(const std::vector<simulation::Vector2>& positions,
             const simulation::Vector2 velocity = point(kFastSpeed, 0.0)) {
  auto world = race_test_world(positions);
  for (auto& body : world.mutable_store<simulation::PhysicsBody>().mutable_values()) {
    body = body.with_velocity(velocity).with_radius(gameplay_configuration().player_radius());
  }
  return world;
}
[[nodiscard]] inline simulation::GameSimulation game(simulation::GameWorld world,
                                                     simulation::MapDefinition map = one_gate_map(),
                                                     const simulation::MotionLimits limits = {}) {
  return simulation::GameSimulation::create(
      gameplay_configuration(), std::move(world),
      simulation::GameSimulationSetup::of_mode(
          std::move(map), gameplay::RaceMode::create(race_test_configuration()))
          .with_motion_limits(limits));
}
[[nodiscard]] inline const simulation::RaceModeState&
race(const simulation::WorldSnapshot& snapshot) {
  return std::get<simulation::RaceModeState>(snapshot.match().mode_state());
}

} // namespace blob_royale::testing::race_motion_fixture

#endif
