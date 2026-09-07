#include "royale/royale_objective.hpp"

#include "gameplay_test_fixture.hpp"

#include "components/controllable_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "match_outcome.hpp"
#include "physics_body.hpp"
#include "royale/royale_configuration.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::GameWorld world_with_alive_entities(const std::uint64_t alive_count) {
  std::vector<simulation::GameWorld::EntitySeed> seeds;
  for (std::uint64_t index = 0; index < alive_count; ++index) {
    seeds.push_back(simulation::GameWorld::EntitySeed::create(
        simulation::EntityId::create(index + 1),
        simulation::PhysicsBody::create(
            simulation::Vector2::create(50.0 * static_cast<double>(index + 1), 50.0),
            simulation::Vector2::create(0.0, 0.0), simulation::Vector2::create(0.0, 0.0)),
        simulation::ControllerId::create(index + 1)));
  }
  return simulation::GameWorld::create(std::move(seeds));
}

[[nodiscard]] gameplay::RoyaleObjective objective_for(const std::uint64_t lobby_minimum_players) {
  gameplay::RoyaleConfiguration::Section section = gameplay::RoyaleConfiguration::default_section();
  section.lobby_minimum_players = lobby_minimum_players;
  return gameplay::RoyaleObjective{gameplay::RoyaleConfiguration::create(section)};
}

} // namespace

TEST_CASE("can_start is the alive count against the configured lobby minimum",
          "[unit][gameplay][royale][objective]") {
  const gameplay::RoyaleObjective objective = objective_for(2);

  CHECK_FALSE(objective.can_start(world_with_alive_entities(0)));
  CHECK_FALSE(objective.can_start(world_with_alive_entities(1)));
  CHECK(objective.can_start(world_with_alive_entities(2)));
  CHECK(objective.can_start(world_with_alive_entities(5)));

  // `lobby_minimum_players = 1` is a legal, degenerate configuration rather than a rejection.
  CHECK(objective_for(1).can_start(world_with_alive_entities(1)));
}

TEST_CASE("outcome names the last alive entity, draws an empty field, and is otherwise undecided",
          "[unit][gameplay][royale][objective]") {
  const gameplay::RoyaleObjective objective = objective_for(2);

  CHECK(objective.outcome(world_with_alive_entities(0)) == simulation::MatchOutcome::drawn());
  CHECK(objective.outcome(world_with_alive_entities(1)) ==
        simulation::MatchOutcome::won_by_entity(simulation::EntityId::create(1)));
  CHECK(objective.outcome(world_with_alive_entities(2)) == simulation::MatchOutcome::undecided());
  CHECK(objective.outcome(world_with_alive_entities(9)) == simulation::MatchOutcome::undecided());
}

TEST_CASE("a pending entity and a body with no controller are not alive",
          "[unit][gameplay][royale][objective]") {
  // "Alive" is entities owning both a PhysicsBody and a Controllable, which is the only population
  // the objective reads. A pending joiner owns only the controller link, a wall owns only the body,
  // and the zone entity owns neither.
  const gameplay::RoyaleObjective objective = objective_for(2);
  simulation::GameWorld world = world_with_alive_entities(1);

  world.mutable_store<simulation::Controllable>().insert_or_assign(
      simulation::EntityId::create(50),
      simulation::Controllable{simulation::ControllerId::create(50)});
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      simulation::EntityId::create(60),
      simulation::PhysicsBody::create_static(simulation::Vector2::create(500.0, 500.0)));

  CHECK_FALSE(objective.can_start(world));
  CHECK(objective.outcome(world) ==
        simulation::MatchOutcome::won_by_entity(simulation::EntityId::create(1)));
}

TEST_CASE("the objective's durations are the configured tick counts",
          "[unit][gameplay][royale][objective]") {
  const gameplay::RoyaleObjective objective = objective_for(2);
  CHECK(objective.durations().countdown_ticks == 2'000);
  CHECK(objective.durations().restart_delay_ticks == 3'200);
}
