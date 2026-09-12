#include "chaser_controller.hpp"
#include "command_registry.hpp"
#include "command_sink.hpp"
#include "command_submission_result.hpp"
#include "commands/shield_command.hpp"
#include "commands/thrust_command.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/shield_component.hpp"
#include "controller.hpp"
#include "controller_host.hpp"
#include "controller_id.hpp"
#include "controllers_test_fixture.hpp"
#include "entity_id.hpp"
#include "game_simulation.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "observation.hpp"
#include "physics_body.hpp"
#include "scripted_replay_controller.hpp"
#include "simulation_limits.hpp"
#include "simulation_runtime.hpp"
#include "snapshot_publication.hpp"
#include "tick_sequence.hpp"
#include "tick_window.hpp"
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
// Five things are asserted, in the order a break would matter:
//
//  1. A host cannot be *built* with anything a session cannot hold.
//  2. A bot reads the exact object a session's encoder reads, not a privileged view of it.
//  3. The published world withholds live per-tick input from every reader, so a bot cannot read
//     what a human player cannot.
//  4. A bot's command and a session's command are indistinguishable once submitted, and the
//     committed world they produce is identical.
//  5. The newest command a source can send -- a shield pulse -- obeys the same invariant end to
//     end: the two sources produce indistinguishable `ShieldCommand` values, and **one** admission
//     decides both of them, refusing and admitting them on the same ticks for the same reasons.
//
// **The shield adds no controller capability.** `Controller` still holds exactly `request_body` and
// `request_thrust`, and bot shield policy is a later step's work
// (`docs/reviews/2026-09-12-shield-composition-contract.md`: "`Controller::request_shield` -- NOT
// added"). The bot half of the shield proofs below is therefore a `ScriptedReplayController`, whose
// recorded log already carries whole `simulation::Command` values and so can emit a pulse today
// with no controllers-domain change (`src/controllers/scripted_replay_controller.hpp`). An unused
// `request_shield` would be vocabulary with nothing behind it, which is the reservation this tree
// refuses.
//
// **A pulse is the sharpest available form of the invariant**, because it carries almost nothing to
// differ in: no direction, no duration, no strength -- only the addressed entity and an activation
// token (`src/simulation/commands/shield_command.hpp`). Whatever separates a bot's activation from
// a player's therefore cannot be hiding in the command value, and the proofs below look for it in
// the one place left: the admission that reads the world.

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

// One entity's pulse. The default token is the absent one, which is the initial generation of an
// entity whose input has never been invalidated -- what both seated entities carry here.
[[nodiscard]] simulation::Command
shield_for(const simulation::EntityId entity,
           const std::optional<simulation::TickSequence> generation = {}) {
  return simulation::Command{
      simulation::ShieldCommand{.entity = entity, .input_generation = generation}};
}

// A positive activation token neither seated entity was ever issued. Nothing has invalidated their
// input, so both carry the absent initial generation and the admission's exact optional equality
// refuses this one -- for either source, which is the point.
[[nodiscard]] simulation::TickSequence stale_generation() {
  return simulation::TickSequence::create(1);
}

// The committed activation, which is the **only** proof a pulse became a shield: queue acceptance
// and a local send are not confirmation, and this step adds no per-request negative receipt
// (`src/gameplay/shared/ability_system.hpp`). A `Shield` publishes every stored value, so what a
// bot can read here is exactly what a browser reads.
[[nodiscard]] std::optional<simulation::Shield>
published_shield(const simulation::WorldSnapshot& snapshot, const simulation::EntityId entity) {
  for (const simulation::ComponentStore<simulation::Shield>::Entry& entry :
       snapshot.components<simulation::Shield>()) {
    if (entry.entity == entity) {
      return entry.value;
    }
  }
  return std::nullopt;
}

// The phase the next tick's `kPreKernel` systems will read, which is the one the last tick
// committed. Bound to a named snapshot because the match section is deleted on an rvalue.
[[nodiscard]] simulation::MatchPhase
committed_phase_of(const testing::ControllersFixture& fixture) {
  const simulation::WorldSnapshot snapshot = fixture.game().snapshot();
  return snapshot.match().phase();
}

// One pulse from each source, submitted the two ways a pulse can reach the sink: the bot's is the
// next step of its hosted controller's recorded log, the session's is the same write capability
// called directly, which is what `SessionWebSocketSession` does. Returns the drained batch in
// submission order -- the bot's command, then the session's.
[[nodiscard]] std::vector<simulation::Command>
pulse_from_both(testing::ControllersFixture& fixture, const simulation::Command& session_pulse) {
  static_cast<void>(fixture.host().decide_once());
  CHECK(fixture.sink().submit(simulation::ControllerId::create(kSessionController),
                              session_pulse) == runtime::CommandSubmissionResult::kAccepted);
  std::vector<simulation::Command> drained = fixture.mailbox().drain();
  REQUIRE(drained.size() == 2);
  return drained;
}

// Commits command-free ticks until the next commit will be `tick`, so a test names the tick a
// published window ends on rather than counting the ticks between two of them.
void commit_until_next_tick_is(testing::ControllersFixture& fixture, const std::uint64_t tick) {
  while (fixture.game().tick_sequence().value() + 1 < tick) {
    static_cast<void>(fixture.commit());
  }
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

TEST_CASE("A bot's shield pulse and a session's shield pulse are indistinguishable once submitted",
          "[unit][controllers][symmetry][shield]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 2);
  const simulation::EntityId bot_entity = *fixture.entity_of(kBotController);
  const simulation::EntityId session_entity = *fixture.entity_of(kSessionController);

  // The fixture's seating tick left free play in `countdown`; one more commit reaches `running`,
  // which is the only phase the ability admits a pulse in.
  static_cast<void>(fixture.commit());
  REQUIRE(committed_phase_of(fixture) == simulation::MatchPhase::kRunning);

  fixture.host().add(controllers::ScriptedReplayController::create(
      simulation::ControllerId::create(kBotController),
      {controllers::ScriptedReplayController::Step{shield_for(bot_entity)}}));

  const std::vector<simulation::Command> drained =
      pulse_from_both(fixture, shield_for(session_entity));
  const auto& from_bot = std::get<simulation::ShieldCommand>(drained[0]);
  const auto& from_session = std::get<simulation::ShieldCommand>(drained[1]);

  // Two `Command` values that differ only in the entity each names. A pulse carries no direction
  // and no duration, so once the addressed entity is set aside there is exactly one field left for
  // a source to differ in -- the activation token -- and both sources carry the same absent one.
  CHECK(simulation::command_kind_of(drained[0]) == simulation::command_kind_of(drained[1]));
  CHECK(from_bot.input_generation == from_session.input_generation);
  CHECK(from_bot.entity == bot_entity);
  CHECK(from_session.entity == session_entity);
  CHECK(fixture.mailbox().statistics().accepted_command_count == 2);

  // And the tick that consumes them commits one activation each, with the same three windows and
  // the same captured parry-stun duration. A `Shield` records when it went up and what it will
  // inflict; it records nothing about who raised it, so the two values are equal.
  const simulation::WorldSnapshot snapshot = fixture.commit(drained);
  const std::optional<simulation::Shield> bot_shield = published_shield(snapshot, bot_entity);
  const std::optional<simulation::Shield> session_shield =
      published_shield(snapshot, session_entity);
  REQUIRE(bot_shield.has_value());
  REQUIRE(session_shield.has_value());
  CHECK(*bot_shield == *session_shield);
  CHECK(bot_shield->activation_tick() == snapshot.tick_sequence());
}

TEST_CASE("The phase and generation gates answer a bot pulse and a session pulse identically",
          "[unit][controllers][symmetry][shield]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 2);
  const simulation::EntityId bot_entity = *fixture.entity_of(kBotController);
  const simulation::EntityId session_entity = *fixture.entity_of(kSessionController);

  // Three passes, three literal steps. One hosted controller answers all three, because a scripted
  // log is one step per decision pass rather than one controller per decision.
  fixture.host().add(controllers::ScriptedReplayController::create(
      simulation::ControllerId::create(kBotController),
      {controllers::ScriptedReplayController::Step{shield_for(bot_entity)},
       controllers::ScriptedReplayController::Step{shield_for(bot_entity, stale_generation())},
       controllers::ScriptedReplayController::Step{shield_for(bot_entity)}}));

  // The match is not running yet, so neither pulse becomes an activation. Nothing is queued for
  // the tick that *is* running either: a refusal changes nothing at all.
  REQUIRE(committed_phase_of(fixture) == simulation::MatchPhase::kCountdown);
  const simulation::WorldSnapshot before_running =
      fixture.commit(pulse_from_both(fixture, shield_for(session_entity)));
  CHECK_FALSE(published_shield(before_running, bot_entity).has_value());
  CHECK_FALSE(published_shield(before_running, session_entity).has_value());
  REQUIRE(before_running.match().phase() == simulation::MatchPhase::kRunning);

  // Running now, but each pulse carries an activation token neither entity was ever issued. Exact
  // optional equality refuses a present generation against an absent one, and it refuses it for
  // the bot and for the session in the same pass of the same join.
  const simulation::WorldSnapshot stale =
      fixture.commit(pulse_from_both(fixture, shield_for(session_entity, stale_generation())));
  CHECK_FALSE(published_shield(stale, bot_entity).has_value());
  CHECK_FALSE(published_shield(stale, session_entity).has_value());

  // The same two sources, the same running match, the matching token: both admitted, identically.
  const simulation::WorldSnapshot admitted =
      fixture.commit(pulse_from_both(fixture, shield_for(session_entity)));
  const std::optional<simulation::Shield> bot_shield = published_shield(admitted, bot_entity);
  const std::optional<simulation::Shield> session_shield =
      published_shield(admitted, session_entity);
  REQUIRE(bot_shield.has_value());
  REQUIRE(session_shield.has_value());
  CHECK(*bot_shield == *session_shield);
  CHECK(bot_shield->activation_tick() == admitted.tick_sequence());
}

TEST_CASE("Active protection and a live cooldown refuse both sources until the ability returns",
          "[unit][controllers][symmetry][shield]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 2);
  const simulation::EntityId bot_entity = *fixture.entity_of(kBotController);
  const simulation::EntityId session_entity = *fixture.entity_of(kSessionController);

  // Four passes: the activation, the re-tap inside protection, the tap while only the cooldown is
  // left, and the tap on the tick both windows have ended.
  fixture.host().add(controllers::ScriptedReplayController::create(
      simulation::ControllerId::create(kBotController),
      {controllers::ScriptedReplayController::Step{shield_for(bot_entity)},
       controllers::ScriptedReplayController::Step{shield_for(bot_entity)},
       controllers::ScriptedReplayController::Step{shield_for(bot_entity)},
       controllers::ScriptedReplayController::Step{shield_for(bot_entity)}}));

  static_cast<void>(fixture.commit());
  REQUIRE(committed_phase_of(fixture) == simulation::MatchPhase::kRunning);

  const simulation::WorldSnapshot activated =
      fixture.commit(pulse_from_both(fixture, shield_for(session_entity)));
  const std::optional<simulation::Shield> first = published_shield(activated, bot_entity);
  REQUIRE(first.has_value());
  REQUIRE(published_shield(activated, session_entity) == first);

  // Re-tapping inside the protection changes nothing for either source. The stored value is still
  // the first activation, so no cooldown was consumed, no perfect opening restarted, and no second
  // activation was queued behind the first.
  const simulation::WorldSnapshot retapped =
      fixture.commit(pulse_from_both(fixture, shield_for(session_entity)));
  CHECK(published_shield(retapped, bot_entity) == first);
  CHECK(published_shield(retapped, session_entity) == first);

  // Protection has ended and the cooldown has not: the one state in which the component survives
  // with nothing left to protect. The expiry sweep keeps it precisely so it can still refuse, and
  // it refuses the bot and the session alike. The endpoints come from the published windows rather
  // than from a retyped tick count, so this reads the tuning the mode actually activated with.
  commit_until_next_tick_is(fixture, first->shield_window().expiry_tick().value());
  const simulation::WorldSnapshot cooling =
      fixture.commit(pulse_from_both(fixture, shield_for(session_entity)));
  REQUIRE(cooling.tick_sequence() == first->shield_window().expiry_tick());
  CHECK(published_shield(cooling, bot_entity) == first);
  CHECK(published_shield(cooling, session_entity) == first);

  // The first tick on which both windows have expired. The sweep erases the spent value and the
  // same pulse from each source is admitted again, dated that tick and equal to the other's.
  commit_until_next_tick_is(fixture, first->cooldown_window().expiry_tick().value());
  const simulation::WorldSnapshot returned =
      fixture.commit(pulse_from_both(fixture, shield_for(session_entity)));
  REQUIRE(returned.tick_sequence() == first->cooldown_window().expiry_tick());
  const std::optional<simulation::Shield> second = published_shield(returned, bot_entity);
  REQUIRE(second.has_value());
  CHECK(published_shield(returned, session_entity) == second);
  CHECK(second->activation_tick() == first->cooldown_window().expiry_tick());
}
