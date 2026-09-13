#include "shared/velocity_rotation_system.hpp"

#include "fixtures/velocity_control_fixture.hpp"
#include "gameplay_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;
namespace fixture = blob_royale::testing::velocity_control_fixture;

namespace {

struct QuarterTurnCase final {
  std::array<double, 2> initial;
  std::array<double, 2> clockwise;
  std::array<double, 2> counterclockwise;
};

constexpr std::array kQuarterTurnCases{
    QuarterTurnCase{{3.0, 4.0}, {-4.0, 3.0}, {4.0, -3.0}},
    QuarterTurnCase{{-3.0, 4.0}, {-4.0, -3.0}, {4.0, 3.0}},
    QuarterTurnCase{{0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}},
    QuarterTurnCase{{0.0, -5.0}, {5.0, 0.0}, {-5.0, 0.0}},
    QuarterTurnCase{{simulation::kMaximumPhysicalComponentMagnitude, 0.0},
                    {0.0, simulation::kMaximumPhysicalComponentMagnitude},
                    {0.0, -simulation::kMaximumPhysicalComponentMagnitude}}};

[[nodiscard]] simulation::Vector2 vector(const std::array<double, 2> coordinates) {
  return simulation::Vector2::create(coordinates[0], coordinates[1]);
}

void record(simulation::GameWorld& world, const bool clockwise,
            const std::optional<simulation::TickSequence> generation = {}) {
  world.mutable_store<simulation::Controllable>()
      .mutable_find(fixture::entity())
      ->commands_this_tick = {fixture::rotate(clockwise, generation)};
}

} // namespace

TEST_CASE(
    "velocity rotation applies exact screen-space quarter turns preserving every other body field",
    "[unit][gameplay][velocity_rotation]") {
  const auto system = gameplay::VelocityRotationSystem::create();
  CHECK(system->name() == std::string_view{"velocity_rotation"});
  const testing::TickHarness harness{fixture::tick()};
  for (const auto& sample : kQuarterTurnCases) {
    for (const bool clockwise : {false, true}) {
      auto world = fixture::world(vector(sample.initial));
      record(world, clockwise);
      auto expected = world;
      const auto* body = world.store<simulation::PhysicsBody>().find(fixture::entity());
      const auto turned = vector(clockwise ? sample.clockwise : sample.counterclockwise);
      expected.mutable_store<simulation::PhysicsBody>().insert_or_assign(
          fixture::entity(), body->with_velocity(turned));
      system->apply(world, harness.context());
      CHECK(world == expected);
      CHECK(turned.dot(turned) == vector(sample.initial).dot(vector(sample.initial)));
    }
  }
}

TEST_CASE("four quarter turns and an opposite pair restore velocity exactly without consuming "
          "charge state",
          "[unit][gameplay][velocity_rotation][charge]") {
  const auto system = gameplay::VelocityRotationSystem::create();
  const testing::TickHarness harness{fixture::tick()};
  for (const bool clockwise : {false, true}) {
    auto world = fixture::world();
    const auto charge = fixture::active_charge();
    world.mutable_store<simulation::Charge>().insert_or_assign(fixture::entity(), charge);
    for (std::uint64_t turn = 0; turn < 4; ++turn) {
      record(world, clockwise);
      system->apply(world, harness.context());
    }
    CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->velocity() ==
          fixture::velocity());
    record(world, clockwise);
    system->apply(world, harness.context());
    record(world, !clockwise);
    system->apply(world, harness.context());
    CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->velocity() ==
          fixture::velocity());
    REQUIRE(world.store<simulation::Charge>().find(fixture::entity()) != nullptr);
    CHECK(*world.store<simulation::Charge>().find(fixture::entity()) == charge);
  }
}

TEST_CASE(
    "velocity rotation refuses missing controllers bodies static actors and nonrunning phases",
    "[unit][gameplay][velocity_rotation][validation]") {
  const auto system = gameplay::VelocityRotationSystem::create();
  const testing::TickHarness harness{fixture::tick()};
  for (const auto phase : {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
                           simulation::MatchPhase::kEnded}) {
    auto world = fixture::world();
    record(world, true);
    world.mutable_match().phase = phase;
    const auto before = world;
    system->apply(world, harness.context());
    CHECK(world == before);
  }
  for (const std::string_view target : {"bodyless", "uncontrolled", "static"}) {
    auto world = fixture::world();
    record(world, true);
    if (target == "bodyless")
      world.mutable_store<simulation::PhysicsBody>().erase(fixture::entity());
    if (target == "uncontrolled")
      world.mutable_store<simulation::Controllable>().erase(fixture::entity());
    if (target == "static") {
      world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
          fixture::entity(), simulation::PhysicsBody::create_static(simulation::Vector2::create(
                                 fixture::kInitialX, fixture::kInitialY)));
    }
    const auto before = world;
    system->apply(world, harness.context());
    CHECK(world == before);
  }
}

TEST_CASE("velocity rotation requires exact optional generation equality and never holds a pulse",
          "[unit][gameplay][velocity_rotation][input_generation]") {
  const std::array generations{std::optional<simulation::TickSequence>{},
                               std::optional{fixture::tick()},
                               std::optional{fixture::tick(fixture::kAfterStunTick)}};
  const auto system = gameplay::VelocityRotationSystem::create();
  const testing::TickHarness harness{fixture::tick(fixture::kAfterStunTick)};
  for (const auto held_generation : generations) {
    for (const auto submitted_generation : generations) {
      auto world = fixture::world();
      auto* held = world.mutable_store<simulation::Controllable>().mutable_find(fixture::entity());
      held->input_generation = held_generation;
      record(world, true, submitted_generation);
      system->apply(world, harness.context());
      CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->velocity() ==
            (held_generation == submitted_generation ? simulation::Vector2::create(-40.0, 30.0)
                                                     : fixture::velocity()));
      held->commands_this_tick.clear();
      const auto cleared = world;
      system->apply(world, harness.context());
      CHECK(world == cleared);
    }
  }
}

TEST_CASE("stun refuses velocity turns while momentum persists and exact expiry admits a fresh "
          "matching turn",
          "[unit][gameplay][velocity_rotation][stun]") {
  auto world = fixture::world();
  world.mutable_store<simulation::Controllable>()
      .mutable_find(fixture::entity())
      ->input_generation = fixture::tick();
  world.mutable_store<simulation::Stun>().insert_or_assign(
      fixture::entity(),
      simulation::Stun{simulation::TickWindow::create(fixture::tick(), fixture::kStunDuration)});
  const auto system = gameplay::VelocityRotationSystem::create();
  record(world, true, fixture::tick());
  const auto before = world;
  const testing::TickHarness stunned{fixture::tick()};
  system->apply(world, stunned.context());
  CHECK(world == before);
  world.mutable_store<simulation::Controllable>()
      .mutable_find(fixture::entity())
      ->commands_this_tick.clear();
  const testing::TickHarness recovered{fixture::tick(fixture::kAfterStunTick)};
  system->apply(world, recovered.context());
  CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->velocity() ==
        fixture::velocity());
  record(world, true, fixture::tick());
  system->apply(world, recovered.context());
  CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->velocity() ==
        simulation::Vector2::create(-40.0, 30.0));
  CHECK(world.store<simulation::Stun>().find(fixture::entity()) != nullptr);
}

TEST_CASE(
    "the actual mode pipeline rotates a newly admitted charge and accepts another turn midcharge",
    "[unit][gameplay][velocity_rotation][charge][integration]") {
  auto game = fixture::game();
  fixture::step(game, {fixture::rotate(true),
                       simulation::ChargeCommand{fixture::entity(), fixture::east(), {}}});
  const auto launched = game.snapshot();
  const auto body = testing::published_body(launched, fixture::entity().value());
  REQUIRE(body.has_value());
  CHECK(body->velocity() == simulation::Vector2::create(-40.0, 30.0 + fixture::kChargeGain));
  REQUIRE(launched.components<simulation::Charge>().size() == 1);
  const auto charge = launched.components<simulation::Charge>().front().value;
  REQUIRE(charge.active_window().contains(launched.tick_sequence()));
  fixture::step(game, {fixture::rotate(false)});
  const auto turned = game.snapshot();
  CHECK(testing::published_body(turned, fixture::entity().value())->velocity() ==
        simulation::Vector2::create(30.0 + fixture::kChargeGain, 40.0));
  REQUIRE(turned.components<simulation::Charge>().size() == 1);
  CHECK(turned.components<simulation::Charge>().front().value == charge);
  fixture::step(game);
  const auto released = game.snapshot();
  CHECK(testing::published_body(released, fixture::entity().value())->velocity() ==
        testing::published_body(turned, fixture::entity().value())->velocity());
}

TEST_CASE("the actual input batch gives conflicting quarter turns to the last submitted pulse",
          "[unit][gameplay][velocity_rotation][integration]") {
  for (const bool last_clockwise : {false, true}) {
    auto game = fixture::game();
    fixture::step(game, {fixture::rotate(!last_clockwise), fixture::rotate(last_clockwise)});
    const auto body = testing::published_body(game.snapshot(), fixture::entity().value());
    REQUIRE(body.has_value());
    CHECK(body->velocity() == (last_clockwise ? simulation::Vector2::create(-40.0, 30.0)
                                              : simulation::Vector2::create(40.0, -30.0)));
  }
}
