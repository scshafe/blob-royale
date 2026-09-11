#include "shared/match_reset_system.hpp"

#include "fixtures/thrust_steering_fixture.hpp"
#include "gameplay_test_fixture.hpp"

#include "components/controllable_component.hpp"
#include "components/respawn_timer_component.hpp"
#include "components/zone_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "physics_body.hpp"
#include "seat_roster.hpp"
#include "simulation_system.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <cstdint>
#include <memory>

#include <catch2/catch_test_macros.hpp>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] simulation::EntityId entity(const std::uint64_t id) {
  return simulation::EntityId::create(id);
}

// Entity 1 is alive; 2 is out of play with a timer; 3 awaits its first seat; 20 is a wall; 30 is
// a zone-shaped prop with neither body nor controller. The lobby holds one seat, taken by the
// person driving entity 1.
[[nodiscard]] simulation::GameWorld field_in(const simulation::MatchPhase phase,
                                             const simulation::MatchPhase previous_phase) {
  simulation::GameWorld world =
      simulation::GameWorld::create({simulation::GameWorld::EntitySeed::create(
          entity(1),
          simulation::PhysicsBody::create(simulation::Vector2::create(60.0, 60.0),
                                          simulation::Vector2::create(0.0, 0.0),
                                          simulation::Vector2::create(0.0, 0.0)),
          simulation::ControllerId::create(1))});
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      entity(2), simulation::Controllable{simulation::ControllerId::create(2)});
  world.mutable_store<simulation::RespawnTimer>().insert_or_assign(entity(2),
                                                                   simulation::RespawnTimer{40});
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      entity(3), simulation::Controllable{simulation::ControllerId::create(3)});
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      entity(20),
      simulation::PhysicsBody::create_static(simulation::Vector2::create(300.0, 300.0)));
  world.mutable_store<simulation::Zone>().insert_or_assign(
      entity(30), simulation::Zone{simulation::Vector2::create(480.0, 320.0), 500.0});
  world.mutable_match().phase = phase;
  world.mutable_match().previous_phase = previous_phase;
  world.mutable_match().seats = simulation::SeatRoster::of_size(1);
  world.mutable_match().seats.assign_seat(
      0, simulation::Seat{simulation::ControllerSeat{simulation::ControllerId::create(1)}});
  return world;
}

void apply(simulation::GameWorld& world) {
  const std::unique_ptr<const simulation::SimulationSystem> reset =
      gameplay::MatchResetSystem::create();
  const testing::TickHarness harness{simulation::TickSequence::create(9)};
  reset->apply(world, harness.context());
}

} // namespace

TEST_CASE("the lobby tick after ended destroys every participant and nothing else",
          "[unit][gameplay][shared][reset]") {
  simulation::GameWorld world =
      field_in(simulation::MatchPhase::kLobby, simulation::MatchPhase::kEnded);
  const simulation::MovementTuningState movement{testing::thrust_steering_fixture::retuned(),
                                                 testing::gameplay_movement_tuning(), 3,
                                                 simulation::TickSequence::create(5)};
  world.mutable_match().movement = movement;

  apply(world);

  CHECK_FALSE(world.contains(entity(1)));
  CHECK_FALSE(world.contains(entity(2)));
  CHECK_FALSE(world.contains(entity(3)));
  CHECK(world.contains(entity(20)));
  CHECK(world.contains(entity(30)));
  // Seats are engine state and survive: the person keeps theirs and re-spawns onto it.
  CHECK(world.match().seats.seat_count() == 1);
  CHECK(world.match().seats.is_full());
  // Round reset destroys participants, not room settings or their authored Reset destination.
  CHECK(world.match().movement.current == movement.current);
  CHECK(world.match().movement.defaults == movement.defaults);
  CHECK(world.match().movement == movement);
}

TEST_CASE("every other pair of phases leaves the field alone", "[unit][gameplay][shared][reset]") {
  for (const simulation::MatchPhase phase : simulation::kMatchPhases) {
    for (const simulation::MatchPhase previous : simulation::kMatchPhases) {
      if (phase == simulation::MatchPhase::kLobby && previous == simulation::MatchPhase::kEnded) {
        continue;
      }
      INFO("phase " << simulation::match_phase_name(phase) << " after "
                    << simulation::match_phase_name(previous));
      simulation::GameWorld world = field_in(phase, previous);
      apply(world);
      CHECK(world.contains(entity(1)));
      CHECK(world.contains(entity(2)));
      CHECK(world.contains(entity(3)));
    }
  }
}
