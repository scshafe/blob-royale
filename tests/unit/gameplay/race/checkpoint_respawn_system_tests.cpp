#include "race/checkpoint_respawn_system.hpp"

#include "gameplay_test_fixture.hpp"
#include "race_test_fixture.hpp"

#include "components/controllable_component.hpp"
#include "components/race_progress_component.hpp"
#include "components/respawn_timer_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "map_definition.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "race/race_configuration.hpp"
#include "race/race_course.hpp"
#include "simulation_tolerance.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

const simulation::Vector2 kFirstGate = simulation::Vector2::create(300.0, 320.0);
const simulation::Vector2 kFinishGate = simulation::Vector2::create(600.0, 320.0);
const simulation::Vector2 kRest = simulation::Vector2::create(0.0, 0.0);

[[nodiscard]] simulation::EntityId entity(const std::uint64_t value) {
  return simulation::EntityId::create(value);
}

[[nodiscard]] simulation::MapDefinition return_map() {
  return testing::race_test_map("race_checkpoint_return", {kFirstGate, kFinishGate});
}

[[nodiscard]] gameplay::CheckpointRespawnSystem system() {
  return gameplay::CheckpointRespawnSystem{
      gameplay::RaceCourse::create(return_map(), gameplay::RaceConfiguration::defaults())};
}

void add_returning_racer(simulation::GameWorld& world, const std::uint64_t id,
                         const std::uint64_t next_checkpoint) {
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      entity(id), simulation::Controllable{simulation::ControllerId::create(id)});
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(
      entity(id), simulation::RaceProgress{next_checkpoint});
}

void apply(const gameplay::CheckpointRespawnSystem& respawn, simulation::GameWorld& world,
           const std::uint64_t tick = 7) {
  const testing::TickHarness harness{simulation::TickSequence::create(tick), return_map()};
  respawn.apply(world, harness.context());
}

[[nodiscard]] const simulation::PhysicsBody* body(const simulation::GameWorld& world,
                                                  const std::uint64_t id) {
  return world.store<simulation::PhysicsBody>().find(entity(id));
}

} // namespace

TEST_CASE("checkpoint return seats each racer at its last gate at rest and preserves progress",
          "[unit][gameplay][race][checkpoint_respawn]") {
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_match().phase = simulation::MatchPhase::kRunning;
  add_returning_racer(world, 1, 1);
  add_returning_racer(world, 2, 2);

  apply(system(), world);

  REQUIRE(body(world, 1) != nullptr);
  REQUIRE(body(world, 2) != nullptr);
  CHECK(body(world, 1)->position() == kFirstGate);
  CHECK(body(world, 2)->position() == kFinishGate);
  for (const std::uint64_t id : {1U, 2U}) {
    CHECK(body(world, id)->velocity() == kRest);
    CHECK(body(world, id)->acceleration() == kRest);
    CHECK(body(world, id)->radius() == testing::gameplay_configuration().player_radius());
    CHECK_FALSE(body(world, id)->is_static());
    REQUIRE(world.store<simulation::RaceProgress>().find(entity(id)) != nullptr);
    CHECK(world.store<simulation::RaceProgress>().find(entity(id))->next_checkpoint == id);
    CHECK(world.store<simulation::Controllable>().find(entity(id)) != nullptr);
  }
}

TEST_CASE(
    "checkpoint return ignores grid racers, missing controller or progress, timers and bodies",
    "[unit][gameplay][race][checkpoint_respawn]") {
  simulation::GameWorld world = simulation::GameWorld::create({});
  add_returning_racer(world, 1, 0);
  add_returning_racer(world, 2, 1);
  world.mutable_store<simulation::RespawnTimer>().insert_or_assign(entity(2),
                                                                   simulation::RespawnTimer{1});
  add_returning_racer(world, 3, 1);
  const simulation::PhysicsBody existing = simulation::PhysicsBody::create(
      simulation::Vector2::create(120.0, 320.0), simulation::Vector2::create(1.0, 2.0), kRest);
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity(3), existing);
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(entity(4),
                                                                   simulation::RaceProgress{1});
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      entity(5), simulation::Controllable{simulation::ControllerId::create(5)});

  apply(system(), world);

  CHECK(body(world, 1) == nullptr);
  CHECK(body(world, 2) == nullptr);
  REQUIRE(body(world, 3) != nullptr);
  CHECK(*body(world, 3) == existing);
  CHECK(body(world, 4) == nullptr);
  CHECK(body(world, 5) == nullptr);
  REQUIRE(world.store<simulation::RespawnTimer>().find(entity(2)) != nullptr);
  CHECK(world.store<simulation::RespawnTimer>().find(entity(2))->ticks_remaining == 1);
}

TEST_CASE("checkpoint return waits through contact tolerance and takes a vacated gate next tick",
          "[unit][gameplay][race][checkpoint_respawn]") {
  const gameplay::CheckpointRespawnSystem respawn = system();
  simulation::GameWorld world = simulation::GameWorld::create({});
  add_returning_racer(world, 1, 1);
  const double contact_distance = 2.0 * testing::gameplay_configuration().player_radius();
  const simulation::Vector2 touching = simulation::Vector2::create(
      kFirstGate.x() + contact_distance + simulation::kPositionTolerance / 2.0, kFirstGate.y());
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      entity(2), simulation::PhysicsBody::create_static(touching));
  apply(respawn, world, 7);
  CHECK(body(world, 1) == nullptr);

  world.mutable_store<simulation::PhysicsBody>().erase(entity(2));
  apply(respawn, world, 8);
  REQUIRE(body(world, 1) != nullptr);
  CHECK(body(world, 1)->position() == kFirstGate);
}

TEST_CASE("checkpoint return competing racers seat in ascending entity order from the live store",
          "[unit][gameplay][race][checkpoint_respawn]") {
  simulation::GameWorld world = simulation::GameWorld::create({});
  add_returning_racer(world, 20, 1);
  add_returning_racer(world, 5, 1);
  apply(system(), world);
  REQUIRE(body(world, 5) != nullptr);
  CHECK(body(world, 5)->position() == kFirstGate);
  CHECK(body(world, 20) == nullptr);
}

TEST_CASE("checkpoint return remains lifecycle bookkeeping outside running",
          "[unit][gameplay][race][checkpoint_respawn]") {
  for (const simulation::MatchPhase phase :
       {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
        simulation::MatchPhase::kEnded}) {
    simulation::GameWorld world = simulation::GameWorld::create({});
    world.mutable_match().phase = phase;
    add_returning_racer(world, 1, 1);
    apply(system(), world);
    REQUIRE(body(world, 1) != nullptr);
    CHECK(body(world, 1)->position() == kFirstGate);
  }
}

TEST_CASE("checkpoint return rejects progress beyond the course instead of indexing a missing gate",
          "[unit][gameplay][race][checkpoint_respawn][validation]") {
  simulation::GameWorld world = simulation::GameWorld::create({});
  add_returning_racer(world, 1, 3);
  try {
    apply(system(), world);
    FAIL("a returning racer with progress beyond the course was accepted");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.code() == "GAMEPLAY.RACE_PROGRESS_BEYOND_COURSE");
    CHECK(error.context().find("entity_id=1") != std::string::npos);
  }
}
