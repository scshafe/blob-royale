#include "fixtures/profiled_bot_reconciliation_fixture.hpp"

#include "commands/join_command.hpp"
#include "commands/leave_command.hpp"
#include "commands/seat_npc_command.hpp"
#include "match_startup_validation.hpp"
#include "room.hpp"
#include "royale/royale_mode.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <variant>
#include <vector>

namespace blob_royale::application {
namespace fixture = profiled_bot_fixture;

TEST_CASE("profiled reconciliation submits its full declaration and keeps pending identity stable",
          "[unit][application][bots][npc_profile]") {
  fixture::Harness harness;
  const auto declaration = fixture::declaration();
  const auto seats = fixture::roster(declaration);
  harness.observe(seats);
  const auto commands = harness.mailbox.drain();
  REQUIRE(commands.size() == 1);
  const auto& join = std::get<simulation::JoinCommand>(commands.front());
  CHECK(join.controller == simulation::ControllerId::create(fixture::kFirstControllerId));
  CHECK(join.seat_index == 1);
  CHECK(join.expected_npc == declaration);
  CHECK(harness.host.size() == 1);
  CHECK(harness.directory.size() == 1);
  harness.observe(seats);
  CHECK(harness.mailbox.drain().empty());
  CHECK(harness.event_count("controllers.bot_created") == 1);
  const auto created = harness.logs.find_event("controllers.bot_created");
  REQUIRE(created.has_value());
  REQUIRE(created->detail.has_value());
  CHECK(created->detail->find("profile_name=steady") != std::string::npos);
}

TEST_CASE("profile replacement retires an old pending or seated bot without the join timeout",
          "[unit][application][bots][npc_profile]") {
  for (const bool initially_seated : {false, true}) {
    CAPTURE(initially_seated);
    fixture::Harness harness;
    const auto old_controller = simulation::ControllerId::create(fixture::kFirstControllerId);
    harness.observe(fixture::roster(fixture::declaration()));
    auto pending = harness.mailbox.drain();
    REQUIRE(pending.size() == 1);
    if (initially_seated) {
      harness.observe(fixture::roster(fixture::declaration(), old_controller));
    }
    const auto replacement = fixture::declaration(fixture::kQuick);
    const auto replacement_roster = fixture::roster(replacement);
    harness.observe(replacement_roster);
    CHECK_FALSE(harness.host.contains(old_controller));
    CHECK_FALSE(harness.directory.contains(old_controller));
    CHECK(harness.host.size() == 1);
    CHECK(harness.reconciler.hosted_bot_count() == 1);
    CHECK(harness.event_count("controllers.bot_created") == 2);
    CHECK(harness.event_count("controllers.bot_retired") == 1);
    auto replacement_commands = harness.mailbox.drain();
    REQUIRE(replacement_commands.size() == 2);
    CHECK(std::get<simulation::LeaveCommand>(replacement_commands[0]).controller == old_controller);
    const auto& replacement_join = std::get<simulation::JoinCommand>(replacement_commands[1]);
    CHECK(replacement_join.expected_npc == replacement);
    CHECK(replacement_join.controller != old_controller);

    // Even a join drained before retirement cannot fill the newly committed declaration.
    auto game = fixture::game(replacement_roster);
    fixture::step(game, std::move(pending));
    const auto after_stale = game.snapshot();
    CHECK(after_stale.match().seats() == replacement_roster);
    fixture::step(game, std::move(replacement_commands));
    const auto after_replacement = game.snapshot();
    const auto& seated =
        std::get<simulation::NpcSeat>(after_replacement.match().seats().seats()[1]);
    CHECK(seated.declaration() == replacement);
    CHECK(seated.controller == simulation::ControllerId::create(fixture::kFirstControllerId + 1));
  }
}

TEST_CASE("a pending profiled bot is retired immediately on clear resize or human replacement",
          "[unit][application][bots][npc_profile]") {
  auto human = simulation::SeatRoster::of_size(2);
  human.assign_seat(
      1, simulation::Seat{simulation::ControllerSeat{simulation::ControllerId::create(7)}});
  for (const auto& replacement :
       {simulation::SeatRoster::of_size(2), simulation::SeatRoster::of_size(1), human}) {
    fixture::Harness harness;
    harness.observe(fixture::roster(fixture::declaration()));
    harness.observe(replacement);
    CHECK(harness.host.size() == 0);
    CHECK(harness.directory.size() == 0);
    CHECK(harness.reconciler.hosted_bot_count() == 0);
    CHECK(harness.event_count("controllers.bot_retired") == 1);
  }
}

TEST_CASE("failed profile creation is cached by full declaration and removal clears that cache",
          "[unit][application][bots][npc_profile]") {
  fixture::Harness harness;
  const auto unknown = fixture::roster(fixture::declaration(fixture::kUnknown));
  harness.observe(unknown);
  harness.observe(unknown);
  CHECK(harness.event_count("controllers.bot_creation_failed") == 1);
  CHECK(harness.host.size() == 0);
  CHECK(harness.directory.size() == 0);
  CHECK(harness.mailbox.drain().empty());
  const auto failure = harness.logs.find_event("controllers.bot_creation_failed");
  REQUIRE(failure.has_value());
  REQUIRE(failure->detail.has_value());
  CHECK(failure->detail->find("APPLICATION.MATCH.BOT_PROFILE_UNKNOWN") != std::string::npos);

  harness.observe(simulation::SeatRoster::of_size(1));
  harness.observe(unknown);
  CHECK(harness.event_count("controllers.bot_creation_failed") == 2);
  // A different profile on the same kind/index is new work, with no empty intermediate snapshot.
  harness.observe(fixture::roster(fixture::declaration(fixture::kQuick)));
  CHECK(harness.host.size() == 1);
  CHECK(harness.event_count("controllers.bot_created") == 1);
}

TEST_CASE("abandonment retires profiled bots and reseating preserves their declaration",
          "[unit][application][bots][npc_profile]") {
  fixture::Harness harness;
  const auto seats = fixture::roster(fixture::declaration());
  harness.observe(seats);
  static_cast<void>(harness.mailbox.drain());
  harness.observe(seats, true);
  harness.observe(seats, true);
  CHECK(harness.host.size() == 0);
  CHECK(harness.event_count("controllers.bot_retired") == 1);
  static_cast<void>(harness.mailbox.drain());
  harness.observe(seats);
  const auto commands = harness.mailbox.drain();
  REQUIRE(commands.size() == 1);
  CHECK(std::get<simulation::JoinCommand>(commands.front()).expected_npc == fixture::declaration());
  CHECK(harness.event_count("controllers.bot_created") == 2);
}

TEST_CASE("registry profile refusal closes the newly allocated session and caches the failure",
          "[unit][application][bots][npc_profile]") {
  const simulation::NpcDeclaration bare_tactical{simulation::SeatKindName::create("tactical"),
                                                 std::nullopt};
  for (const auto& invalid : {fixture::declaration(fixture::kSteady, "wanderer"), bare_tactical}) {
    fixture::Harness harness;
    harness.observe(fixture::roster(invalid));
    harness.observe(fixture::roster(invalid));
    CHECK(harness.host.size() == 0);
    CHECK(harness.directory.size() == 0);
    CHECK(harness.reconciler.hosted_bot_count() == 0);
    CHECK(harness.event_count("controllers.bot_creation_failed") == 1);
    const auto commands = harness.mailbox.drain();
    REQUIRE(commands.size() == 1);
    CHECK(std::get<simulation::LeaveCommand>(commands.front()).controller ==
          simulation::ControllerId::create(fixture::kFirstControllerId));
    harness.observe(fixture::roster(fixture::declaration()));
    CHECK(harness.host.size() == 1);
    CHECK(harness.directory.size() == 1);
    CHECK(harness.event_count("controllers.bot_created") == 1);
  }
}

TEST_CASE("unchanged profile joins expire at their budget while observed seated ownership does not",
          "[unit][application][bots][npc_profile]") {
  const auto seats = fixture::roster(fixture::declaration());
  for (const bool seated : {false, true}) {
    CAPTURE(seated);
    fixture::Harness harness;
    harness.observe(seats);
    static_cast<void>(harness.mailbox.drain());
    const auto observed =
        seated ? fixture::roster(fixture::declaration(),
                                 simulation::ControllerId::create(fixture::kFirstControllerId))
               : seats;
    harness.observe_at(observed, SeatBotReconciler::kJoinObservationBudgetTicks - 1);
    CHECK(harness.event_count("controllers.bot_retired") == 0);
    CHECK(harness.event_count("controllers.bot_created") == 1);
    harness.observe_at(observed, SeatBotReconciler::kJoinObservationBudgetTicks);
    CHECK(harness.event_count("controllers.bot_retired") == (seated ? 0 : 1));
    CHECK(harness.event_count("controllers.bot_created") == (seated ? 1 : 2));
    CHECK(harness.host.size() == 1);
  }
}

TEST_CASE("startup roster retains profile identities in authored order",
          "[unit][application][startup][npc_profile]") {
  const auto entries =
      MatchConfiguration::parse_bot_roster("tactical@steady:1,wanderer:1,tactical@quick:1");
  const auto seats = initial_seat_roster_for(*gameplay::RoyaleMode::create(), 4, entries);
  CHECK(std::get<simulation::NpcSeat>(seats.seats()[0]).declaration() == fixture::declaration());
  CHECK_FALSE(std::get<simulation::NpcSeat>(seats.seats()[1]).profile_name.has_value());
  CHECK(std::get<simulation::NpcSeat>(seats.seats()[2]).declaration() ==
        fixture::declaration(fixture::kQuick));
  CHECK(std::holds_alternative<simulation::EmptySeat>(seats.seats()[3]));
}

TEST_CASE("Room shares one exact profile catalogue between welcome and runtime admission",
          "[unit][application][room][npc_profile]") {
  const auto match = MatchConfiguration::create(
      "royale", "profiled_reconciliation", "maps", fixture::kRawMatchSeed, 2,
      MatchConfiguration::parse_bot_roster("tactical@steady:1"));
  auto game = fixture::game(fixture::roster(fixture::declaration()));
  const auto accepted = game.accepted_command_kinds();
  test_support::StructuredLogCapture logs;
  Room room(fixture::kLobbyId, std::move(game), accepted, match, fixture::profiles(),
            fixture::catalogue(), logs.logger);
  CHECK(room.seed() == fixture::kLegacyRoomSeed);
  REQUIRE(room.reconciler() != nullptr);
  CHECK(room.match_session().npc_catalogue() == fixture::catalogue());
  auto& sink = room.runtime().command_sink();
  const auto person = sink.open_session("session", "Profile chooser");
  for (const auto& declaration : room.match_session().npc_profiles()) {
    CHECK(runtime::command_submission_accepted(
        sink.submit(person, simulation::SeatNpcCommand{person, 0, declaration.kind,
                                                       declaration.profile_name})));
  }
  const auto unknown = fixture::declaration(fixture::kUnknown);
  CHECK(sink.submit(person,
                    simulation::SeatNpcCommand{person, 0, unknown.kind, unknown.profile_name}) ==
        runtime::CommandSubmissionResult::kRejectedNpcDeclarationUnknown);
}

} // namespace blob_royale::application
