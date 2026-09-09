#include "bot_reconciliation.hpp"

#include "command_registry.hpp"
#include "command_submission_result.hpp"
#include "commands/clear_seat_command.hpp"
#include "commands/join_command.hpp"
#include "commands/seat_npc_command.hpp"
#include "controller_directory.hpp"
#include "controller_host.hpp"
#include "controller_id.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "seat_roster.hpp"
#include "simulation_config.hpp"
#include "simulation_runtime.hpp"
#include "snapshot_publication.hpp"
#include "structured_log_capture.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace blob_royale::application {
namespace {

using namespace std::chrono_literals;
using LogCapture = test_support::StructuredLogCapture;

constexpr std::uint64_t kMatchSeed = 7;
constexpr std::size_t kSpawnPointCount = 4;
constexpr auto kDeadline = 5s;

[[nodiscard]] simulation::MapDefinition lobby_map() {
  std::vector<simulation::MapDefinition::Marker> markers;
  markers.reserve(kSpawnPointCount);
  for (std::size_t index = 0; index < kSpawnPointCount; ++index) {
    markers.push_back(simulation::MapDefinition::Marker::spawn(
        simulation::Vector2::create(100.0 * static_cast<double>(index + 1), 250.0)));
  }
  return simulation::MapDefinition::create("reconciler_map",
                                           simulation::ArenaBounds::create(500.0, 500.0), {},
                                           std::move(markers), simulation::MapMetadata::none());
}

[[nodiscard]] simulation::Seat declared(const std::string_view kind) {
  return simulation::Seat{
      simulation::NpcSeat{simulation::SeatKindName::create(kind), std::nullopt}};
}

[[nodiscard]] simulation::Seat bot_at(const std::string_view kind, const std::uint64_t controller) {
  return simulation::Seat{simulation::NpcSeat{simulation::SeatKindName::create(kind),
                                              simulation::ControllerId::create(controller)}};
}

[[nodiscard]] simulation::Seat person_at(const simulation::ControllerId controller) {
  return simulation::Seat{simulation::ControllerSeat{controller}};
}

// A world with this lobby and nothing else, stepped with no mode, so the phase stays `lobby` and
// the seats are the whole observable.
[[nodiscard]] simulation::GameSimulation lobby_simulation(simulation::SeatRoster roster) {
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_match().seats = std::move(roster);
  return simulation::GameSimulation::create(
      simulation::SimulationConfig::create(500.0, 500.0, 10.0, 400, 8, 8), std::move(world),
      simulation::GameSimulationSetup::engine_defaults().with_map(lobby_map()));
}

// A live runtime with the host and the reconciler production builds on it, polled by the test in
// place of the control loop.
class ReconcilerHarness final {
public:
  explicit ReconcilerHarness(simulation::SeatRoster roster)
      : runtime_(lobby_simulation(std::move(roster))),
        host_(runtime_.snapshot_publication(), runtime_.command_sink()),
        reconciler_(runtime_.command_sink(), host_, kMatchSeed, log_capture_.logger) {
    runtime_.start();
    const auto deadline = std::chrono::steady_clock::now() + kDeadline;
    while (!runtime_.snapshot_publication().is_ready()) {
      if (std::chrono::steady_clock::now() >= deadline) {
        throw std::runtime_error{"the test runtime never published"};
      }
      std::this_thread::sleep_for(1ms);
    }
  }

  ReconcilerHarness(const ReconcilerHarness&) = delete;
  ReconcilerHarness(ReconcilerHarness&&) = delete;
  ReconcilerHarness& operator=(const ReconcilerHarness&) = delete;
  ReconcilerHarness& operator=(ReconcilerHarness&&) = delete;
  ~ReconcilerHarness() { runtime_.stop(); }

  // One control poll: reconcile against the latest committed world.
  void reconcile() { reconciler_.reconcile(*runtime_.snapshot_publication().latest()); }

  // Polls until the predicate holds or the deadline passes, and reports which.
  template <typename Predicate> [[nodiscard]] bool reconcile_until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + kDeadline;
    while (std::chrono::steady_clock::now() < deadline) {
      reconcile();
      if (predicate()) {
        return true;
      }
      std::this_thread::sleep_for(2ms);
    }
    return false;
  }

  // Waits for the tick alone, without a poll in between.
  template <typename Predicate> [[nodiscard]] bool wait_until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + kDeadline;
    while (std::chrono::steady_clock::now() < deadline) {
      if (predicate()) {
        return true;
      }
      std::this_thread::sleep_for(2ms);
    }
    return false;
  }

  // The committed roster, by value.
  [[nodiscard]] simulation::SeatRoster seats() const {
    const std::shared_ptr<const simulation::WorldSnapshot> latest =
        runtime_.snapshot_publication().latest();
    return latest->match().seats();
  }

  // One committed seat, by value: `seats()` on the temporary roster is deleted on purpose.
  [[nodiscard]] simulation::Seat seat_at(const std::size_t index) const {
    const simulation::SeatRoster roster = seats();
    return roster.seats()[index];
  }

  [[nodiscard]] std::size_t count_events(const std::string_view event) const {
    std::size_t count = 0;
    for (const test_support::CapturedStructuredLogEvent& record : log_capture_.events()) {
      if (record.event == event) {
        ++count;
      }
    }
    return count;
  }

  [[nodiscard]] runtime::SimulationRuntime& runtime() noexcept { return runtime_; }
  [[nodiscard]] controllers::ControllerHost& host() noexcept { return host_; }
  [[nodiscard]] SeatBotReconciler& reconciler() noexcept { return reconciler_; }
  [[nodiscard]] const LogCapture& log_capture() const noexcept { return log_capture_; }

private:
  runtime::SimulationRuntime runtime_;
  LogCapture log_capture_;
  controllers::ControllerHost host_;
  SeatBotReconciler reconciler_;
};

[[nodiscard]] bool detail_mentions(const test_support::CapturedStructuredLogEvent& record,
                                   const std::string_view fragment) {
  return record.detail.has_value() && record.detail->find(fragment) != std::string::npos;
}

} // namespace

TEST_CASE("SeatBotReconciler creates one bot per declared seat and joins each to its own seat",
          "[unit][application][bots][lobby]") {
  simulation::SeatRoster roster = simulation::SeatRoster::of_size(3);
  roster.assign_seat(0, declared("wanderer"));
  roster.assign_seat(2, declared("chaser"));
  ReconcilerHarness harness{std::move(roster)};
  const std::uint64_t first_bot = harness.runtime().command_sink().next_controller_id();

  // One poll builds both: a session each, a hosted controller each, a join each.
  harness.reconcile();
  CHECK(harness.reconciler().hosted_bot_count() == 2);
  CHECK(harness.host().size() == 2);
  CHECK(harness.runtime().controller_directory().size() == 2);
  CHECK(harness.count_events("controllers.bot_created") == 2);

  REQUIRE(harness.reconcile_until([&harness, first_bot] {
    const simulation::SeatRoster seats = harness.seats();
    return seats.seats()[0] == bot_at("wanderer", first_bot) &&
           seats.seats()[2] == bot_at("chaser", first_bot + 1);
  }));
  CHECK(harness.seat_at(1) == simulation::Seat{simulation::EmptySeat{}});

  // Steady state: every further poll finds the seats held and builds and retires nothing.
  for (int poll = 0; poll < 5; ++poll) {
    harness.reconcile();
  }
  CHECK(harness.reconciler().hosted_bot_count() == 2);
  CHECK(harness.host().size() == 2);
  CHECK(harness.runtime().controller_directory().size() == 2);
  CHECK(harness.count_events("controllers.bot_created") == 2);
  CHECK(harness.count_events("controllers.bot_retired") == 0);
}

TEST_CASE("SeatBotReconciler retires a bot whose seat is cleared and builds one for a seat "
          "declared again",
          "[unit][application][bots][lobby]") {
  simulation::SeatRoster roster = simulation::SeatRoster::of_size(2);
  roster.assign_seat(0, declared("wanderer"));
  ReconcilerHarness harness{std::move(roster)};
  const std::uint64_t bot = harness.runtime().command_sink().next_controller_id();
  REQUIRE(harness.reconcile_until(
      [&harness, bot] { return harness.seat_at(0) == bot_at("wanderer", bot); }));
  // One more poll over a world that holds the bot, so the reconciler has seen its join land and
  // is not still giving that join its budget when the seat goes.
  harness.reconcile();

  // A person clears the seat. The bot then sits nowhere, and the next poll closes it: its session
  // leaves the directory, its controller leaves the host, and the leave its close enqueued is what
  // the tick applies.
  runtime::CommandSink& sink = harness.runtime().command_sink();
  const simulation::ControllerId person = sink.open_session("session", "Ada");
  REQUIRE(sink.submit(person, simulation::Command{simulation::ClearSeatCommand{person, 0}}) ==
          runtime::CommandSubmissionResult::kAccepted);
  REQUIRE(harness.wait_until(
      [&harness] { return harness.seat_at(0) == simulation::Seat{simulation::EmptySeat{}}; }));
  harness.reconcile();
  CHECK(harness.reconciler().hosted_bot_count() == 0);
  CHECK(harness.host().size() == 0);
  CHECK_FALSE(
      harness.runtime().controller_directory().contains(simulation::ControllerId::create(bot)));
  CHECK(harness.runtime().controller_directory().size() == 1);
  REQUIRE(harness.count_events("controllers.bot_retired") == 1);
  CHECK(detail_mentions(*harness.log_capture().find_event("controllers.bot_retired"),
                        "reason=seat_lost"));

  // The person declares a different kind into the same seat, and a new bot with a new identity
  // takes it.
  REQUIRE(sink.submit(person, simulation::Command{simulation::SeatNpcCommand{
                                  person, 0, simulation::SeatKindName::create("chaser")}}) ==
          runtime::CommandSubmissionResult::kAccepted);
  REQUIRE(harness.wait_until([&harness] { return harness.seat_at(0) == declared("chaser"); }));
  const std::uint64_t replacement = sink.next_controller_id();
  REQUIRE(harness.reconcile_until(
      [&harness, replacement] { return harness.seat_at(0) == bot_at("chaser", replacement); }));
  CHECK(replacement != bot);
  CHECK(harness.reconciler().hosted_bot_count() == 1);
  CHECK(harness.host().size() == 1);
  CHECK(harness.count_events("controllers.bot_created") == 2);
}

TEST_CASE("SeatBotReconciler retires a bot a person displaced and builds nothing for the person",
          "[unit][application][bots][lobby]") {
  // A one-seat lobby whose only seat is declared: the person's join finds no empty seat and takes
  // the bot's, whether the bot's own join has landed yet or not.
  simulation::SeatRoster roster = simulation::SeatRoster::of_size(1);
  roster.assign_seat(0, declared("wanderer"));
  ReconcilerHarness harness{std::move(roster)};
  const std::uint64_t bot = harness.runtime().command_sink().next_controller_id();
  harness.reconcile();
  REQUIRE(harness.reconciler().hosted_bot_count() == 1);

  runtime::CommandSink& sink = harness.runtime().command_sink();
  const simulation::ControllerId person = sink.open_session("session", "Ada");
  REQUIRE(sink.submit(person, simulation::Command{simulation::JoinCommand{person, std::nullopt}}) ==
          runtime::CommandSubmissionResult::kAccepted);
  REQUIRE(
      harness.wait_until([&harness, person] { return harness.seat_at(0) == person_at(person); }));

  // Within its observation budget of one committed second the bot is judged to sit nowhere.
  REQUIRE(
      harness.reconcile_until([&harness] { return harness.reconciler().hosted_bot_count() == 0; }));
  CHECK(harness.host().size() == 0);
  CHECK_FALSE(
      harness.runtime().controller_directory().contains(simulation::ControllerId::create(bot)));
  CHECK(harness.runtime().controller_directory().size() == 1);
  CHECK(harness.count_events("controllers.bot_retired") == 1);
  CHECK(harness.count_events("controllers.bot_created") == 1);
  CHECK(harness.seat_at(0) == person_at(person));
}

TEST_CASE("SeatBotReconciler records a seat whose kind it cannot build and retries it only once "
          "the declaration changes",
          "[unit][application][bots][lobby]") {
  // `stalker` satisfies the kind-name grammar and names no registered controller, which the tick
  // cannot know: the seat is declared, the session for it opens, and the registry refuses.
  simulation::SeatRoster roster = simulation::SeatRoster::of_size(1);
  roster.assign_seat(0, declared("stalker"));
  ReconcilerHarness harness{std::move(roster)};
  harness.reconcile();
  harness.reconcile();
  harness.reconcile();
  CHECK(harness.count_events("controllers.bot_creation_failed") == 1);
  CHECK(harness.reconciler().hosted_bot_count() == 0);
  CHECK(harness.host().size() == 0);
  // The session that was opened for the seat was closed again: nothing sits in the directory with
  // nobody behind it.
  CHECK(harness.runtime().controller_directory().size() == 0);
  CHECK(harness.seat_at(0) == declared("stalker"));

  // A person replaces the declaration, and that seat is tried again.
  runtime::CommandSink& sink = harness.runtime().command_sink();
  const simulation::ControllerId person = sink.open_session("session", "Ada");
  REQUIRE(sink.submit(person, simulation::Command{simulation::ClearSeatCommand{person, 0}}) ==
          runtime::CommandSubmissionResult::kAccepted);
  REQUIRE(harness.wait_until(
      [&harness] { return harness.seat_at(0) == simulation::Seat{simulation::EmptySeat{}}; }));
  REQUIRE(sink.submit(person, simulation::Command{simulation::SeatNpcCommand{
                                  person, 0, simulation::SeatKindName::create("wanderer")}}) ==
          runtime::CommandSubmissionResult::kAccepted);
  REQUIRE(harness.wait_until([&harness] { return harness.seat_at(0) == declared("wanderer"); }));
  const std::uint64_t bot = sink.next_controller_id();
  REQUIRE(harness.reconcile_until(
      [&harness, bot] { return harness.seat_at(0) == bot_at("wanderer", bot); }));
  CHECK(harness.count_events("controllers.bot_creation_failed") == 1);
  CHECK(harness.count_events("controllers.bot_created") == 1);
  CHECK(harness.reconciler().hosted_bot_count() == 1);
}

} // namespace blob_royale::application
