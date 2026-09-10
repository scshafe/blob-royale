#ifndef BLOB_ROYALE_TESTS_UNIT_GAMEPLAY_RACE_RACE_TEST_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_GAMEPLAY_RACE_RACE_TEST_FIXTURE_HPP

#include "gameplay_test_fixture.hpp"
#include "race/race_configuration.hpp"
#include "race/race_course.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::testing {

// canonical: race_test_fixture -- named course, balance, and initial field for race unit tests.
// A straight centreline from (100,320) to (800,320), four grid positions at x=100,200,300,400,
// and by default gates at x=300,600. Gate positions are arguments so ordering/overlap tests vary
// only the geometry whose behavior they assert.
[[nodiscard]] inline simulation::MapDefinition race_test_map(
    const std::string& name = "race_test_map",
    const std::vector<simulation::Vector2>& checkpoints = {
        simulation::Vector2::create(300.0, 320.0), simulation::Vector2::create(600.0, 320.0)}) {
  std::vector<simulation::MapDefinition::Marker> markers;
  for (const simulation::Vector2& position :
       {simulation::Vector2::create(100.0, 320.0), simulation::Vector2::create(800.0, 320.0)}) {
    markers.push_back(simulation::MapDefinition::Marker::create("track", position, std::nullopt,
                                                                simulation::MapMetadata::none()));
  }
  for (const simulation::Vector2& position : checkpoints) {
    markers.push_back(simulation::MapDefinition::Marker::create(
        "checkpoint", position, std::nullopt, simulation::MapMetadata::none()));
  }
  for (std::size_t index = 0; index < 4; ++index) {
    markers.push_back(simulation::MapDefinition::Marker::spawn(
        simulation::Vector2::create(100.0 * static_cast<double>(index + 1), 320.0)));
  }
  return simulation::MapDefinition::create(name, simulation::ArenaBounds::create(960.0, 640.0), {},
                                           std::move(markers), simulation::MapMetadata::none());
}

// Proposed race balance with a zero countdown, so a started lobby commits running on tick 2.
[[nodiscard]] inline gameplay::RaceConfiguration race_test_configuration() {
  gameplay::RaceConfiguration::Section section = gameplay::RaceConfiguration::default_section();
  section.countdown_seconds = 0.0;
  return gameplay::RaceConfiguration::create(section);
}

// Alive entities/controllers numbered from 1, at rest at each position. Previous phase equals
// current phase; tests that exercise the first running tick set previous_phase to countdown.
[[nodiscard]] inline simulation::GameWorld
race_test_world(const std::vector<simulation::Vector2>& positions,
                const simulation::MatchPhase phase = simulation::MatchPhase::kRunning) {
  std::vector<simulation::GameWorld::EntitySeed> seeds;
  for (std::size_t index = 0; index < positions.size(); ++index) {
    seeds.push_back(simulation::GameWorld::EntitySeed::create(
        simulation::EntityId::create(index + 1),
        simulation::PhysicsBody::create(positions[index], simulation::Vector2::create(0.0, 0.0),
                                        simulation::Vector2::create(0.0, 0.0)),
        simulation::ControllerId::create(index + 1)));
  }
  simulation::GameWorld world = simulation::GameWorld::create(std::move(seeds));
  world.mutable_match().phase = phase;
  world.mutable_match().previous_phase = phase;
  return world;
}

} // namespace blob_royale::testing

#endif
