#include "king_of_the_hill/hill_movement_system.hpp"

#include "gameplay_test_fixture.hpp"

#include "components/hill_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "map_definition.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "simulation_system.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] simulation::Vector2 point(const double x, const double y) {
  return simulation::Vector2::create(x, y);
}

// Four spawn points and two hill markers, 200 wu apart along x.
[[nodiscard]] simulation::MapDefinition hill_map() {
  std::vector<simulation::MapDefinition::Marker> markers;
  for (std::size_t index = 0; index < 4; ++index) {
    markers.push_back(simulation::MapDefinition::Marker::spawn(
        point(100.0 * static_cast<double>(index + 1), 320.0)));
  }
  markers.push_back(simulation::MapDefinition::Marker::create(
      "hill", point(200.0, 200.0), std::nullopt, simulation::MapMetadata::none()));
  markers.push_back(simulation::MapDefinition::Marker::create(
      "hill", point(400.0, 200.0), std::nullopt, simulation::MapMetadata::none()));
  return simulation::MapDefinition::create("hill_map",
                                           simulation::ArenaBounds::create(960.0, 640.0), {},
                                           std::move(markers), simulation::MapMetadata::none());
}

// D = 100, T = 40 at the fixture's 400 ticks per second.
[[nodiscard]] gameplay::KingOfTheHillConfiguration configuration() {
  gameplay::KingOfTheHillConfiguration::Section section =
      gameplay::KingOfTheHillConfiguration::default_section();
  section.hill_dwell_seconds = 0.25;
  section.hill_travel_seconds = 0.1;
  section.hill_radius_world_units = 75.0;
  return gameplay::KingOfTheHillConfiguration::create(section);
}

// A world already carrying a hill entity, so the system's per-tick write can be observed outside a
// tick: a hand-built world holds no reservation, and creation is proven through the mode in
// `king_of_the_hill_mode_tests.cpp`.
[[nodiscard]] simulation::GameWorld world_with_hill(const simulation::MatchPhase phase,
                                                    const std::uint64_t running_started_tick,
                                                    const std::uint64_t phase_started_tick) {
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_store<simulation::Hill>().insert_or_assign(simulation::EntityId::create(90),
                                                           simulation::Hill{point(0.0, 0.0), 1.0});
  world.mutable_match().phase = phase;
  world.mutable_match().running_started_tick =
      simulation::TickSequence::create(running_started_tick);
  world.mutable_match().phase_started_tick = simulation::TickSequence::create(phase_started_tick);
  return world;
}

[[nodiscard]] simulation::Hill hill_after(simulation::GameWorld& world,
                                          const std::uint64_t tick_sequence) {
  const std::unique_ptr<const simulation::SimulationSystem> movement =
      gameplay::HillMovementSystem::create(configuration());
  const testing::TickHarness harness{simulation::TickSequence::create(tick_sequence), hill_map()};
  movement->apply(world, harness.context());
  const simulation::Hill* hill =
      world.store<simulation::Hill>().find(simulation::EntityId::create(90));
  REQUIRE(hill != nullptr);
  return *hill;
}

} // namespace

TEST_CASE("the hill sits at the first marker before a match and carries the configured radius",
          "[unit][gameplay][king_of_the_hill][hill]") {
  for (const simulation::MatchPhase phase :
       {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown}) {
    INFO("phase " << simulation::match_phase_name(phase));
    simulation::GameWorld world = world_with_hill(phase, 0, 0);
    const simulation::Hill hill = hill_after(world, 777);
    CHECK(hill.center == point(200.0, 200.0));
    CHECK(hill.radius == 75.0);
  }
}

TEST_CASE("while running the hill tours by elapsed running ticks, and ended freezes it",
          "[unit][gameplay][king_of_the_hill][hill]") {
  // Running began on tick 1,000; on tick 1,120 the hill is 20 ticks into its first glide.
  simulation::GameWorld running = world_with_hill(simulation::MatchPhase::kRunning, 1'000, 1'000);
  CHECK(hill_after(running, 1'000).center == point(200.0, 200.0));
  CHECK(hill_after(running, 1'099).center == point(200.0, 200.0));
  CHECK(hill_after(running, 1'120).center == point(300.0, 200.0));
  CHECK(hill_after(running, 1'140).center == point(400.0, 200.0));

  // The match ended on tick 1,120: every later tick evaluates the tour at that tick.
  simulation::GameWorld ended = world_with_hill(simulation::MatchPhase::kEnded, 1'000, 1'120);
  CHECK(hill_after(ended, 1'121).center == point(300.0, 200.0));
  CHECK(hill_after(ended, 9'999).center == point(300.0, 200.0));
}

TEST_CASE("the same entity is rewritten every tick rather than a second hill created",
          "[unit][gameplay][king_of_the_hill][hill]") {
  simulation::GameWorld world = world_with_hill(simulation::MatchPhase::kRunning, 1'000, 1'000);
  static_cast<void>(hill_after(world, 1'000));
  static_cast<void>(hill_after(world, 1'120));
  CHECK(world.store<simulation::Hill>().entries().size() == 1);
  CHECK(world.entities().size() == 1);
}

TEST_CASE("a tick that must create the hill and may create nothing fails naming the hill",
          "[unit][gameplay][king_of_the_hill][hill][validation]") {
  // Outside a tick the world holds the empty reservation, which is exactly the no-input tick's.
  simulation::GameWorld world = simulation::GameWorld::create({});
  const std::unique_ptr<const simulation::SimulationSystem> movement =
      gameplay::HillMovementSystem::create(configuration());
  const testing::TickHarness harness{simulation::TickSequence::create(1), hill_map()};
  try {
    movement->apply(world, harness.context());
    FAIL("a tick with no reservation created the hill anyway");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() ==
          gameplay::GameplayValidationCode::kKingOfTheHillHillEntityUnreserved);
    CHECK(error.code() == std::string_view{"GAMEPLAY.KING_OF_THE_HILL_HILL_ENTITY_UNRESERVED"});
    CHECK(error.context() == "hill_movement.hill_entity");
  }
  CHECK(world.entities().empty());
}

TEST_CASE("a map with no hill marker is a rejection naming the map",
          "[unit][gameplay][king_of_the_hill][hill][validation]") {
  simulation::GameWorld world = world_with_hill(simulation::MatchPhase::kLobby, 0, 0);
  const std::unique_ptr<const simulation::SimulationSystem> movement =
      gameplay::HillMovementSystem::create(configuration());
  const testing::TickHarness harness{simulation::TickSequence::create(1),
                                     testing::gameplay_map(4, "no_hill_map")};
  try {
    movement->apply(world, harness.context());
    FAIL("a map with no hill marker was played");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() ==
          gameplay::GameplayValidationCode::kKingOfTheHillMapWithoutHill);
    CHECK(error.context() == "hill_movement.markers");
  }
}
