#ifndef BLOB_ROYALE_TESTS_UNIT_CONTROLLERS_FIXTURES_RACER_OBSERVATION_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_CONTROLLERS_FIXTURES_RACER_OBSERVATION_FIXTURE_HPP

#include "../../gameplay/race/race_test_fixture.hpp"
#include "components/race_progress_component.hpp"
#include "game_simulation_setup.hpp"
#include "map_definition.hpp"
#include "mode_states/race_mode_state.hpp"
#include "observation.hpp"

#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace blob_royale::testing {

// canonical: racer_observation_fixture -- literal snapshots for the ADR 0007 bot policy.
// The straight road is y=320, x=100..800, half-width 80: the default 0.75 caution
// boundary is exactly 60. Gates at x=300 and x=600 distinguish progress from proximity.
// These fixtures test snapshot -> command, not the production course publisher or physics.
[[nodiscard]] inline simulation::RaceModeState straight_racer_course() {
  simulation::RaceModeState state{.road = simulation::RaceRoadName::create("road")};
  state.checkpoint_radius = 20.0;
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
  state.checkpoints = {simulation::Vector2::create(300.0, 320.0),
                       simulation::Vector2::create(500.0, 520.0)};
  return state;
}

[[nodiscard]] inline simulation::TerrainDefinition
racer_observation_terrain(std::vector<simulation::TerrainCorridor> corridors) {
  const simulation::SimulationConfig configuration = gameplay_configuration();
  return simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(configuration.world_width(), configuration.world_height()),
      simulation::TerrainGround::kCorridors, std::move(corridors), {});
}

[[nodiscard]] inline simulation::TerrainDefinition straight_racer_terrain() {
  return racer_observation_terrain({simulation::TerrainCorridor::create(
      "road", 80.0,
      {simulation::Vector2::create(100.0, 320.0), simulation::Vector2::create(800.0, 320.0)})});
}

[[nodiscard]] inline simulation::TerrainDefinition bent_racer_terrain() {
  return racer_observation_terrain({simulation::TerrainCorridor::create(
      "road", 80.0,
      {simulation::Vector2::create(100.0, 320.0), simulation::Vector2::create(500.0, 320.0),
       simulation::Vector2::create(500.0, 560.0)})});
}

[[nodiscard]] inline simulation::RaceModeState alternate_racer_course() {
  auto state = straight_racer_course();
  state.road = simulation::RaceRoadName::create("outer_lane");
  return state;
}

// The corridor literally named "road" is a decoy with different geometry and width. Selecting
// it by convention or taking the first corridor would produce downward recovery, not gate aim.
[[nodiscard]] inline simulation::TerrainDefinition
alternate_racer_terrain(const bool selected_first) {
  const auto selected = simulation::TerrainCorridor::create(
      "outer_lane", 80.0,
      {simulation::Vector2::create(100.0, 320.0), simulation::Vector2::create(800.0, 320.0)});
  const auto decoy = simulation::TerrainCorridor::create(
      "road", 20.0,
      {simulation::Vector2::create(100.0, 100.0), simulation::Vector2::create(800.0, 100.0)});
  return racer_observation_terrain(selected_first ? std::vector{selected, decoy}
                                                  : std::vector{decoy, selected});
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
// this authored state without advancing a tick or replacing it through a mode system. The map
// publishes the explicitly supplied terrain, never inferred from mode fields or repaired when
// the selected road binding is absent. Invalid-binding tests need that mismatch to survive.
[[nodiscard]] inline controllers::Observation
racer_observation(simulation::GameWorld world, simulation::TerrainDefinition terrain,
                  const std::uint64_t controller = 1) {
  const simulation::SimulationConfig configuration = gameplay_configuration();
  simulation::MapDefinition map = simulation::MapDefinition::create(
      "racer_observation", std::move(terrain), {}, {}, simulation::MapMetadata::none());
  const simulation::GameSimulation game = simulation::GameSimulation::create(
      configuration, std::move(world),
      simulation::GameSimulationSetup::engine_defaults().with_map(std::move(map)));
  return controllers::Observation::create(
      std::make_shared<const simulation::WorldSnapshot>(game.snapshot()),
      simulation::ControllerId::create(controller));
}

// Named straight-course authoring convenience, not a missing-terrain fallback in publication.
[[nodiscard]] inline controllers::Observation
straight_racer_observation(simulation::GameWorld world, const std::uint64_t controller = 1) {
  return racer_observation(std::move(world), straight_racer_terrain(), controller);
}

} // namespace blob_royale::testing

#endif
