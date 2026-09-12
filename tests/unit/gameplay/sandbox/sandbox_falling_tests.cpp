#include "sandbox/sandbox_mode.hpp"

#include "components/respawn_timer_component.hpp"
#include "fixtures/falling_mode_fixture.hpp"
#include "game_mode_registry.hpp"

#include <catch2/catch_test_macros.hpp>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;
namespace falling = testing::falling_mode_fixture;

TEST_CASE("registry Sandbox falls before running and returns along its free-play lifecycle",
          "[unit][gameplay][sandbox][falling][respawn]") {
  for (const std::uint64_t delay : {0U, 2U}) {
    CAPTURE(delay);
    auto modes = gameplay::GameModeConfiguration::defaults();
    modes.sandbox = gameplay::SandboxConfiguration::create(
        static_cast<double>(delay) * simulation::FixedDelta::canonical().seconds());
    auto game = falling::game(gameplay::GameModeRegistry::create("sandbox", modes),
                              simulation::MatchPhase::kLobby);
    const auto initial = game.snapshot();
    CHECK(initial.match().phase() == simulation::MatchPhase::kLobby);
    const auto fallen = falling::step(game);
    // Support is already active during the first kernel, before the ordinary free-play
    // objective advances lobby to countdown. The second tick reaches running permanently.
    CHECK(fallen.match().phase() == simulation::MatchPhase::kCountdown);
    CHECK(falling::component<simulation::PhysicsBody>(fallen, 1) == nullptr);
    CHECK(falling::component<simulation::Controllable>(fallen, 1) != nullptr);
    CHECK(falling::component<simulation::ContactEffectAdmission>(fallen, 1) == nullptr);
    REQUIRE(falling::component<simulation::PhysicsBody>(fallen, 2) != nullptr);
    CHECK(falling::component<simulation::PhysicsBody>(fallen, 2)->velocity() ==
          falling::point(0, 0));
    if (delay == 0) {
      CHECK(falling::component<simulation::RespawnTimer>(fallen, 1) == nullptr);
    } else {
      REQUIRE(falling::component<simulation::RespawnTimer>(fallen, 1) != nullptr);
      CHECK(falling::component<simulation::RespawnTimer>(fallen, 1)->ticks_remaining == delay);
    }
    for (std::uint64_t elapsed = 1; elapsed <= delay; ++elapsed) {
      const auto waiting = falling::step(game);
      CHECK(falling::component<simulation::PhysicsBody>(waiting, 1) == nullptr);
      CHECK(waiting.match().phase() == simulation::MatchPhase::kRunning);
    }
    const auto returned = falling::step(game);
    CHECK(returned.tick_sequence().value() == 1 + delay + 1);
    CHECK(returned.match().phase() == simulation::MatchPhase::kRunning);
    const auto* body = falling::component<simulation::PhysicsBody>(returned, 1);
    REQUIRE(body != nullptr);
    CHECK(body->position() == falling::point(100, 320));
    CHECK(body->velocity() == falling::point(0, 0));
    CHECK(body->acceleration() == falling::point(0, 0));
    CHECK(body->ground_attachment() == simulation::GroundAttachment::kGroundBound);
    // Publication deliberately omits held intent. An additional no-input tick proves it was
    // cleared internally rather than merely hidden by the snapshot projection.
    const auto resting = falling::step(game);
    REQUIRE(falling::component<simulation::PhysicsBody>(resting, 1) != nullptr);
    CHECK(falling::component<simulation::PhysicsBody>(resting, 1)->position() == body->position());
    CHECK(falling::component<simulation::PhysicsBody>(resting, 1)->velocity() ==
          falling::point(0, 0));
    CHECK(resting.match().phase() == simulation::MatchPhase::kRunning);
  }
}

TEST_CASE("ordinary registry Sandbox seating creates a ground-bound player before free play "
          "reaches running",
          "[unit][gameplay][sandbox][falling][spawn]") {
  auto game = simulation::GameSimulation::create(
      testing::gameplay_configuration(), simulation::GameWorld::create({}),
      simulation::GameSimulationSetup::of_mode(
          falling::map(), gameplay::GameModeRegistry::create(
                              "sandbox", gameplay::GameModeConfiguration::defaults())));
  const auto initial = game.snapshot();
  CHECK(initial.match().phase() == simulation::MatchPhase::kLobby);
  const auto seated = falling::step(game, {testing::spawn_command(9)});
  REQUIRE(seated.components<simulation::PhysicsBody>().size() == 1);
  CHECK(seated.components<simulation::PhysicsBody>().front().value.ground_attachment() ==
        simulation::GroundAttachment::kGroundBound);
  CHECK(seated.match().phase() == simulation::MatchPhase::kCountdown);
  const auto running = falling::step(game);
  CHECK(running.match().phase() == simulation::MatchPhase::kRunning);
}
