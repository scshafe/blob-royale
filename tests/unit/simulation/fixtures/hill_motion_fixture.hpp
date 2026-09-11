#ifndef BLOB_ROYALE_TESTING_HILL_MOTION_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_HILL_MOTION_FIXTURE_HPP

#include "components/hill_motion_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "simulation_config.hpp"

namespace blob_royale::testing::hill_motion_fixture {

[[nodiscard]] inline simulation::SimulationConfig configuration() {
  return simulation::SimulationConfig::create(
      100.0, 100.0, 1.0, simulation::SimulationConfig::kRequiredTicksPerSecond, 10, 10);
}

[[nodiscard]] inline simulation::EntityId entity() { return simulation::EntityId::create(90); }
[[nodiscard]] inline simulation::HillMotion motion() {
  return {simulation::Vector2::create(20.0, -30.0),
          simulation::HillMotionSchedule{simulation::TickSequence::create(140),
                                         simulation::RandomStreamKind::kHill}};
}
[[nodiscard]] inline simulation::GameWorld world() {
  simulation::GameWorld result = simulation::GameWorld::create({});
  result.mutable_store<simulation::HillMotion>().insert_or_assign(entity(), motion());
  return result;
}

} // namespace blob_royale::testing::hill_motion_fixture

#endif
