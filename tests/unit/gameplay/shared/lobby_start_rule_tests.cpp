#include "shared/lobby_start_rule.hpp"

#include "controller_id.hpp"
#include "game_world.hpp"
#include "seat_roster.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::GameWorld lobby_world(const std::size_t seat_count,
                                                const std::size_t filled_count,
                                                const bool start_requested) {
  simulation::GameWorld world = simulation::GameWorld::create({});
  if (seat_count == 0) {
    // A world nobody declared a lobby for holds the default roster with no seats; `of_size`
    // refuses zero, because a declared lobby has at least one seat.
    return world;
  }
  simulation::SeatRoster roster = simulation::SeatRoster::of_size(seat_count);
  for (std::size_t index = 0; index < filled_count; ++index) {
    roster.assign_seat(index, simulation::Seat{simulation::ControllerSeat{
                                  simulation::ControllerId::create(index + 1)}});
  }
  if (start_requested) {
    roster.request_start();
  }
  world.mutable_match().seats = roster;
  return world;
}

} // namespace

TEST_CASE("a lobby is ready to start exactly when every seat is filled and a start was requested",
          "[unit][gameplay][shared][lobby]") {
  CHECK(gameplay::lobby_ready_to_start(lobby_world(2, 2, true)));
  CHECK_FALSE(gameplay::lobby_ready_to_start(lobby_world(2, 2, false)));
  CHECK_FALSE(gameplay::lobby_ready_to_start(lobby_world(2, 1, true)));
  // A world nobody declared a lobby for has no seats, and an empty roster is never full.
  CHECK_FALSE(gameplay::lobby_ready_to_start(lobby_world(0, 0, true)));
}

TEST_CASE("a declared bot seat nobody has built yet does not fill its seat",
          "[unit][gameplay][shared][lobby]") {
  simulation::GameWorld world = lobby_world(2, 1, true);
  world.mutable_match().seats.assign_seat(
      1, simulation::Seat{
             simulation::NpcSeat{simulation::SeatKindName::create("wanderer"), std::nullopt}});
  CHECK_FALSE(gameplay::lobby_ready_to_start(world));

  world.mutable_match().seats.assign_seat(
      1, simulation::Seat{simulation::NpcSeat{simulation::SeatKindName::create("wanderer"),
                                              simulation::ControllerId::create(12)}});
  CHECK(gameplay::lobby_ready_to_start(world));
}
