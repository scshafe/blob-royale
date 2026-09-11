#include "fixtures/movement_tuning_command_fixture.hpp"

#include "components/controllable_component.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <type_traits>

namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::movement_tuning_command_fixture;
using Status = simulation::MovementTuningDecisionStatus;

TEST_CASE(
    "movement tuning selects the last eligible canonical controller against the entry revision",
    "[unit][simulation][movement_tuning]") {
  auto game = fixture::game();
  const auto result = game.step(
      simulation::FixedDelta::canonical(),
      fixture::batch({fixture::command(2, 1, 0, fixture::kSecondPair), fixture::command(4, 2, 0),
                      fixture::command(1, 900, 0), fixture::command(3, 3, 1)}));
  const auto entries = result.entries();
  REQUIRE(entries.size() == 4);
  CHECK(entries[0].controller == fixture::controller(1));
  CHECK(entries[0].status == Status::kSuperseded);
  CHECK(entries[1].status == Status::kApplied);
  CHECK(entries[2].status == Status::kStaleRevision);
  CHECK(entries[3].status == Status::kNotSeated);
  for (const auto& entry : entries) {
    CHECK(entry.decision_tick == simulation::TickSequence::create(1));
    CHECK(entry.revision == 1);
  }
  const auto snapshot = game.snapshot();
  CHECK(snapshot.match().movement().current == fixture::kSecondPair);
  CHECK(snapshot.match().movement().defaults == simulation::MovementTuning::defaults());
  CHECK(snapshot.match().movement().effective_tick == simulation::TickSequence::create(1));
}

TEST_CASE("movement tuning preserves submission deduplication rather than sorting correlation IDs",
          "[unit][simulation][movement_tuning]") {
  auto game = fixture::game();
  const auto result = game.step(simulation::FixedDelta::canonical(),
                                fixture::batch({fixture::command(1, 900, 0),
                                                fixture::command(1, 2, 0, fixture::kSecondPair)}));
  REQUIRE(result.entries().size() == 1);
  CHECK(result.entries()[0].tuning_request_id == 2);
  const auto snapshot = game.snapshot();
  CHECK(snapshot.match().movement().current == fixture::kSecondPair);
}

TEST_CASE("movement tuning admits seated controllers in every phase including equal-value updates",
          "[unit][simulation][movement_tuning]") {
  for (const auto phase : {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
                           simulation::MatchPhase::kRunning, simulation::MatchPhase::kEnded}) {
    auto game = fixture::game(phase);
    const auto result = game.step(
        simulation::FixedDelta::canonical(),
        fixture::batch({fixture::command(3, 1, 0, simulation::MovementTuning::defaults())}));
    REQUIRE(result.entries().size() == 1);
    CHECK(result.entries()[0].status == Status::kApplied);
    const auto snapshot = game.snapshot();
    CHECK(snapshot.match().movement().revision == 1);
    CHECK(snapshot.match().movement().effective_tick == simulation::TickSequence::create(1));
  }
}

TEST_CASE(
    "movement tuning checks membership before same-batch Join and retains acceptance before Leave",
    "[unit][simulation][movement_tuning]") {
  auto game = fixture::game();
  const auto result = game.step(
      simulation::FixedDelta::canonical(),
      fixture::batch({simulation::JoinCommand{fixture::controller(4), std::nullopt},
                      fixture::command(4, 1, 0), simulation::LeaveCommand{fixture::controller(1)},
                      fixture::command(1, 1, 0),
                      simulation::StartMatchCommand{fixture::controller(1)}}));
  REQUIRE(result.entries().size() == 2);
  CHECK(result.entries()[0].status == Status::kApplied);
  CHECK(result.entries()[1].status == Status::kNotSeated);
  const auto snapshot = game.snapshot();
  CHECK(snapshot.match().movement().current == fixture::kFirstPair);
  CHECK(std::holds_alternative<simulation::EmptySeat>(snapshot.match().seats().seats()[0]));
  CHECK(snapshot.match().seats().start_requested());
}

TEST_CASE(
    "movement tuning refuses revision exhaustion without wrapping or changing its effective tick",
    "[unit][simulation][movement_tuning]") {
  const auto maximum = simulation::kMaximumProtocolSafeInteger;
  auto game = fixture::game(simulation::MatchPhase::kLobby, maximum);
  const auto result = game.step(
      simulation::FixedDelta::canonical(),
      fixture::batch({fixture::command(1, 1, maximum), fixture::command(2, 1, maximum - 1)}));
  REQUIRE(result.entries().size() == 2);
  CHECK(result.entries()[0].status == Status::kRevisionExhausted);
  CHECK(result.entries()[1].status == Status::kStaleRevision);
  const auto snapshot = game.snapshot();
  CHECK(snapshot.match().movement().revision == maximum);
  CHECK(snapshot.match().movement().current == simulation::MovementTuning::defaults());
  CHECK(snapshot.match().movement().effective_tick == simulation::TickSequence::zero());
}

TEST_CASE(
    "movement tuning decisions and state do not escape a failed tick and clean continuation agrees",
    "[unit][simulation][movement_tuning]") {
  auto game = fixture::game(simulation::MatchPhase::kLobby, 0, true);
  auto control = fixture::game(simulation::MatchPhase::kLobby, 0, true);
  const auto before = game.snapshot();
  CHECK_THROWS_AS(game.step(simulation::FixedDelta::canonical(),
                            fixture::batch({fixture::command(1, 1, 0, fixture::kRejectedPair)})),
                  simulation::SimulationValidationError);
  CHECK(game.snapshot() == before);
  const auto retry = fixture::batch({fixture::command(1, 2, 0)});
  const auto recovered = game.step(simulation::FixedDelta::canonical(), retry);
  const auto clean = control.step(simulation::FixedDelta::canonical(), retry);
  REQUIRE(recovered.entries().size() == 1);
  REQUIRE(clean.entries().size() == 1);
  CHECK(recovered.entries()[0] == clean.entries()[0]);
  CHECK(game.snapshot() == control.snapshot());
}

TEST_CASE(
    "movement tuning command rejects unsafe correlation and revision values at batch construction",
    "[unit][simulation][movement_tuning][validation]") {
  for (const auto request : {std::uint64_t{0}, simulation::kMaximumProtocolSafeInteger + 1}) {
    CHECK_THROWS_AS(fixture::batch({fixture::command(1, request, 0)}),
                    simulation::SimulationValidationError);
  }
  CHECK_THROWS_AS(
      fixture::batch({fixture::command(1, 1, simulation::kMaximumProtocolSafeInteger + 1)}),
      simulation::SimulationValidationError);
  const auto valid = fixture::batch({fixture::command(1, simulation::kMaximumProtocolSafeInteger,
                                                      simulation::kMaximumProtocolSafeInteger)});
  CHECK(valid.commands().size() == 1);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<simulation::MovementTuningDecisions>);
  STATIC_REQUIRE(std::is_nothrow_move_assignable_v<simulation::MovementTuningDecisions>);
}

TEST_CASE("movement tuning private intent is removed by the canonical component publication owner",
          "[unit][simulation][movement_tuning][snapshot]") {
  simulation::Controllable source{fixture::controller(1)};
  source.normalized_thrust_intent = simulation::Vector2::create(0.6, 0.8);
  source.commands_this_tick.push_back(fixture::command(1, 1, 0));
  const auto published = simulation::published_component(source);
  CHECK(published.controller_id == source.controller_id);
  CHECK(published.commands_this_tick.empty());
  CHECK_FALSE(published.normalized_thrust_intent.has_value());
  CHECK(source.normalized_thrust_intent.has_value());
}
