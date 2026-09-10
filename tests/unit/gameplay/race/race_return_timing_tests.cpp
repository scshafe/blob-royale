#include "race/race_mode.hpp"

#include "gameplay_test_fixture.hpp"
#include "race_test_fixture.hpp"

#include "commands/despawn_command.hpp"
#include "components/race_progress_component.hpp"
#include "components/respawn_timer_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "race/race_configuration.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

// Explicit scenario IDs stay beyond the small reservation blocks these horizons consume.
constexpr std::uint64_t kGridRacer = 101;
constexpr std::uint64_t kCheckpointRacer = 102;
constexpr std::uint64_t kGateBlocker = 103;
const simulation::Vector2 kGrid = simulation::Vector2::create(100.0, 320.0);
const simulation::Vector2 kFirstGate = simulation::Vector2::create(300.0, 320.0);
const simulation::Vector2 kFinishGate = simulation::Vector2::create(600.0, 320.0);
const simulation::Vector2 kRest = simulation::Vector2::create(0.0, 0.0);

[[nodiscard]] simulation::EntityId entity(const std::uint64_t value) {
  return simulation::EntityId::create(value);
}

[[nodiscard]] simulation::GameSimulation return_simulation(const std::uint64_t delay_ticks,
                                                           const bool occupied_gate = false) {
  simulation::MapDefinition map =
      testing::race_test_map("race_return_timing", {kFirstGate, kFinishGate});
  const simulation::SimulationConfig simulation_configuration = testing::gameplay_configuration();
  gameplay::RaceConfiguration::Section section = gameplay::RaceConfiguration::default_section();
  section.respawn_delay_seconds = static_cast<double>(delay_ticks) /
                                  static_cast<double>(simulation_configuration.ticks_per_second());
  const gameplay::RaceConfiguration race_configuration =
      gameplay::RaceConfiguration::create(section);
  std::vector<simulation::GameWorld::EntitySeed> seeds{
      simulation::GameWorld::EntitySeed::create(
          entity(kGridRacer),
          simulation::PhysicsBody::create(simulation::Vector2::create(100.0, 450.0), kRest, kRest),
          simulation::ControllerId::create(1)),
      simulation::GameWorld::EntitySeed::create(
          entity(kCheckpointRacer),
          simulation::PhysicsBody::create(simulation::Vector2::create(300.0, 450.0), kRest, kRest),
          simulation::ControllerId::create(2))};
  if (occupied_gate) {
    seeds.push_back(simulation::GameWorld::EntitySeed{
        entity(kGateBlocker), simulation::PhysicsBody::create_static(kFirstGate), std::nullopt});
  }
  simulation::GameWorld world =
      simulation::GameWorld::create(simulation_configuration, map, 0, std::move(seeds));
  world.mutable_match().phase = simulation::MatchPhase::kRunning;
  world.mutable_match().previous_phase = simulation::MatchPhase::kRunning;
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(entity(kGridRacer),
                                                                   simulation::RaceProgress{0});
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(entity(kCheckpointRacer),
                                                                   simulation::RaceProgress{1});
  return simulation::GameSimulation::create(
      simulation_configuration, std::move(world),
      simulation::GameSimulationSetup::of_mode(std::move(map),
                                               gameplay::RaceMode::create(race_configuration)));
}

[[nodiscard]] std::optional<std::uint64_t> progress_of(const simulation::WorldSnapshot& snapshot,
                                                       const std::uint64_t id) {
  for (const auto& entry : snapshot.components<simulation::RaceProgress>()) {
    if (entry.entity == entity(id)) {
      return entry.value.next_checkpoint;
    }
  }
  return std::nullopt;
}

void check_returned_body(const simulation::WorldSnapshot& snapshot, const std::uint64_t id,
                         const simulation::Vector2& position) {
  const std::optional<simulation::PhysicsBody> body = testing::published_body(snapshot, id);
  REQUIRE(body.has_value());
  CHECK(body->position() == position);
  CHECK(body->velocity() == kRest);
  CHECK(body->acceleration() == kRest);
  CHECK(body->radius() == testing::gameplay_configuration().player_radius());
}

} // namespace

TEST_CASE(
    "race grid and checkpoint routes return on N plus delay plus one for zero, one and three ticks",
    "[unit][gameplay][race][return_timing]") {
  for (const std::uint64_t delay : {0U, 1U, 3U}) {
    CAPTURE(delay);
    testing::SteppedGame driver{return_simulation(delay)};
    // The newcomer arrives after running began, so it must never gain a body or progress.
    const simulation::EntityId newcomer = driver.next_entity_id();
    simulation::WorldSnapshot snapshot = driver.step({testing::spawn_command(3)});
    REQUIRE(snapshot.tick_sequence() == simulation::TickSequence::create(1));
    CHECK_FALSE(testing::published_body(snapshot, kGridRacer).has_value());
    CHECK_FALSE(testing::published_body(snapshot, kCheckpointRacer).has_value());
    CHECK(progress_of(snapshot, kGridRacer) == 0);
    CHECK(progress_of(snapshot, kCheckpointRacer) == 1);
    CHECK(snapshot.components<simulation::RespawnTimer>().size() == (delay == 0 ? 0U : 2U));

    for (std::uint64_t elapsed = 1; elapsed <= delay; ++elapsed) {
      snapshot = driver.step();
      CHECK_FALSE(testing::published_body(snapshot, kGridRacer).has_value());
      CHECK_FALSE(testing::published_body(snapshot, kCheckpointRacer).has_value());
    }
    CHECK(snapshot.components<simulation::RespawnTimer>().empty());
    snapshot = driver.step();
    CHECK(snapshot.tick_sequence() == simulation::TickSequence::create(1 + delay + 1));
    check_returned_body(snapshot, kGridRacer, kGrid);
    check_returned_body(snapshot, kCheckpointRacer, kFirstGate);
    CHECK(progress_of(snapshot, kGridRacer) == 0);
    CHECK(progress_of(snapshot, kCheckpointRacer) == 1);
    CHECK_FALSE(testing::published_body(snapshot, newcomer.value()).has_value());
    CHECK(progress_of(snapshot, newcomer.value()) == std::nullopt);
    CHECK(snapshot.match().phase() == simulation::MatchPhase::kRunning);
  }
}

TEST_CASE(
    "race checkpoint return blocked on its due tick seats on the next tick after the gate clears",
    "[unit][gameplay][race][return_timing]") {
  testing::SteppedGame driver{return_simulation(1, true)};
  simulation::WorldSnapshot snapshot = driver.advance(3);
  REQUIRE(snapshot.tick_sequence() == simulation::TickSequence::create(3));
  check_returned_body(snapshot, kGridRacer, kGrid);
  CHECK_FALSE(testing::published_body(snapshot, kCheckpointRacer).has_value());
  CHECK(snapshot.components<simulation::RespawnTimer>().empty());

  snapshot = driver.step({simulation::Command{simulation::DespawnCommand{entity(kGateBlocker)}}});
  CHECK(snapshot.tick_sequence() == simulation::TickSequence::create(4));
  check_returned_body(snapshot, kCheckpointRacer, kFirstGate);
  CHECK(progress_of(snapshot, kCheckpointRacer) == 1);
}
