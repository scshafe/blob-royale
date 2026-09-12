#include "shared/status_system.hpp"

#include "fixtures/status_fixture.hpp"
#include "gameplay_validation_error.hpp"
#include "shared/match_reset_system.hpp"
#include "shared/respawn_system.hpp"
#include "simulation_validation_error.hpp"
#include "spawn_seating.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>

namespace simulation = blob_royale::simulation;
namespace gameplay = blob_royale::gameplay;
namespace testing = blob_royale::testing;
namespace fixture = blob_royale::testing::status_fixture;

TEST_CASE("positive stun clears propulsion and invalidates input without changing velocity",
          "[unit][gameplay][status][stun]") {
  auto world = fixture::world();
  auto* controllable =
      world.mutable_store<simulation::Controllable>().mutable_find(fixture::entity());
  controllable->normalized_thrust_intent = fixture::direction();
  controllable->commands_this_tick.push_back(fixture::command());
  const auto commands = controllable->commands_this_tick;
  world.emit(simulation::StunRequest{fixture::entity(), fixture::kDuration});
  const testing::TickHarness harness{fixture::tick()};
  const auto status = gameplay::StatusSystem::create();
  CHECK(status->name() == "status");
  status->apply(world, harness.context());
  REQUIRE(world.store<simulation::Stun>().find(fixture::entity()) != nullptr);
  CHECK(*world.store<simulation::Stun>().find(fixture::entity()) == fixture::stun());
  CHECK(controllable->input_generation == fixture::tick());
  CHECK(controllable->normalized_thrust_intent == fixture::zero());
  CHECK(controllable->commands_this_tick == commands);
  CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->acceleration() ==
        fixture::zero());
  CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->velocity() ==
        fixture::velocity());
}

TEST_CASE("zero duration and missing bodyless or static targets leave the entire world unchanged",
          "[unit][gameplay][status][stun]") {
  for (const auto current_tick : {simulation::TickSequence::zero(), fixture::tick()}) {
    auto world = fixture::invalid_target_world();
    world.emit(simulation::StunRequest{fixture::entity(), fixture::kZeroDuration});
    for (const auto target :
         {fixture::kBodylessEntity, fixture::kStaticEntity, fixture::kMissingEntity}) {
      world.emit(simulation::StunRequest{fixture::entity(target), fixture::kOverflowDuration});
    }
    const auto before = world;
    const testing::TickHarness harness{current_tick};
    gameplay::StatusSystem::create()->apply(world, harness.context());
    CHECK(world == before);
  }
}

TEST_CASE("later invalid windows roll back every request and defer expired-status cleanup",
          "[unit][gameplay][status][validation]") {
  auto world = fixture::world_with_status(fixture::kEarlierActivation, fixture::kDuration);
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      fixture::entity(fixture::kSecondEntity),
      *world.store<simulation::PhysicsBody>().find(fixture::entity()));
  world.emit(simulation::StunRequest{fixture::entity(), fixture::kDuration});
  world.emit(
      simulation::StunRequest{fixture::entity(fixture::kSecondEntity), fixture::kOverflowDuration});
  const auto before = world;
  const testing::TickHarness harness{fixture::tick()};
  try {
    gameplay::StatusSystem::create()->apply(world, harness.context());
    FAIL("overflowing applicable stun request was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.code() == "SIMULATION.TICK_WINDOW_EXPIRY_OVERFLOW");
  }
  CHECK(world == before);
}

TEST_CASE("positive stun at committing tick zero fails before mutating expired status",
          "[unit][gameplay][status][validation]") {
  auto world = fixture::world();
  world.mutable_store<simulation::Stun>().insert_or_assign(
      fixture::entity(), simulation::Stun{simulation::TickWindow::create(
                             simulation::TickSequence::zero(), fixture::kZeroDuration)});
  world.emit(simulation::StunRequest{fixture::entity(), fixture::kDuration});
  const auto before = world;
  const testing::TickHarness harness{simulation::TickSequence::zero()};
  try {
    gameplay::StatusSystem::create()->apply(world, harness.context());
    FAIL("positive stun at tick zero was accepted");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.code() == "GAMEPLAY.STATUS_ACTIVATION_TICK_ZERO");
  }
  CHECK(world == before);
}

TEST_CASE("active stun requests merge maximum expiry and invalidate even beneath a longer window",
          "[unit][gameplay][status][stun]") {
  for (const bool longest_first : {false, true}) {
    auto world = fixture::world_with_status(fixture::kEarlierActivation, fixture::kLongDuration);
    const auto earlier =
        world.store<simulation::Stun>().find(fixture::entity())->window.activation_tick();
    const auto first = longest_first ? fixture::kLongDuration : fixture::kOneTick;
    const auto second = longest_first ? fixture::kOneTick : fixture::kLongDuration;
    world.emit(simulation::StunRequest{fixture::entity(), first});
    world.emit(simulation::StunRequest{fixture::entity(), second});
    const testing::TickHarness harness{fixture::tick()};
    gameplay::StatusSystem::create()->apply(world, harness.context());
    const auto& window = world.store<simulation::Stun>().find(fixture::entity())->window;
    CHECK(window.activation_tick() == earlier);
    CHECK(window.expiry_tick() == fixture::tick(fixture::kActivation + fixture::kLongDuration));
    CHECK(world.store<simulation::Controllable>().find(fixture::entity())->input_generation ==
          fixture::tick());
  }
  auto world = fixture::world_with_status(fixture::kEarlierActivation, fixture::kLongDuration);
  const auto previous = *world.store<simulation::Stun>().find(fixture::entity());
  world.emit(simulation::StunRequest{fixture::entity(), fixture::kOneTick});
  const testing::TickHarness harness{fixture::tick()};
  gameplay::StatusSystem::create()->apply(world, harness.context());
  CHECK(*world.store<simulation::Stun>().find(fixture::entity()) == previous);
  CHECK(world.store<simulation::Controllable>().find(fixture::entity())->input_generation ==
        fixture::tick());
}

TEST_CASE("expired stun is replaced without bridging a disconnected interval",
          "[unit][gameplay][status][stun]") {
  auto world = fixture::world_with_status();
  world.emit(simulation::StunRequest{fixture::entity(), fixture::kDuration});
  const testing::TickHarness harness{fixture::tick(fixture::kLaterActivation)};
  gameplay::StatusSystem::create()->apply(world, harness.context());
  CHECK(*world.store<simulation::Stun>().find(fixture::entity()) ==
        fixture::stun(fixture::kLaterActivation, fixture::kDuration));
  CHECK(world.store<simulation::Controllable>().find(fixture::entity())->input_generation ==
        fixture::tick(fixture::kLaterActivation));
}

TEST_CASE("expiry removes stun but preserves its generation and never restores old input",
          "[unit][gameplay][status][stun]") {
  auto world = fixture::world_with_status();
  const testing::TickHarness harness{fixture::tick(fixture::kExpiry)};
  gameplay::StatusSystem::create()->apply(world, harness.context());
  CHECK(world.store<simulation::Stun>().find(fixture::entity()) == nullptr);
  const auto* controllable = world.store<simulation::Controllable>().find(fixture::entity());
  CHECK(controllable->input_generation == fixture::tick());
  CHECK(controllable->normalized_thrust_intent == fixture::zero());
  gameplay::ThrustSteeringSystem::create()->apply(world, harness.context());
  CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->acceleration() ==
        fixture::zero());
}

TEST_CASE("kernel stun requests publish endpoints and generation while discarding locked commands",
          "[unit][gameplay][status][stun][kernel]") {
  auto game = fixture::game({{fixture::kOneTick, {fixture::entity(), fixture::kDuration}}});
  fixture::step(game, {fixture::command()});
  const auto applied = game.snapshot();
  REQUIRE(applied.components<simulation::Stun>().size() == fixture::kOneTick);
  CHECK(applied.components<simulation::Stun>().front().value == fixture::stun(fixture::kOneTick));
  const auto& controllable = applied.components<simulation::Controllable>().front().value;
  CHECK(controllable.input_generation == fixture::tick(fixture::kOneTick));
  CHECK(controllable.commands_this_tick.empty());
  CHECK_FALSE(controllable.normalized_thrust_intent.has_value());
  fixture::step(game, {fixture::command(fixture::tick(fixture::kOneTick))});
  fixture::advance(game, fixture::kOneTick + fixture::kDuration);
  const auto expired = game.snapshot();
  CHECK(expired.components<simulation::Stun>().empty());
  CHECK(expired.components<simulation::Controllable>().front().value.input_generation ==
        fixture::tick(fixture::kOneTick));
  CHECK(testing::published_body(expired, fixture::entity().value())->acceleration() ==
        fixture::zero());
  fixture::step(game, {fixture::command()});
  CHECK(testing::published_body(game.snapshot(), fixture::entity().value())->acceleration() ==
        fixture::zero());
  fixture::step(game, {fixture::command(fixture::tick(fixture::kOneTick))});
  CHECK(testing::published_body(game.snapshot(), fixture::entity().value())->acceleration() !=
        fixture::zero());
}

TEST_CASE("one-tick stun accepts fresh matching input on the next steering tick",
          "[unit][gameplay][status][stun][kernel]") {
  auto game = fixture::game({{fixture::kOneTick, {fixture::entity(), fixture::kOneTick}}});
  fixture::advance(game, fixture::kOneTick);
  fixture::step(game, {fixture::command(fixture::tick(fixture::kOneTick))});
  const auto snapshot = game.snapshot();
  CHECK(snapshot.components<simulation::Stun>().empty());
  CHECK(testing::published_body(snapshot, fixture::entity().value())->acceleration() !=
        fixture::zero());
}

TEST_CASE("failed in-tick status request leaves committed snapshot and sequence untouched",
          "[unit][gameplay][status][validation][kernel]") {
  auto game = fixture::game({{fixture::kOneTick, {fixture::entity(), fixture::kDuration}},
                             {fixture::kOneTick, {fixture::entity(), fixture::kOverflowDuration}}});
  const auto before = game.snapshot();
  CHECK_THROWS_AS(fixture::step(game, {fixture::command()}), simulation::SimulationValidationError);
  CHECK(game.snapshot() == before);
  CHECK(game.tick_sequence() == simulation::TickSequence::zero());
}

TEST_CASE("stunned hazard retains later external velocity and ordinary lifetime expiry",
          "[unit][gameplay][status][stun][lifetime]") {
  auto world = fixture::world();
  world.mutable_store<simulation::Controllable>().erase(fixture::entity());
  world.mutable_store<simulation::Lifetime>().insert_or_assign(
      fixture::entity(), simulation::Lifetime{fixture::kHazardLifetime});
  world.emit(simulation::StunRequest{fixture::entity(), fixture::kLongDuration});
  const testing::TickHarness harness{fixture::tick()};
  gameplay::StatusSystem::create()->apply(world, harness.context());
  auto* body = world.mutable_store<simulation::PhysicsBody>().mutable_find(fixture::entity());
  CHECK(body->velocity() == fixture::velocity());
  *body = body->with_velocity(fixture::bump_velocity());
  gameplay::StatusSystem::create()->apply(world, harness.context());
  CHECK(body->velocity() == fixture::bump_velocity());
  gameplay::LifetimeExpirySystem::create()->apply(world, harness.context());
  CHECK(world.store<simulation::Lifetime>().find(fixture::entity())->ticks_remaining ==
        fixture::kHazardLifetime - fixture::kOneTick);

  auto initial = fixture::world();
  initial.mutable_store<simulation::Controllable>().erase(fixture::entity());
  initial.mutable_store<simulation::Lifetime>().insert_or_assign(
      fixture::entity(), simulation::Lifetime{fixture::kHazardLifetime});
  auto game = fixture::game({{fixture::kOneTick, {fixture::entity(), fixture::kLongDuration}}},
                            std::move(initial));
  fixture::advance(game, fixture::kHazardLifetime);
  const auto expired = game.snapshot();
  CHECK(expired.entities().empty());
  CHECK(expired.components<simulation::Stun>().empty());
}

TEST_CASE("zero and delayed same-entity respawn remove stun but retain cancellation generation",
          "[unit][gameplay][status][stun][respawn]") {
  for (const auto delay : fixture::kRespawnDelays) {
    auto world = fixture::world_with_status();
    world.emit(simulation::EliminationEvent{fixture::entity()});
    const testing::TickHarness harness{fixture::tick()};
    const auto respawn = gameplay::RespawnSystem::create(delay);
    respawn->apply(world, harness.context());
    CHECK(world.store<simulation::Stun>().empty());
    CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity()) == nullptr);
    for (std::uint64_t elapsed = 0; elapsed < delay; ++elapsed) {
      respawn->apply(world, harness.context());
    }
    CHECK(world.store<simulation::RespawnTimer>().find(fixture::entity()) == nullptr);
    simulation::seat_body_at_rest(world, fixture::entity(), fixture::replacement_position(),
                                  fixture::kRadius);
    auto* controllable =
        world.mutable_store<simulation::Controllable>().mutable_find(fixture::entity());
    CHECK(controllable->input_generation == fixture::tick());
    CHECK_FALSE(controllable->normalized_thrust_intent.has_value());
    controllable->commands_this_tick = {fixture::command()};
    gameplay::ThrustSteeringSystem::create()->apply(world, harness.context());
    CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->acceleration() ==
          fixture::zero());
    controllable->commands_this_tick = {fixture::command(fixture::tick())};
    gameplay::ThrustSteeringSystem::create()->apply(world, harness.context());
    CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->acceleration() !=
          fixture::zero());
  }
}

TEST_CASE("a later physical collision moves a still-stunned body through the ordinary solver",
          "[unit][gameplay][status][stun][kernel]") {
  auto game = fixture::game({{fixture::kOneTick, {fixture::entity(), fixture::kLongDuration}}},
                            fixture::later_collision_world());
  fixture::advance(game, fixture::kOneTick);
  CHECK(testing::published_body(game.snapshot(), fixture::entity().value())->velocity() ==
        fixture::zero());
  fixture::advance(game, fixture::kAfterCollisionTick);
  const auto bumped = game.snapshot();
  REQUIRE_FALSE(bumped.components<simulation::Stun>().empty());
  CHECK(bumped.components<simulation::Stun>().front().value.window.contains(game.tick_sequence()));
  const auto body = testing::published_body(bumped, fixture::entity().value());
  REQUIRE(body.has_value());
  CHECK(body->velocity().x() < fixture::zero().x());
  CHECK(body->acceleration() == fixture::zero());
}

TEST_CASE("round reset destroys stun and generation with their participant entity",
          "[unit][gameplay][status][stun][reset]") {
  auto world = fixture::world_with_status();
  world.mutable_match().phase = simulation::MatchPhase::kLobby;
  world.mutable_match().previous_phase = simulation::MatchPhase::kEnded;
  const testing::TickHarness harness{fixture::tick()};
  gameplay::MatchResetSystem::create()->apply(world, harness.context());
  CHECK_FALSE(world.contains(fixture::entity()));
  CHECK(world.store<simulation::Stun>().empty());
  CHECK(world.store<simulation::Controllable>().empty());
}
