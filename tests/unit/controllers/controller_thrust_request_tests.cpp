#include "fixtures/controller_thrust_request_fixture.hpp"

#include "command_mailbox.hpp"
#include "command_sink.hpp"
#include "command_submission_result.hpp"
#include "controller_host.hpp"
#include "entity_id_allocator.hpp"
#include "scripted_replay_controller.hpp"
#include "shared/thrust_steering_system.hpp"
#include "snapshot_publication.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace controllers = blob_royale::controllers;
namespace gameplay = blob_royale::gameplay;
namespace runtime = blob_royale::runtime;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;
namespace fixture = testing::thrust_request_fixture;

TEST_CASE("Shared thrust authoring copies direction bits and exact observed optional generation",
          "[unit][controllers][input_generation]") {
  const fixture::ThrustRequestController controller;
  for (const auto token :
       {std::optional<simulation::TickSequence>{}, std::optional{fixture::generation()}}) {
    const auto observed = fixture::observation(fixture::world(token));
    const auto commands = controller.request_thrust(observed, fixture::direction());
    REQUIRE(commands.size() == 1);
    const auto& thrust = std::get<simulation::ThrustCommand>(commands.front());
    CHECK(thrust.entity == simulation::EntityId::create(fixture::kController));
    CHECK(thrust.input_generation == token);
    CHECK(std::bit_cast<std::uint64_t>(thrust.direction.x()) ==
          std::bit_cast<std::uint64_t>(fixture::direction().x()));
    CHECK(std::bit_cast<std::uint64_t>(thrust.direction.y()) ==
          std::bit_cast<std::uint64_t>(fixture::direction().y()));
  }
}

TEST_CASE("Shared thrust authoring requires this controller's dynamic published body",
          "[unit][controllers][input_generation]") {
  const fixture::ThrustRequestController controller;
  for (const auto body : {fixture::BodyState::kAbsent, fixture::BodyState::kStatic}) {
    const auto observed =
        fixture::observation(fixture::world(fixture::generation(), std::nullopt, body));
    CHECK(controller.request_thrust(observed, fixture::direction()).empty());
  }
  const auto foreign =
      fixture::observation(fixture::world(), fixture::kActivation, fixture::kOtherController);
  CHECK(controller.request_thrust(foreign, fixture::direction()).empty());
  const auto unseated =
      fixture::observation(fixture::world(), fixture::kActivation, fixture::kUnseatedController);
  CHECK(controller.request_thrust(unseated, fixture::direction()).empty());
}

TEST_CASE("All four active bots echo the public generation and suppress the observed stun window",
          "[unit][controllers][stun][input_generation]") {
  const auto fresh = fixture::observation(fixture::world());
  const auto never_invalidated = fixture::observation(fixture::world(std::nullopt));
  const auto locked =
      fixture::observation(fixture::world(fixture::generation(), fixture::active_stun()));
  const auto expired = fixture::observation(
      fixture::world(fixture::generation(), fixture::active_stun()), fixture::kExpiry);
  for (const auto kind : fixture::kActiveKinds) {
    CAPTURE(kind);
    const auto commands = fixture::active_bot(kind)->decide(fresh);
    REQUIRE(commands.size() == 1);
    CHECK(std::get<simulation::ThrustCommand>(commands.front()).input_generation ==
          fixture::generation());
    const auto initial_commands = fixture::active_bot(kind)->decide(never_invalidated);
    REQUIRE(initial_commands.size() == 1);
    const auto& initial_thrust = std::get<simulation::ThrustCommand>(initial_commands.front());
    CHECK_FALSE(initial_thrust.input_generation.has_value());
    CHECK(initial_thrust.direction ==
          std::get<simulation::ThrustCommand>(commands.front()).direction);
    CHECK(fixture::active_bot(kind)->decide(locked).empty());
    const auto after_expiry = fixture::active_bot(kind)->decide(expired);
    REQUIRE(after_expiry.size() == 1);
    CHECK(std::get<simulation::ThrustCommand>(after_expiry.front()).input_generation ==
          fixture::generation());
  }
}

TEST_CASE("Stun suppression preserves the active bots' random draws and reaction scheduling",
          "[unit][controllers][stun][input_generation]") {
  const auto clear = fixture::observation(fixture::world());
  const auto locked =
      fixture::observation(fixture::world(fixture::generation(), fixture::active_stun()));
  const auto expired = fixture::observation(fixture::world(), fixture::kExpiry);
  for (const auto kind : fixture::kActiveKinds) {
    CAPTURE(kind);
    const auto uninterrupted = fixture::active_bot(kind);
    const auto suppressed = fixture::active_bot(kind);
    static_cast<void>(uninterrupted->decide(clear));
    CHECK(suppressed->decide(locked).empty());
    for (std::uint64_t pass = 0; pass < fixture::kReactionComparisonPasses; ++pass) {
      CHECK(suppressed->decide(expired) == uninterrupted->decide(expired));
    }
  }
}

TEST_CASE("Scripted replay preserves omitted stale and current typed tokens despite observed stun",
          "[unit][controllers][scripted_replay][input_generation]") {
  const auto observed =
      fixture::observation(fixture::world(fixture::generation(), fixture::active_stun()));
  const std::vector<controllers::ScriptedReplayController::Step> log{
      {fixture::literal(std::nullopt)},
      {fixture::literal(simulation::TickSequence::create(fixture::kActivation - 1))},
      {fixture::literal(fixture::generation(), fixture::kController, true)}};
  const auto replay = controllers::ScriptedReplayController::create(
      simulation::ControllerId::create(fixture::kController), log);
  for (const auto& step : log) {
    CHECK(replay->decide(observed) == step);
  }
}

TEST_CASE(
    "Hosted replay and session commands share authoritative stun generation and release admission",
    "[unit][controllers][symmetry][scripted_replay][input_generation]") {
  const auto observed = fixture::observation(fixture::world());
  const runtime::SnapshotPublication publication(observed.snapshot());
  const auto steering = gameplay::ThrustSteeringSystem::create();
  for (const bool locked : {false, true}) {
    CAPTURE(locked);
    const testing::TickHarness harness(
        simulation::TickSequence::create(locked ? fixture::kActivation : fixture::kExpiry));
    for (const auto token :
         {std::optional<simulation::TickSequence>{},
          std::optional{simulation::TickSequence::create(fixture::kActivation - 1)},
          std::optional{fixture::generation()}}) {
      for (const bool release : {false, true}) {
        runtime::CommandMailbox mailbox(simulation::CommandKindMask::all());
        runtime::ControllerDirectory directory;
        auto allocator = runtime::EntityIdAllocator::create(
            simulation::EntityId::create(fixture::kHillEntity + 1));
        runtime::CommandSink sink(mailbox, directory, allocator, fixture::kController);
        const auto bot_controller = sink.open_session("scripted_replay", "literal bot");
        const auto session_controller = sink.open_session("session", "literal session");
        controllers::ControllerHost host(publication, sink);
        const auto bot_command = fixture::literal(token, fixture::kController, release);
        const auto session_command = fixture::literal(token, fixture::kOtherController, release);
        host.add(controllers::ScriptedReplayController::create(bot_controller, {{bot_command}}));
        const auto pass = host.decide_once();
        REQUIRE(pass.accepted_command_count == 1);
        REQUIRE(sink.submit(session_controller, session_command) ==
                runtime::CommandSubmissionResult::kAccepted);
        const auto drained = mailbox.drain();
        const std::vector<simulation::Command> expected{bot_command, session_command};
        REQUIRE(drained == expected);

        auto world = fixture::world();
        if (locked) {
          for (const auto entity : {fixture::kController, fixture::kOtherController}) {
            world.mutable_store<simulation::Stun>().insert_or_assign(
                simulation::EntityId::create(entity), fixture::active_stun());
          }
        }
        const auto held = simulation::Vector2::create(0.0, 1.0);
        for (const auto& command : drained) {
          const auto& thrust = std::get<simulation::ThrustCommand>(command);
          auto* controllable =
              world.mutable_store<simulation::Controllable>().mutable_find(thrust.entity);
          controllable->commands_this_tick = {command};
          controllable->normalized_thrust_intent = held;
        }
        steering->apply(world, harness.context());
        const auto entity = simulation::EntityId::create(fixture::kController);
        const auto other = simulation::EntityId::create(fixture::kOtherController);
        const auto& bot_link = *world.store<simulation::Controllable>().find(entity);
        const auto& session_link = *world.store<simulation::Controllable>().find(other);
        CHECK(bot_link.normalized_thrust_intent == session_link.normalized_thrust_intent);
        CHECK(world.store<simulation::PhysicsBody>().find(entity)->acceleration() ==
              world.store<simulation::PhysicsBody>().find(other)->acceleration());
        if (locked) {
          CHECK(bot_link.normalized_thrust_intent == simulation::Vector2::create(0.0, 0.0));
          CHECK(world.store<simulation::PhysicsBody>().find(entity)->acceleration() ==
                simulation::Vector2::create(0.0, 0.0));
        } else if (token == fixture::generation()) {
          REQUIRE(bot_link.normalized_thrust_intent.has_value());
          if (release) {
            CHECK(*bot_link.normalized_thrust_intent == simulation::Vector2::create(0.0, 0.0));
          } else {
            CHECK(bot_link.normalized_thrust_intent->x() > 0.0);
            CHECK(bot_link.normalized_thrust_intent->y() < 0.0);
          }
        } else {
          CHECK(bot_link.normalized_thrust_intent == held);
        }
      }
    }
  }
}
