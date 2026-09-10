#ifndef BLOB_ROYALE_TESTS_UNIT_CONTROLLERS_FIXTURES_RACER_OBSERVATION_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_CONTROLLERS_FIXTURES_RACER_OBSERVATION_FIXTURE_HPP

#include "../../gameplay/race/race_test_fixture.hpp"
#include "components/race_progress_component.hpp"
#include "mode_states/race_mode_state.hpp"
#include "observation.hpp"

#include <cstdint>
#include <memory>
#include <utility>

namespace blob_royale::testing {

// canonical: racer_observation_fixture -- literal snapshots for the ADR 0007 bot policy.
// The straight road is y=320, x=100..800, half-width 80: the default 0.75 caution
// boundary is exactly 60. Gates at x=300 and x=600 distinguish progress from proximity.
// These fixtures test snapshot -> command, not the production course publisher or physics.
[[nodiscard]] inline simulation::RaceModeState straight_racer_course() {
  simulation::RaceModeState state;
  state.track_half_width = 80.0;
  state.checkpoint_radius = 20.0;
  state.track = {simulation::Vector2::create(100.0, 320.0),
                 simulation::Vector2::create(800.0, 320.0)};
  state.checkpoints = {simulation::Vector2::create(300.0, 320.0),
                       simulation::Vector2::create(600.0, 320.0)};
  state.time_limit_ticks = 96'000;
  state.finish_window_ticks = 8'000;
  return state;
}

// A right-angle bend. (550,440) is nearer the vertical leg; (450,370) is equally
// distant from both legs, so a 0.5 caution fraction exposes the declared-order tie.
[[nodiscard]] inline simulation::RaceModeState bent_racer_course() {
  simulation::RaceModeState state = straight_racer_course();
  state.track = {simulation::Vector2::create(100.0, 320.0),
                 simulation::Vector2::create(500.0, 320.0),
                 simulation::Vector2::create(500.0, 560.0)};
  state.checkpoints = {simulation::Vector2::create(300.0, 320.0),
                       simulation::Vector2::create(500.0, 520.0)};
  return state;
}

// One durable controller/entity, at rest, with caller-selected progress. Reuses the
// gameplay world's seeding helper; no second entity or physics construction policy.
[[nodiscard]] inline simulation::GameWorld
racer_observation_world(const simulation::Vector2 position, const std::uint64_t next_checkpoint = 0,
                        simulation::RaceModeState course = straight_racer_course()) {
  simulation::GameWorld world = race_test_world({position});
  world.mutable_match().mode_state = std::move(course);
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(
      simulation::EntityId::create(1), simulation::RaceProgress{next_checkpoint});
  return world;
}

// GameSimulation owns the only snapshot constructor. Its initial snapshot copies
// this authored state without advancing a tick or replacing it through a mode system.
[[nodiscard]] inline controllers::Observation
racer_observation(simulation::GameWorld world, const std::uint64_t controller = 1) {
  const simulation::GameSimulation game =
      simulation::GameSimulation::create(gameplay_configuration(), std::move(world));
  return controllers::Observation::create(
      std::make_shared<const simulation::WorldSnapshot>(game.snapshot()),
      simulation::ControllerId::create(controller));
}

} // namespace blob_royale::testing

#endif
