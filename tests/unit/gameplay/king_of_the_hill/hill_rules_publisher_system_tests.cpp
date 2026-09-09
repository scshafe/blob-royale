#include "king_of_the_hill/hill_rules_publisher_system.hpp"

#include "gameplay_test_fixture.hpp"

#include "game_world.hpp"
#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "match_state.hpp"
#include "mode_match_state_registry.hpp"
#include "mode_states/king_of_the_hill_mode_state.hpp"
#include "mode_states/royale_placements_mode_state.hpp"
#include "tick_sequence.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <variant>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] simulation::KingOfTheHillModeState block_of(const simulation::GameWorld& world) {
  const auto* held = std::get_if<simulation::KingOfTheHillModeState>(&world.match().mode_state);
  REQUIRE(held != nullptr);
  return *held;
}

void publish(simulation::GameWorld& world,
             const gameplay::KingOfTheHillConfiguration& configuration) {
  const testing::TickHarness harness{simulation::TickSequence::create(17)};
  gameplay::HillRulesPublisherSystem::create(configuration)->apply(world, harness.context());
}

} // namespace

TEST_CASE("the publisher stamps the three configured denominators into the hill's block",
          "[unit][gameplay][king_of_the_hill][mode_state]") {
  gameplay::KingOfTheHillConfiguration::Section section =
      gameplay::KingOfTheHillConfiguration::default_section();
  section.points_to_win = 7;
  section.point_interval_seconds = 0.5;
  section.time_limit_seconds = 30.0;
  const gameplay::KingOfTheHillConfiguration configuration =
      gameplay::KingOfTheHillConfiguration::create(section);

  simulation::GameWorld world = simulation::GameWorld::create({});
  publish(world, configuration);

  CHECK(simulation::mode_match_state_schema_id_of(world.match().mode_state) ==
        std::string_view{"king_of_the_hill"});
  CHECK(block_of(world).points_to_win == 7);
  CHECK(block_of(world).point_interval_ticks == 200);
  CHECK(block_of(world).time_limit_ticks == 12'000);
}

TEST_CASE("a world holding another mode's arm is given the hill's",
          "[unit][gameplay][king_of_the_hill][mode_state]") {
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_match().mode_state = simulation::RoyalePlacementsModeState{};

  publish(world, gameplay::KingOfTheHillConfiguration::defaults());

  CHECK(block_of(world).points_to_win == 30);
  CHECK(block_of(world).point_interval_ticks == 400);
  CHECK(block_of(world).time_limit_ticks == 96'000);
}
