#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "commands/clear_seat_command.hpp"
#include "commands/seat_npc_command.hpp"
#include "commands/set_seat_count_command.hpp"
#include "commands/start_match_command.hpp"
#include "controller_id.hpp"
#include "entity_id_reservation.hpp"
#include "fixed_delta.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "match_phase.hpp"
#include "match_snapshot.hpp"
#include "seat_roster.hpp"
#include "simulation_config.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

// A world with a declared lobby and nothing else, stepped with no mode, so the only thing any of
// these ticks can change is the seat roster. There is no mode, so the engine's idle objective never
// starts a match and the phase stays `lobby` unless a test moves it deliberately -- which is what
// makes "the roster changed" the whole observable.
[[nodiscard]] simulation::GameSimulation lobby_simulation(const std::size_t seat_count) {
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_match().seats = simulation::SeatRoster::of_size(seat_count);
  return simulation::GameSimulation::create(
      simulation::SimulationConfig::create(500.0, 500.0, 10.0, 400, 8, 8), std::move(world));
}

[[nodiscard]] simulation::InputBatch batch(std::vector<simulation::Command> commands) {
  return simulation::InputBatch::create(std::move(commands), simulation::CommandKindMask::all(),
                                        simulation::EntityIdReservation::none());
}

void step(simulation::GameSimulation& game, std::vector<simulation::Command> commands) {
  game.step(simulation::FixedDelta::canonical(), batch(std::move(commands)));
}

[[nodiscard]] simulation::Command seat_npc(const std::uint64_t controller,
                                           const std::uint64_t seat_index,
                                           const std::string_view kind) {
  return simulation::Command{
      simulation::SeatNpcCommand{simulation::ControllerId::create(controller), seat_index,
                                 simulation::SeatKindName::create(kind)}};
}

[[nodiscard]] simulation::Command clear_seat(const std::uint64_t controller,
                                             const std::uint64_t seat_index) {
  return simulation::Command{
      simulation::ClearSeatCommand{simulation::ControllerId::create(controller), seat_index}};
}

[[nodiscard]] simulation::Command set_seat_count(const std::uint64_t controller,
                                                 const std::uint64_t seat_count) {
  return simulation::Command{
      simulation::SetSeatCountCommand{simulation::ControllerId::create(controller), seat_count}};
}

[[nodiscard]] simulation::Command start_match(const std::uint64_t controller) {
  return simulation::Command{
      simulation::StartMatchCommand{simulation::ControllerId::create(controller)}};
}

// The committed roster, **by value**. `GameSimulation::snapshot()` returns a snapshot by value, so
// a reference into one taken inline would dangle at the semicolon; copying a handful of seats is
// what makes every assertion below read the state it names.
[[nodiscard]] simulation::SeatRoster roster_of(const simulation::GameSimulation& game) {
  const simulation::WorldSnapshot snapshot = game.snapshot();
  return snapshot.match().seats();
}

[[nodiscard]] simulation::MatchPhase phase_of(const simulation::GameSimulation& game) {
  const simulation::WorldSnapshot snapshot = game.snapshot();
  return snapshot.match().phase();
}

[[nodiscard]] std::optional<std::string> npc_kind_at(const simulation::SeatRoster& roster,
                                                     const std::size_t index) {
  const auto* const declared = std::get_if<simulation::NpcSeat>(&roster.seats()[index]);
  return declared == nullptr ? std::nullopt : std::optional<std::string>{declared->kind.value()};
}

} // namespace

TEST_CASE("A seated NPC is recorded as a declaration with no controller yet",
          "[unit][simulation][lobby][command]") {
  simulation::GameSimulation game = lobby_simulation(4);
  step(game, {seat_npc(1, 2, "wanderer")});

  const simulation::SeatRoster roster = roster_of(game);
  REQUIRE(roster.seat_count() == 4);
  CHECK(npc_kind_at(roster, 2) == std::string{"wanderer"});
  // The simulation cannot construct a controller, so the seat is a declaration the runtime has yet
  // to reconcile. The absent controller is what a reconciliation reads to decide it has work to do.
  CHECK_FALSE(std::get<simulation::NpcSeat>(roster.seats()[2]).controller.has_value());
  CHECK(simulation::seat_is_filled(roster.seats()[2]));
  CHECK_FALSE(roster.is_full());
}

TEST_CASE("Two clients seating one seat in one tick resolve by the batch's order, and the second "
          "press is a no-op",
          "[unit][simulation][lobby][command]") {
  // The stated rule, and the reason first-wins rather than last-wins: a press must never evict
  // whoever is already sitting there. Submission order is deliberately the reverse of controller
  // order so the assertion cannot pass by accident of arrival.
  simulation::GameSimulation game = lobby_simulation(2);
  step(game, {seat_npc(9, 0, "chaser"), seat_npc(4, 0, "wanderer")});

  CHECK(npc_kind_at(roster_of(game), 0) == std::string{"wanderer"});

  // And on a later tick it is still a no-op, so "first wins" is a property of the seat rather than
  // of one batch.
  step(game, {seat_npc(9, 0, "chaser")});
  CHECK(npc_kind_at(roster_of(game), 0) == std::string{"wanderer"});
}

TEST_CASE("One controller may clear a seat and refill it in a single tick",
          "[unit][simulation][lobby][command]") {
  // This is the only way to replace an occupant, and it works because `clear_seat` ranks ahead of
  // `seat_npc` in phase 0's application order. Reversing the two ranks would make replacing a seat
  // impossible without a wasted tick.
  simulation::GameSimulation game = lobby_simulation(2);
  step(game, {seat_npc(1, 0, "wanderer")});
  REQUIRE(npc_kind_at(roster_of(game), 0) == std::string{"wanderer"});

  step(game, {seat_npc(1, 0, "chaser"), clear_seat(1, 0)});
  CHECK(npc_kind_at(roster_of(game), 0) == std::string{"chaser"});
}

TEST_CASE("Clearing empties a declared NPC seat and leaves a live controller's seat alone",
          "[unit][simulation][lobby][command]") {
  simulation::GameSimulation game = lobby_simulation(2);
  step(game, {seat_npc(1, 0, "wanderer")});
  const simulation::SeatRoster seated = roster_of(game);
  REQUIRE(simulation::seat_is_filled(seated.seats()[0]));

  step(game, {clear_seat(1, 0)});
  const simulation::SeatRoster emptied = roster_of(game);
  CHECK_FALSE(simulation::seat_is_filled(emptied.seats()[0]));

  // A seat a live session holds is the runtime's to give and take. A tick that emptied one would be
  // contradicted by the next reconciliation, which seats that session again because it is still
  // connected, so the command declines rather than lying. The seat is written into the world
  // directly, because no command can produce a controller seat before plan Step 7.
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_match().seats = simulation::SeatRoster::of_size(2);
  world.mutable_match().seats.assign_seat(
      0, simulation::Seat{simulation::ControllerSeat{simulation::ControllerId::create(3)}});
  simulation::GameSimulation occupied = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(500.0, 500.0, 10.0, 400, 8, 8), std::move(world));

  step(occupied, {clear_seat(9, 0)});
  const simulation::SeatRoster after_clear = roster_of(occupied);
  REQUIRE(std::holds_alternative<simulation::ControllerSeat>(after_clear.seats()[0]));
  CHECK(std::get<simulation::ControllerSeat>(after_clear.seats()[0]).controller ==
        simulation::ControllerId::create(3));

  // Seating over it is refused too, by the ordinary first-wins rule, so the two commands agree
  // about who owns a person's seat rather than disagreeing at the edges.
  step(occupied, {seat_npc(9, 0, "wanderer")});
  const simulation::SeatRoster after_seat = roster_of(occupied);
  CHECK(std::holds_alternative<simulation::ControllerSeat>(after_seat.seats()[0]));
}

TEST_CASE("A lobby grows and shrinks, and never shrinks past somebody sitting down",
          "[unit][simulation][lobby][command]") {
  simulation::GameSimulation game = lobby_simulation(2);
  step(game, {set_seat_count(1, 5)});
  CHECK(roster_of(game).seat_count() == 5);

  step(game, {seat_npc(1, 3, "wanderer")});
  step(game, {set_seat_count(1, 2)});
  // Ignored, not rejected: a client's view of the roster lags the world by a frame, and racing it
  // must not fail a tick everybody shares.
  const simulation::SeatRoster refused = roster_of(game);
  CHECK(refused.seat_count() == 5);
  CHECK(npc_kind_at(refused, 3) == std::string{"wanderer"});

  step(game, {set_seat_count(1, 4)});
  CHECK(roster_of(game).seat_count() == 4);

  // Growing and seating the new seat in one tick works, because `set_seat_count` ranks ahead of
  // `seat_npc`.
  step(game, {seat_npc(1, 5, "chaser"), set_seat_count(1, 6)});
  const simulation::SeatRoster grown = roster_of(game);
  CHECK(grown.seat_count() == 6);
  CHECK(npc_kind_at(grown, 5) == std::string{"chaser"});
}

TEST_CASE("A seat index inside the wire bound that names no seat is ignored",
          "[unit][simulation][lobby][command]") {
  // The boundary bounds the *value* at `kMaximumLobbySeatCount`; only the tick knows how many seats
  // this lobby actually has, and an index past the end is a stale view rather than an attack.
  simulation::GameSimulation game = lobby_simulation(2);
  step(game, {seat_npc(1, 40, "wanderer"), clear_seat(1, 40)});

  const simulation::SeatRoster roster = roster_of(game);
  CHECK(roster.seat_count() == 2);
  for (const simulation::Seat& seat : roster.seats()) {
    CHECK_FALSE(simulation::seat_is_filled(seat));
  }
}

TEST_CASE("Pressing Start records a request and commits no transition",
          "[unit][simulation][lobby][command]") {
  simulation::GameSimulation game = lobby_simulation(1);
  step(game, {start_match(1)});

  // The flag is set even though the lobby is empty: the press is a fact about a person, and the
  // objective decides separately whether the field is complete.
  const simulation::SeatRoster pressed = roster_of(game);
  CHECK(pressed.start_requested());
  CHECK_FALSE(pressed.is_full());
  CHECK(phase_of(game) == simulation::MatchPhase::kLobby);

  // Pressing twice is one request, and the request survives until the seats fill or the match ends.
  step(game, {start_match(1), start_match(2)});
  CHECK(roster_of(game).start_requested());

  step(game, {seat_npc(1, 0, "wanderer")});
  const simulation::SeatRoster complete = roster_of(game);
  CHECK(complete.is_full());
  CHECK(complete.start_requested());
}

TEST_CASE("Every lobby command is refused outside the lobby phase",
          "[unit][simulation][lobby][command]") {
  // A seat roster is only meaningful before a match. Resizing the field mid-match, or seating a bot
  // into a running game, are changes the arena has no way to represent, so the guard is one line
  // for all four rather than four separate opinions.
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_match().seats = simulation::SeatRoster::of_size(2);
  world.mutable_match().phase = simulation::MatchPhase::kRunning;
  simulation::GameSimulation game = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(500.0, 500.0, 10.0, 400, 8, 8), std::move(world));

  step(game, {set_seat_count(1, 6), seat_npc(1, 0, "wanderer"), clear_seat(1, 1), start_match(1)});

  const simulation::WorldSnapshot snapshot = game.snapshot();
  const simulation::SeatRoster roster = snapshot.match().seats();
  CHECK(snapshot.match().phase() == simulation::MatchPhase::kRunning);
  CHECK(roster.seat_count() == 2);
  CHECK_FALSE(roster.start_requested());
  for (const simulation::Seat& seat : roster.seats()) {
    CHECK_FALSE(simulation::seat_is_filled(seat));
  }
}

TEST_CASE("A lobby command is applied at phase 0, so a replayed log reproduces the roster exactly",
          "[unit][simulation][lobby][command][determinism]") {
  // Determinism is inherited rather than reinvented: the commands are ordinary batch members in the
  // canonical order, so two fresh runs of one log commit the same roster on the same tick.
  const auto run = [] {
    simulation::GameSimulation game = lobby_simulation(3);
    step(game, {seat_npc(5, 1, "chaser"), seat_npc(2, 0, "wanderer"), set_seat_count(9, 4)});
    step(game, {seat_npc(2, 2, "wanderer"), clear_seat(5, 1), start_match(7)});
    return game.snapshot();
  };

  const simulation::WorldSnapshot first = run();
  const simulation::WorldSnapshot second = run();
  CHECK(first == second);
  const simulation::SeatRoster roster = first.match().seats();
  CHECK(roster.seat_count() == 4);
  CHECK(npc_kind_at(roster, 0) == std::string{"wanderer"});
  CHECK_FALSE(simulation::seat_is_filled(roster.seats()[1]));
  CHECK(npc_kind_at(roster, 2) == std::string{"wanderer"});
  CHECK(roster.start_requested());
}
