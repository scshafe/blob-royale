#ifndef BLOB_ROYALE_TESTING_TACTICAL_OBSERVATION_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_TACTICAL_OBSERVATION_FIXTURE_HPP

#include "components/controllable_component.hpp"
#include "components/hill_component.hpp"
#include "components/race_progress_component.hpp"
#include "components/stun_component.hpp"
#include "components/zone_component.hpp"
#include "fixed_delta.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "map_definition.hpp"
#include "observation.hpp"
#include "simulation_config.hpp"
#include "tick_window.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace blob_royale::testing::tactical_observation_fixture {

inline constexpr std::uint64_t kController = 7;
inline constexpr std::uint64_t kEntity = 7;
inline constexpr std::uint64_t kFirstObjective = 20;
inline constexpr std::uint64_t kSecondObjective = 21;
enum class Mode { kHill, kZone, kRace, kUnsupported };
struct Circle final {
  std::uint64_t entity;
  double x;
  double y;
  double radius;
};
struct Frame final {
  std::uint64_t tick{1};
  std::uint64_t running_tick{0};
  simulation::MatchPhase phase{simulation::MatchPhase::kRunning};
  Mode mode{Mode::kHill};
  std::uint64_t controller{kController};
  std::uint64_t entity{kEntity};
  double x{200.0};
  double y{320.0};
  bool owned{true};
  bool body{true};
  bool race_progress{true};
  std::uint64_t checkpoint{0};
  std::optional<std::uint64_t> generation{};
  bool stun{false};
  std::uint64_t stun_activation{1};
  std::uint64_t stun_duration{2};
  std::vector<Circle> circles{{kFirstObjective, 600.0, 320.0, 20.0}};
  std::optional<simulation::TerrainDefinition> terrain{};
  std::optional<simulation::RaceModeState> course{};
};

[[nodiscard]] inline simulation::RaceModeState race_course() {
  return {simulation::RaceRoadName::create("selected_lane"),
          20.0,
          {simulation::Vector2::create(300.0, 320.0), simulation::Vector2::create(600.0, 320.0)},
          96'000,
          8'000,
          {}};
}
[[nodiscard]] inline simulation::TerrainDefinition race_terrain() {
  return simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(960.0, 640.0), simulation::TerrainGround::kCorridors,
      {simulation::TerrainCorridor::create(
           "decoy", 20.0,
           {simulation::Vector2::create(100.0, 100.0), simulation::Vector2::create(800.0, 100.0)}),
       simulation::TerrainCorridor::create(
           "selected_lane", 80.0,
           {simulation::Vector2::create(100.0, 320.0), simulation::Vector2::create(800.0, 320.0)})},
      {});
}
[[nodiscard]] inline simulation::TerrainDefinition terrain_with_hole(const double x,
                                                                     const double radius) {
  return simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(960.0, 640.0), simulation::TerrainGround::kSolid, {},
      {simulation::TerrainHole::create("blocking_hole", simulation::Vector2::create(x, 320.0),
                                       radius)});
}

// Real immutable engine snapshots of explicitly authored public state, with no gameplay linkage.
[[nodiscard]] inline controllers::Observation observation(const Frame& frame) {
  const auto self = simulation::EntityId::create(frame.entity);
  const auto zero = simulation::Vector2::create(0.0, 0.0);
  std::vector<simulation::GameWorld::EntitySeed> seeds;
  if (frame.owned) {
    seeds.push_back(simulation::GameWorld::EntitySeed::create(
        self,
        simulation::PhysicsBody::create(simulation::Vector2::create(frame.x, frame.y), zero, zero),
        simulation::ControllerId::create(frame.controller)));
  }
  auto world = simulation::GameWorld::create(std::move(seeds));
  if (frame.owned && !frame.body) {
    world.mutable_store<simulation::PhysicsBody>().erase(self);
  }
  if (frame.owned && frame.generation) {
    world.mutable_store<simulation::Controllable>().insert_or_assign(
        self, simulation::Controllable{simulation::ControllerId::create(frame.controller),
                                       {},
                                       {},
                                       simulation::TickSequence::create(*frame.generation)});
  }
  if (frame.owned && frame.stun) {
    world.mutable_store<simulation::Stun>().insert_or_assign(
        self, simulation::Stun{simulation::TickWindow::create(
                  simulation::TickSequence::create(frame.stun_activation), frame.stun_duration)});
  }
  auto& match = world.mutable_match();
  match.phase = frame.phase;
  match.previous_phase = frame.phase;
  match.running_started_tick = simulation::TickSequence::create(frame.running_tick);
  switch (frame.mode) {
  case Mode::kHill:
    match.mode_state = simulation::KingOfTheHillModeState{};
    for (const auto& circle : frame.circles) {
      world.mutable_store<simulation::Hill>().insert_or_assign(
          simulation::EntityId::create(circle.entity),
          simulation::Hill{simulation::Vector2::create(circle.x, circle.y), circle.radius});
    }
    break;
  case Mode::kZone:
    match.mode_state = simulation::RoyalePlacementsModeState{};
    for (const auto& circle : frame.circles) {
      world.mutable_store<simulation::Zone>().insert_or_assign(
          simulation::EntityId::create(circle.entity),
          simulation::Zone{simulation::Vector2::create(circle.x, circle.y), circle.radius});
    }
    break;
  case Mode::kRace:
    match.mode_state = frame.course ? *frame.course : race_course();
    if (frame.owned && frame.race_progress) {
      world.mutable_store<simulation::RaceProgress>().insert_or_assign(
          self, simulation::RaceProgress{frame.checkpoint});
    }
    break;
  case Mode::kUnsupported:
    match.mode_state = simulation::NoModeState{};
    break;
  }
  auto terrain =
      frame.terrain ? *frame.terrain
      : frame.mode == Mode::kRace
          ? race_terrain()
          : simulation::TerrainDefinition::solid(simulation::ArenaBounds::create(960.0, 640.0));
  auto map = simulation::MapDefinition::create("tactical_fixture", std::move(terrain), {}, {},
                                               simulation::MapMetadata::none());
  auto game = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16), std::move(world),
      simulation::GameSimulationSetup::engine_defaults().with_map(std::move(map)));
  for (std::uint64_t tick = 0; tick < frame.tick; ++tick) {
    static_cast<void>(
        game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()));
  }
  return controllers::Observation::create(
      std::make_shared<const simulation::WorldSnapshot>(game.snapshot()),
      simulation::ControllerId::create(frame.controller));
}

} // namespace blob_royale::testing::tactical_observation_fixture

#endif
