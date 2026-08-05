#include "entity_id.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "player.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "vector2.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::PhysicsBody stationary_body(const double x, const double y) {
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  return simulation::PhysicsBody::create(simulation::Vector2::create(x, y), zero, zero);
}

[[nodiscard]] simulation::Player player(const simulation::EntityId::Value id, const double x) {
  return simulation::Player::create(simulation::EntityId::create(id), stationary_body(x, 0.0));
}

} // namespace

TEST_CASE("Vector2 is finite, bounded, and normalizes signed zero", "[unit][simulation][vector2]") {
  const simulation::Vector2 vector = simulation::Vector2::create(-0.0, 4.0);

  CHECK(vector.x() == 0.0);
  CHECK_FALSE(std::signbit(vector.x()));
  CHECK(vector.y() == 4.0);
  CHECK(vector.magnitude() == Catch::Approx(4.0));
  CHECK(vector.dot(simulation::Vector2::create(2.0, 3.0)) == Catch::Approx(12.0));

  CHECK_THROWS_AS(simulation::Vector2::create(std::numeric_limits<double>::infinity(), 0.0),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::Vector2::create(std::numeric_limits<double>::quiet_NaN(), 0.0),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(
      simulation::Vector2::create(simulation::kMaximumPhysicalComponentMagnitude + 1.0, 0.0),
      simulation::SimulationValidationError);
}

TEST_CASE("Vector2 checked arithmetic preserves valid results and rejects invalid ones",
          "[unit][simulation][vector2]") {
  const simulation::Vector2 left = simulation::Vector2::create(3.0, -2.0);
  const simulation::Vector2 right = simulation::Vector2::create(-1.0, 6.0);

  CHECK(left + right == simulation::Vector2::create(2.0, 4.0));
  CHECK(left - right == simulation::Vector2::create(4.0, -8.0));
  CHECK(-left == simulation::Vector2::create(-3.0, 2.0));
  CHECK(left * 2.0 == simulation::Vector2::create(6.0, -4.0));
  CHECK(2.0 * left == simulation::Vector2::create(6.0, -4.0));
  CHECK(left / 2.0 == simulation::Vector2::create(1.5, -1.0));

  CHECK_THROWS_AS(left / 0.0, simulation::SimulationValidationError);
  CHECK_THROWS_AS(left * std::numeric_limits<double>::quiet_NaN(),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::Vector2::create(simulation::kMaximumPhysicalComponentMagnitude, 0.0) +
                      simulation::Vector2::create(1.0, 0.0),
                  simulation::SimulationValidationError);
}

TEST_CASE("EntityId accepts only the protocol-safe integer range",
          "[unit][simulation][entity_id]") {
  const simulation::EntityId minimum = simulation::EntityId::create(simulation::kMinimumEntityId);
  const simulation::EntityId maximum = simulation::EntityId::create(simulation::kMaximumEntityId);

  CHECK(minimum.value() == simulation::kMinimumEntityId);
  CHECK(maximum.value() == simulation::kMaximumEntityId);
  CHECK(minimum < maximum);
  CHECK_THROWS_AS(simulation::EntityId::create(0), simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::EntityId::create(simulation::kMaximumEntityId + 1),
                  simulation::SimulationValidationError);
}

TEST_CASE("Simulation validation errors expose stable machine-readable context",
          "[unit][simulation][validation]") {
  try {
    static_cast<void>(simulation::EntityId::create(0));
    FAIL("invalid EntityId was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() == simulation::SimulationValidationCode::kEntityIdOutOfRange);
    CHECK(error.code() == std::string_view{"SIMULATION.ENTITY_ID_OUT_OF_RANGE"});
    CHECK(error.context() == "entity_id.value");
    CHECK(std::string_view{error.detail()}.find("safe-integer range") != std::string_view::npos);
    CHECK(std::string_view{error.what()}.find("safe-integer range") != std::string_view::npos);
  }
}

TEST_CASE("PhysicsBody and Player replacements preserve value semantics",
          "[unit][simulation][composition]") {
  const simulation::PhysicsBody original_body = stationary_body(1.0, 2.0);
  const simulation::Vector2 new_velocity = simulation::Vector2::create(3.0, 4.0);
  const simulation::PhysicsBody moving_body = original_body.with_velocity(new_velocity);
  const simulation::Player original_player =
      simulation::Player::create(simulation::EntityId::create(7), original_body);
  const simulation::Player moving_player = original_player.with_body(moving_body);

  CHECK(original_body.velocity() == simulation::Vector2::create(0.0, 0.0));
  CHECK(moving_body.velocity() == new_velocity);
  CHECK(original_player.id() == moving_player.id());
  CHECK(original_player.body() == original_body);
  CHECK(moving_player.body() == moving_body);
}

TEST_CASE("GameWorld canonicalizes players and provides stable lookup",
          "[unit][simulation][game_world]") {
  const simulation::GameWorld world =
      simulation::GameWorld::create({player(9, 9.0), player(2, 2.0), player(5, 5.0)});

  REQUIRE(world.players().size() == 3);
  CHECK(world.players()[0].id().value() == 2);
  CHECK(world.players()[1].id().value() == 5);
  CHECK(world.players()[2].id().value() == 9);
  REQUIRE(world.find(simulation::EntityId::create(5)) != nullptr);
  CHECK(world.find(simulation::EntityId::create(5))->body().position().x() == 5.0);
  CHECK(world.find(simulation::EntityId::create(6)) == nullptr);
}

TEST_CASE("GameWorld rejects duplicate IDs and unsafe player counts",
          "[unit][simulation][game_world]") {
  CHECK_THROWS_AS(simulation::GameWorld::create({player(1, 0.0), player(1, 1.0)}),
                  simulation::SimulationValidationError);

  std::vector<simulation::Player> too_many_players;
  too_many_players.reserve(simulation::kMaximumPlayerCount + 1);
  for (std::size_t index = 0; index <= simulation::kMaximumPlayerCount; ++index) {
    too_many_players.push_back(player(static_cast<simulation::EntityId::Value>(index + 1), 0.0));
  }

  CHECK_THROWS_AS(simulation::GameWorld::create(std::move(too_many_players)),
                  simulation::SimulationValidationError);
}
