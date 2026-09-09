#include "royale/royale_objective.hpp"

#include "gameplay_test_fixture.hpp"

#include "components/controllable_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "match_outcome.hpp"
#include "physics_body.hpp"
#include "royale/royale_configuration.hpp"
#include "seat_roster.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
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

// The objective reads the lobby from the world it is asked about; its configuration carries no
// seat count, because who plays is a `[match]` fact and the roster is world state.
[[nodiscard]] gameplay::RoyaleObjective royale_objective() {
  return gameplay::RoyaleObjective{gameplay::RoyaleConfiguration::defaults()};
}

// A world whose lobby has `seat_count` seats, the first `filled_count` of them held by a
// controller. The NPC arm -- filled only once its bot exists -- is covered by its own case below.
[[nodiscard]] simulation::GameWorld lobby_world(const std::size_t seat_count,
                                                const std::size_t filled_count,
                                                const bool start_requested) {
  simulation::GameWorld world = world_with_alive_entities(0);
  simulation::SeatRoster roster = simulation::SeatRoster::of_size(seat_count);
  for (std::size_t index = 0; index < filled_count; ++index) {
    roster.assign_seat(index, simulation::Seat{simulation::ControllerSeat{
                                  simulation::ControllerId::create(index + 1)}});
  }
  if (start_requested) {
    roster.request_start();
  }
  world.mutable_match().seats = std::move(roster);
  return world;
}

} // namespace

TEST_CASE("can_start is a full lobby and a requested start, and needs both",
          "[unit][gameplay][royale][objective]") {
  // The Step 2 rule. Both conjuncts are load-bearing and the four combinations say so: a full lobby
  // nobody started does not start, a start requested into an incomplete lobby does not start
  // either, and only the two together do.
  const gameplay::RoyaleObjective objective = royale_objective();

  CHECK_FALSE(objective.can_start(lobby_world(4, 4, false)));
  CHECK_FALSE(objective.can_start(lobby_world(4, 3, true)));
  CHECK_FALSE(objective.can_start(lobby_world(4, 0, true)));
  CHECK(objective.can_start(lobby_world(4, 4, true)));

  // A one-seat lobby is legal and degenerate rather than a rejection.
  CHECK(royale_objective().can_start(lobby_world(1, 1, true)));
}

TEST_CASE("a world with no declared lobby never starts a match",
          "[unit][gameplay][royale][objective]") {
  // A default-constructed `MatchState` has no seats at all, which is what every world nobody seeded
  // a lobby onto holds. "Every seat is filled" over an empty roster is vacuously true, so without
  // the roster's own "at least one seat" rule this world would start a match with nobody in it --
  // and it would do it on the first tick, before any session had connected.
  const gameplay::RoyaleObjective objective = royale_objective();
  simulation::GameWorld world = world_with_alive_entities(3);
  REQUIRE(world.match().seats.seat_count() == 0);

  CHECK_FALSE(objective.can_start(world));
  world.mutable_match().seats.request_start();
  CHECK_FALSE(objective.can_start(world));
}

TEST_CASE("a seat declared for an NPC fills that seat only once its bot exists",
          "[unit][gameplay][royale][objective]") {
  // The simulation cannot construct a controller, so a seat that names a bot kind is a declaration
  // the runtime has not yet reconciled. It does not count as filled: a lobby of declarations could
  // otherwise start a match with nobody in it
  // (`docs/reviews/2026-09-08-lobby-and-hazard-review.md`, finding 4), and the cost of waiting is
  // the one control poll the reconciliation takes.
  const gameplay::RoyaleObjective objective = royale_objective();
  simulation::GameWorld world = lobby_world(2, 1, true);
  CHECK_FALSE(objective.can_start(world));

  world.mutable_match().seats.assign_seat(
      1, simulation::Seat{
             simulation::NpcSeat{simulation::SeatKindName::create("wanderer"), std::nullopt}});
  CHECK_FALSE(objective.can_start(world));

  // Once the runtime has built the bot and its join has landed, the seat is filled exactly as a
  // person's is.
  world.mutable_match().seats.assign_seat(
      1, simulation::Seat{simulation::NpcSeat{simulation::SeatKindName::create("wanderer"),
                                              simulation::ControllerId::create(9)}});
  CHECK(objective.can_start(world));

  // And a bot that leaves -- retired by the reconciliation, or displaced -- unfills it again, which
  // is what returns a match whose field broke up during the countdown to the lobby.
  world.mutable_match().seats.assign_seat(
      1, simulation::Seat{
             simulation::NpcSeat{simulation::SeatKindName::create("wanderer"), std::nullopt}});
  CHECK_FALSE(objective.can_start(world));
}

TEST_CASE("the alive count no longer decides whether a match may start",
          "[unit][gameplay][royale][objective]") {
  // The rule this replaced was "the alive count is at or above `lobby_minimum_players`". This is
  // the regression that says it is gone: a field of five live blobs with an empty seat does not
  // start, and an empty arena whose lobby is full and started does.
  const gameplay::RoyaleObjective objective = royale_objective();

  simulation::GameWorld crowded = world_with_alive_entities(5);
  // Bound to a named world first: `GameWorld::match()` is deleted on an rvalue, because a reference
  // into a temporary world would dangle.
  const simulation::GameWorld one_short = lobby_world(2, 1, true);
  crowded.mutable_match().seats = one_short.match().seats;
  CHECK_FALSE(objective.can_start(crowded));

  CHECK(objective.can_start(lobby_world(2, 2, true)));
}

TEST_CASE("outcome names the last alive entity, draws an empty field, and is otherwise undecided",
          "[unit][gameplay][royale][objective]") {
  const gameplay::RoyaleObjective objective = royale_objective();

  CHECK(objective.outcome(world_with_alive_entities(0)) == simulation::MatchOutcome::drawn());
  CHECK(objective.outcome(world_with_alive_entities(1)) ==
        simulation::MatchOutcome::won_by_entity(simulation::EntityId::create(1)));
  CHECK(objective.outcome(world_with_alive_entities(2)) == simulation::MatchOutcome::undecided());
  CHECK(objective.outcome(world_with_alive_entities(9)) == simulation::MatchOutcome::undecided());
}

TEST_CASE("a pending entity and a body with no controller are not alive",
          "[unit][gameplay][royale][objective]") {
  // "Alive" is entities owning both a PhysicsBody and a Controllable, which is the population
  // `outcome` reads. A pending joiner owns only the controller link, a wall owns only the body, and
  // the zone entity owns neither. `can_start` is deliberately not asserted here any more: since
  // Step 2 it reads the seat roster and not this population at all, so an assertion on it would
  // pass for a reason that has nothing to do with what this case is about.
  const gameplay::RoyaleObjective objective = royale_objective();
  simulation::GameWorld world = world_with_alive_entities(1);

  world.mutable_store<simulation::Controllable>().insert_or_assign(
      simulation::EntityId::create(50),
      simulation::Controllable{simulation::ControllerId::create(50)});
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      simulation::EntityId::create(60),
      simulation::PhysicsBody::create_static(simulation::Vector2::create(500.0, 500.0)));

  CHECK(objective.outcome(world) ==
        simulation::MatchOutcome::won_by_entity(simulation::EntityId::create(1)));
}

TEST_CASE("the objective's durations are the configured tick counts",
          "[unit][gameplay][royale][objective]") {
  const gameplay::RoyaleObjective objective = royale_objective();
  CHECK(objective.durations().countdown_ticks == 2'000);
  CHECK(objective.durations().restart_delay_ticks == 3'200);
}
