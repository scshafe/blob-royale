#include "chaser_controller.hpp"
#include "command_registry.hpp"
#include "command_sink.hpp"
#include "command_submission_result.hpp"
#include "commands/thrust_command.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "controller.hpp"
#include "controller_host.hpp"
#include "controller_id.hpp"
#include "controllers_test_fixture.hpp"
#include "entity_id.hpp"
#include "game_simulation.hpp"
#include "game_world.hpp"
#include "observation.hpp"
#include "physics_body.hpp"
#include "scripted_replay_controller.hpp"
#include "simulation_limits.hpp"
#include "simulation_runtime.hpp"
#include "snapshot_publication.hpp"
#include "vector2.hpp"
#include "wanderer_controller.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace controllers = blob_royale::controllers;
namespace runtime = blob_royale::runtime;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

// canonical: human_bot_symmetry_tests -- the invariant that a bot and a player are the same thing
// to the simulation, asserted rather than assumed.
//
// This file mirrors no single source, because the invariant is not a property of one class: it is
// the joint property of `ControllerHost`'s construction surface, `Observation`'s read surface,
// `CommandSink`'s write surface, and the `Controllable` component's publication rule. ADR 0004
// § "Controllers" states it -- "All three hold exactly the two capabilities the server holds ...
// and the simulation stores `Controllable::controller_id` and never branches on it, which makes the
// symmetry a testable invariant rather than an aspiration" -- and this is the test it asks for.
//
// Four things are asserted, in the order a break would matter:
//
//  1. A host cannot be *built* with anything a session cannot hold.
//  2. A bot reads the exact object a session's encoder reads, not a privileged view of it.
//  3. The published world withholds live per-tick input from every reader, so a bot cannot read
//     what a human player cannot.
//  4. A bot's command and a session's command are indistinguishable once submitted, and the
//     committed world they produce is identical.

namespace {

constexpr std::uint64_t kBotController = simulation::kMinimumControllerId;
constexpr std::uint64_t kSessionController = simulation::kMinimumControllerId + 1;

[[nodiscard]] simulation::Command thrust_for(const simulation::EntityId entity, const double x,
                                             const double y) {
  return simulation::Command{
      simulation::ThrustCommand{.entity = entity, .direction = simulation::Vector2::create(x, y)}};
}

[[nodiscard]] std::optional<simulation::Controllable>
published_link(const simulation::WorldSnapshot& snapshot, const simulation::EntityId entity) {
  for (const simulation::ComponentStore<simulation::Controllable>::Entry& entry :
       snapshot.components<simulation::Controllable>()) {
    if (entry.entity == entity) {
      return entry.value;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<simulation::PhysicsBody>
published_body(const simulation::WorldSnapshot& snapshot, const simulation::EntityId entity) {
  for (const simulation::ComponentStore<simulation::PhysicsBody>::Entry& entry :
       snapshot.components<simulation::PhysicsBody>()) {
    if (entry.entity == entity) {
      return entry.value;
    }
  }
  return std::nullopt;
}

} // namespace

// 1. The construction surface. A host built from the simulation, the world, or the runtime would be
// a bot holding a capability no network session holds, and the type system rules it out completely
// where a runtime test could only sample. `ControllerHost` also publishes no accessor that hands
// either capability back out, so what it was built with is all it can ever reach.
static_assert(std::is_constructible_v<controllers::ControllerHost,
                                      const runtime::SnapshotPublication&, runtime::CommandSink&>,
              "a bot host holds exactly the read and write capabilities a network session holds");
static_assert(!std::is_constructible_v<controllers::ControllerHost, simulation::GameSimulation&>);
static_assert(!std::is_constructible_v<controllers::ControllerHost, simulation::GameWorld&>);
static_assert(!std::is_constructible_v<controllers::ControllerHost, runtime::SimulationRuntime&>);
static_assert(
    !std::is_constructible_v<controllers::ControllerHost, const runtime::SnapshotPublication&,
                             runtime::CommandSink&, simulation::GameSimulation&>);
// And the observation a controller decides from is built from a published snapshot and a durable
// identity, never from a live world.
static_assert(!std::is_constructible_v<controllers::Observation, simulation::GameWorld&,
                                       simulation::ControllerId>);
static_assert(!std::is_constructible_v<controllers::Observation, const simulation::WorldSnapshot&,
                                       simulation::ControllerId>);

TEST_CASE("A hosted bot reads the exact snapshot a network session's encoder would read",
          "[unit][controllers][symmetry]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 2);
  const std::shared_ptr<const simulation::WorldSnapshot> published = fixture.publication().latest();

  const controllers::Observation bot_view = fixture.observation_for(kBotController);

  // Pointer identity: one published value, read by both. There is no bot-only projection, no extra
  // section, and no second acquisition path.
  CHECK(&bot_view.snapshot() == published.get());
  CHECK(bot_view.retained_snapshot() == published);
}

TEST_CASE("The published world withholds this tick's live input from every reader",
          "[unit][controllers][symmetry]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 2);
  const simulation::EntityId bot_entity = *fixture.entity_of(kBotController);
  const simulation::EntityId session_entity = *fixture.entity_of(kSessionController);

  // Commit a tick in which both entities steered, so the world being published is one where live
  // per-tick input existed.
  const simulation::WorldSnapshot snapshot =
      fixture.commit({thrust_for(bot_entity, 1.0, 0.0), thrust_for(session_entity, -1.0, 0.0)});

  const std::optional<simulation::Controllable> bot_link = published_link(snapshot, bot_entity);
  const std::optional<simulation::Controllable> session_link =
      published_link(snapshot, session_entity);
  REQUIRE(bot_link.has_value());
  REQUIRE(session_link.has_value());

  // A published `Controllable` is the controller link and nothing else. If `commands_this_tick`
  // survived publication, an in-process bot could read every player's steering for the tick being
  // rendered -- an advantage no human has, and the sharpest break of this invariant available.
  CHECK(*bot_link == simulation::Controllable{bot_link->controller_id});
  CHECK(bot_link->commands_this_tick.empty());
  CHECK(session_link->commands_this_tick.empty());

  // The steering did land; the world is simply not telling readers who is steering right now.
  REQUIRE(published_body(snapshot, bot_entity).has_value());
  CHECK(published_body(snapshot, bot_entity)->acceleration().x() > 0.0);
  CHECK(published_body(snapshot, session_entity)->acceleration().x() < 0.0);
}

TEST_CASE("A bot's command and a session's command are indistinguishable once submitted",
          "[unit][controllers][symmetry]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 2);
  const simulation::EntityId bot_entity = *fixture.entity_of(kBotController);
  const simulation::EntityId session_entity = *fixture.entity_of(kSessionController);

  // The bot decides through the host. Its behavior is a recorded log so the two sides submit
  // exactly the same steering intent.
  fixture.host().add(controllers::ScriptedReplayController::create(
      simulation::ControllerId::create(kBotController),
      {controllers::ScriptedReplayController::Step{thrust_for(bot_entity, 1.0, 0.0)}}));
  static_cast<void>(fixture.host().decide_once());

  // The "session" is the same write capability called directly, which is exactly what
  // `SessionWebSocketSession` will do in plan Step 26: no host, no controller, no interface.
  CHECK(fixture.sink().submit(simulation::ControllerId::create(kSessionController),
                              thrust_for(session_entity, 1.0, 0.0)) ==
        runtime::CommandSubmissionResult::kAccepted);

  const std::vector<simulation::Command> drained = fixture.mailbox().drain();
  REQUIRE(drained.size() == 2);
  const auto& from_bot = std::get<simulation::ThrustCommand>(drained[0]);
  const auto& from_session = std::get<simulation::ThrustCommand>(drained[1]);

  // Two `Command` values that differ only in the entity each names. Nothing in the value, and
  // nothing the mailbox recorded about it, says which side produced it.
  CHECK(simulation::command_kind_of(drained[0]) == simulation::command_kind_of(drained[1]));
  CHECK(from_bot.direction == from_session.direction);
  CHECK(from_bot.entity == bot_entity);
  CHECK(from_session.entity == session_entity);
  CHECK(fixture.mailbox().statistics().accepted_command_count == 2);

  // And the tick that consumes them treats them identically.
  const simulation::WorldSnapshot snapshot = fixture.commit(drained);
  REQUIRE(published_body(snapshot, bot_entity).has_value());
  REQUIRE(published_body(snapshot, session_entity).has_value());
  CHECK(published_body(snapshot, bot_entity)->acceleration() ==
        published_body(snapshot, session_entity)->acceleration());
  CHECK(published_body(snapshot, bot_entity)->velocity() ==
        published_body(snapshot, session_entity)->velocity());
}

TEST_CASE("Nothing in the committed world says which controller is a bot",
          "[unit][controllers][symmetry]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 2);
  const simulation::EntityId bot_entity = *fixture.entity_of(kBotController);
  const simulation::EntityId session_entity = *fixture.entity_of(kSessionController);
  const simulation::WorldSnapshot snapshot = fixture.game().snapshot();

  const std::optional<simulation::Controllable> bot_link = published_link(snapshot, bot_entity);
  const std::optional<simulation::Controllable> session_link =
      published_link(snapshot, session_entity);
  REQUIRE(bot_link.has_value());
  REQUIRE(session_link.has_value());

  // The two links differ in exactly one field -- the durable identity -- and that field is an
  // opaque number the tick never branches on. The controller *kind* a client renders lives in
  // `ControllerDirectory`, which is presentation state outside the deterministic core, so the
  // committed world carries no notion of "bot" at all.
  CHECK(bot_link->controller_id != session_link->controller_id);
  CHECK(*bot_link == simulation::Controllable{bot_link->controller_id});
  CHECK(*session_link == simulation::Controllable{session_link->controller_id});
  CHECK(published_body(snapshot, bot_entity)->collision_layer() ==
        published_body(snapshot, session_entity)->collision_layer());
  CHECK(published_body(snapshot, bot_entity)->mass() ==
        published_body(snapshot, session_entity)->mass());
}
