#include "seat_roster.hpp"

#include "controller_id.hpp"
#include "match_state.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::Seat controller_seat(const std::uint64_t controller) {
  return simulation::Seat{simulation::ControllerSeat{simulation::ControllerId::create(controller)}};
}

// A seat declared for an NPC the runtime has not created yet, which is the state every `seat_npc`
// command produces and the state a reconciliation has to be able to tell from a created one.
[[nodiscard]] simulation::Seat npc_seat(const std::string_view kind) {
  return simulation::Seat{
      simulation::NpcSeat{simulation::SeatKindName::create(kind), std::nullopt}};
}

// The same seat once a bot exists for it. The kind survives, which is what lets a reconciliation
// ask "is this already the right bot?" rather than "is anybody here?".
[[nodiscard]] simulation::Seat seated_npc(const std::string_view kind,
                                          const std::uint64_t controller) {
  return simulation::Seat{simulation::NpcSeat{simulation::SeatKindName::create(kind),
                                              simulation::ControllerId::create(controller)}};
}

} // namespace

TEST_CASE("a default SeatRoster is the absence of a lobby rather than an empty one",
          "[unit][simulation][seat_roster]") {
  // This is the value every world holds until a caller declares a lobby, and it is the value the
  // engine's own `MatchState` default carries. `is_full()` must be **false** for it: "every seat is
  // filled" over zero seats is vacuously true, and a mode whose `can_start` asked only that
  // question would start a match with nobody in it on the very first tick.
  const simulation::SeatRoster roster;

  CHECK(roster.seat_count() == 0);
  CHECK(roster.seats().empty());
  CHECK_FALSE(roster.is_full());
  CHECK_FALSE(roster.start_requested());

  const simulation::MatchState state;
  CHECK(state.seats == roster);
}

TEST_CASE("a declared lobby is between one seat and the engine's ceiling",
          "[unit][simulation][seat_roster][validation]") {
  CHECK_THROWS_AS(simulation::SeatRoster::of_size(0), simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::SeatRoster::of_size(simulation::SeatRoster::kMaximumSeatCount + 1),
                  simulation::SimulationValidationError);

  CHECK(simulation::SeatRoster::of_size(1).seat_count() == 1);
  CHECK(simulation::SeatRoster::of_size(simulation::SeatRoster::kMaximumSeatCount).seat_count() ==
        simulation::SeatRoster::kMaximumSeatCount);
  CHECK(simulation::SeatRoster::kMaximumSeatCount == simulation::kMaximumLobbySeatCount);

  try {
    static_cast<void>(simulation::SeatRoster::of_size(0));
    FAIL("a lobby of no seats was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kSeatRosterSeatCountOutOfRange);
    CHECK(error.code() == std::string_view{"SIMULATION.SEAT_ROSTER_SEAT_COUNT_OUT_OF_RANGE"});
  }
}

TEST_CASE("a declared lobby begins empty and fills seat by seat",
          "[unit][simulation][seat_roster]") {
  simulation::SeatRoster roster = simulation::SeatRoster::of_size(3);
  REQUIRE(roster.seat_count() == 3);
  for (const simulation::Seat& seat : roster.seats()) {
    CHECK_FALSE(simulation::seat_is_filled(seat));
  }
  CHECK_FALSE(roster.is_full());

  roster.assign_seat(0, controller_seat(7));
  roster.assign_seat(2, controller_seat(9));
  CHECK_FALSE(roster.is_full());

  // The third seat is declared for a bot the runtime has not created yet. That occupies it -- a
  // resize may not drop it -- but does not fill it: a lobby of declarations cannot start a match
  // with nobody in it, so the lobby waits for the bot to exist.
  roster.assign_seat(1, npc_seat("wanderer"));
  CHECK_FALSE(simulation::seat_is_filled(roster.seats()[1]));
  CHECK(simulation::seat_is_occupied(roster.seats()[1]));
  CHECK_FALSE(roster.is_full());
  CHECK(std::holds_alternative<simulation::NpcSeat>(roster.seats()[1]));
  CHECK(std::get<simulation::NpcSeat>(roster.seats()[1]).kind == std::string_view{"wanderer"});

  // Once the reconciliation has built the bot, the seat is filled exactly as a person's is.
  roster.assign_seat(1, seated_npc("wanderer", 12));
  CHECK(simulation::seat_is_filled(roster.seats()[1]));
  CHECK(roster.is_full());

  // Clearing one seat unfills the lobby, which is what returns a match in `countdown` to `lobby`.
  roster.assign_seat(1, simulation::Seat{simulation::EmptySeat{}});
  CHECK_FALSE(roster.is_full());
}

TEST_CASE("a seat index the roster does not have is a rejection, not a resize",
          "[unit][simulation][seat_roster][validation]") {
  simulation::SeatRoster roster = simulation::SeatRoster::of_size(2);
  CHECK_THROWS_AS(roster.assign_seat(2, controller_seat(1)), simulation::SimulationValidationError);
  CHECK(roster.seat_count() == 2);

  try {
    simulation::SeatRoster{}.assign_seat(0, controller_seat(1));
    FAIL("a seat was assigned in a roster with no seats");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kSeatRosterSeatIndexOutOfRange);
    CHECK(error.code() == std::string_view{"SIMULATION.SEAT_ROSTER_SEAT_INDEX_OUT_OF_RANGE"});
  }
}

TEST_CASE("the start request is a flag anyone may set twice and clear twice",
          "[unit][simulation][seat_roster]") {
  // Idempotent in both directions, because two clients pressing Start on one tick is one request
  // and a machine that arrives in `lobby` twice must not care that it already cleared the flag.
  simulation::SeatRoster roster = simulation::SeatRoster::of_size(2);
  CHECK_FALSE(roster.start_requested());

  roster.request_start();
  CHECK(roster.start_requested());
  roster.request_start();
  CHECK(roster.start_requested());

  roster.clear_start_request();
  CHECK_FALSE(roster.start_requested());
  roster.clear_start_request();
  CHECK_FALSE(roster.start_requested());

  // The request is part of the roster's value, so two rosters that differ only in it are different
  // world states and a snapshot of one is not a snapshot of the other.
  simulation::SeatRoster started = simulation::SeatRoster::of_size(2);
  started.request_start();
  CHECK_FALSE(started == roster);
}

TEST_CASE("a seat's declared kind is the published kind-name grammar and nothing looser",
          "[unit][simulation][seat_roster][validation]") {
  // The same rule `common.schema.json#/$defs/kind_name` states, because this name is accepted from
  // a client and published back to every client: a name this admitted could not be encoded.
  CHECK(simulation::SeatKindName::create("wanderer") == std::string_view{"wanderer"});
  CHECK(simulation::SeatKindName::create("chaser_2") == std::string_view{"chaser_2"});
  CHECK(simulation::SeatKindName{}.empty());

  CHECK_THROWS_AS(simulation::SeatKindName::create(""), simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::SeatKindName::create("Wanderer"),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::SeatKindName::create("2wanderer"),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::SeatKindName::create("wanderer bot"),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(
      simulation::SeatKindName::create(std::string(simulation::kMaximumKindNameLength + 1, 'a')),
      simulation::SimulationValidationError);
  CHECK_NOTHROW(
      simulation::SeatKindName::create(std::string(simulation::kMaximumKindNameLength, 'a')));

  try {
    static_cast<void>(simulation::SeatKindName::create("Wanderer"));
    FAIL("a kind name outside the published grammar was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() == simulation::SimulationValidationCode::kSeatKindNameInvalid);
    CHECK(error.code() == std::string_view{"SIMULATION.SEAT_KIND_NAME_INVALID"});
  }
}

TEST_CASE("two seats holding different things are different world state",
          "[unit][simulation][seat_roster]") {
  // The roster is part of `MatchState`, which is part of world equality, so a lobby change is a
  // committed state change like any other. Two seats of different arms and two of the same arm
  // holding different values must all compare unequal, or a snapshot could not tell a lobby of four
  // people from a lobby of four bots.
  simulation::SeatRoster human = simulation::SeatRoster::of_size(1);
  simulation::SeatRoster other_human = simulation::SeatRoster::of_size(1);
  simulation::SeatRoster bot = simulation::SeatRoster::of_size(1);
  simulation::SeatRoster other_bot = simulation::SeatRoster::of_size(1);
  human.assign_seat(0, controller_seat(1));
  other_human.assign_seat(0, controller_seat(2));
  bot.assign_seat(0, npc_seat("wanderer"));
  other_bot.assign_seat(0, npc_seat("chaser"));

  CHECK_FALSE(human == other_human);
  CHECK_FALSE(human == bot);
  CHECK_FALSE(bot == other_bot);
  CHECK(human == [] {
    simulation::SeatRoster same = simulation::SeatRoster::of_size(1);
    same.assign_seat(0, controller_seat(1));
    return same;
  }());

  // A roster of a different size is a different lobby even when every seat matches.
  simulation::SeatRoster wider = simulation::SeatRoster::of_size(2);
  wider.assign_seat(0, controller_seat(1));
  CHECK_FALSE(human == wider);
}

TEST_CASE("an NPC seat remembers the bot the runtime built for it and keeps its kind",
          "[unit][simulation][seat_roster]") {
  // The two states a reconciliation must tell apart. Without the controller they compare equal on
  // the kind alone, and "this seat needs a wanderer" would be indistinguishable from "this seat has
  // one" -- which is a reconciler that builds a second bot every tick.
  const simulation::Seat declared = npc_seat("wanderer");
  const simulation::Seat created = seated_npc("wanderer", 12);
  CHECK(declared != created);
  // Both are occupied -- the seat is spoken for either way -- and only the created one is filled.
  CHECK(simulation::seat_is_occupied(declared));
  CHECK(simulation::seat_is_occupied(created));
  CHECK_FALSE(simulation::seat_is_filled(declared));
  CHECK(simulation::seat_is_filled(created));

  CHECK_FALSE(std::get<simulation::NpcSeat>(declared).controller.has_value());
  REQUIRE(std::get<simulation::NpcSeat>(created).controller.has_value());
  CHECK(std::get<simulation::NpcSeat>(created).controller->value() == 12);

  // **The kind survives the seating.** A created NPC seat is still an `NpcSeat`, never a
  // `ControllerSeat`, so the kind a player chose is still there to compare against and to render.
  CHECK(std::get<simulation::NpcSeat>(created).kind == std::string_view{"wanderer"});
  CHECK(seated_npc("chaser", 12) != created);
}

TEST_CASE("a lobby grows freely and never shrinks past somebody sitting down",
          "[unit][simulation][seat_roster]") {
  simulation::SeatRoster roster = simulation::SeatRoster::of_size(2);
  roster.assign_seat(0, controller_seat(3));

  // Growing appends empty seats at the high indices, so nobody's seat moves under them.
  CHECK(roster.try_set_seat_count(5));
  CHECK(roster.seat_count() == 5);
  CHECK(roster.seats()[0] == controller_seat(3));
  for (std::size_t index = 1; index < roster.seat_count(); ++index) {
    CHECK_FALSE(simulation::seat_is_filled(roster.seats()[index]));
  }

  // Shrinking over empty seats is allowed and shrinking over an occupied one is refused **and
  // changes nothing**, which is the difference between a rule and a warning. Occupied, not filled:
  // a seat declared for a bot nobody has built yet is still somebody's decision, and a resize does
  // not get to drop it.
  roster.assign_seat(3, npc_seat("wanderer"));
  CHECK_FALSE(simulation::seat_is_filled(roster.seats()[3]));
  CHECK_FALSE(roster.try_set_seat_count(2));
  CHECK(roster.seat_count() == 5);
  CHECK(roster.seats()[3] == npc_seat("wanderer"));

  // Exactly one above the highest filled seat is the floor, so the rule a client renders -- the
  // seat-count control stops at highest_filled + 1 -- is the rule the tick applies.
  CHECK(roster.try_set_seat_count(4));
  CHECK(roster.seat_count() == 4);
  CHECK_FALSE(roster.try_set_seat_count(3));

  // A resize to the count it already has is a no-op that still reports success: a client that sent
  // the number it is already showing has not failed at anything.
  CHECK(roster.try_set_seat_count(4));
  CHECK(roster.seat_count() == 4);
}

TEST_CASE("a seat count outside the declared range is a defect rather than a lagging view",
          "[unit][simulation][seat_roster]") {
  // The asymmetry is deliberate. A shrink past an occupant is something a client can legitimately
  // ask for with a one-frame-old view of the roster, so it is a silent no-op. A count outside
  // `[1, kMaximumLobbySeatCount]` was range-checked at the boundary and again by
  // `InputBatch::create`, so one arriving here means those two disagree with this one.
  simulation::SeatRoster roster = simulation::SeatRoster::of_size(2);
  CHECK_THROWS_AS(roster.try_set_seat_count(0), simulation::SimulationValidationError);
  CHECK_THROWS_AS(roster.try_set_seat_count(simulation::kMaximumLobbySeatCount + 1),
                  simulation::SimulationValidationError);
  CHECK(roster.seat_count() == 2);
  CHECK(roster.try_set_seat_count(simulation::kMaximumLobbySeatCount));
  CHECK(roster.seat_count() == simulation::kMaximumLobbySeatCount);
}
