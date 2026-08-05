#include "entity_id.hpp"
#include "fixed_delta.hpp"
#include "game_simulation.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "player.hpp"
#include "player_snapshot.hpp"
#include "simulation_config.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

struct PlayerFixture final {
  simulation::EntityId::Value entity_id;
  double position_x;
  double position_y;
  double velocity_x;
  double velocity_y;
  double acceleration_x;
  double acceleration_y;
};

constexpr PlayerFixture kLowerIdPlayer{2, 20.0, 21.0, 2.5, -3.5, 0.25, -0.5};
constexpr PlayerFixture kHigherIdPlayer{9, 90.0, 91.0, -4.5, 5.5, -0.75, 1.0};

[[nodiscard]] simulation::SimulationConfig snapshot_configuration(const double width = 500.0,
                                                                  const double height = 500.0,
                                                                  const double radius = 1.0,
                                                                  const std::uint64_t columns = 10,
                                                                  const std::uint64_t rows = 10) {
  return simulation::SimulationConfig::create(
      width, height, radius, simulation::SimulationConfig::kRequiredTicksPerSecond, columns, rows);
}

[[nodiscard]] simulation::GameSimulation
simulation_from_world(simulation::GameWorld world,
                      simulation::SimulationConfig configuration = snapshot_configuration()) {
  return simulation::GameSimulation::create(configuration, std::move(world));
}

[[nodiscard]] simulation::Player player_from_fixture(const PlayerFixture& fixture) {
  return simulation::Player::create(
      simulation::EntityId::create(fixture.entity_id),
      simulation::PhysicsBody::create(
          simulation::Vector2::create(fixture.position_x, fixture.position_y),
          simulation::Vector2::create(fixture.velocity_x, fixture.velocity_y),
          simulation::Vector2::create(fixture.acceleration_x, fixture.acceleration_y)));
}

[[nodiscard]] simulation::Player positioned_player(const simulation::EntityId::Value entity_id,
                                                   const double x, const double y) {
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  return simulation::Player::create(
      simulation::EntityId::create(entity_id),
      simulation::PhysicsBody::create(simulation::Vector2::create(x, y), zero, zero));
}

} // namespace

TEST_CASE("TickSequence is exact, ordered, and bounded without unsigned wraparound",
          "[unit][simulation][snapshot]") {
  const simulation::TickSequence zero = simulation::TickSequence::zero();
  const simulation::TickSequence one = zero.next();
  const simulation::TickSequence maximum =
      simulation::TickSequence::create(simulation::TickSequence::kMaximumValue);

  CHECK(zero.value() == simulation::TickSequence::kMinimumValue);
  CHECK(one.value() == 1);
  CHECK(zero < one);
  CHECK(maximum.value() == simulation::TickSequence::kMaximumValue);
  CHECK_THROWS_AS(maximum.next(), simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::TickSequence::create(simulation::TickSequence::kMaximumValue + 1),
                  simulation::SimulationValidationError);
}

TEST_CASE("WorldSnapshot copies complete player data in canonical EntityId order",
          "[unit][simulation][snapshot]") {
  const simulation::GameWorld world = simulation::GameWorld::create(
      {player_from_fixture(kHigherIdPlayer), player_from_fixture(kLowerIdPlayer)});
  const simulation::WorldSnapshot snapshot = simulation_from_world(world).snapshot();

  REQUIRE(snapshot.players().size() == 2);
  CHECK(snapshot.tick_sequence() == simulation::TickSequence::zero());
  CHECK(snapshot.players()[0].entity_id().value() == kLowerIdPlayer.entity_id);
  CHECK(snapshot.players()[0].position() ==
        simulation::Vector2::create(kLowerIdPlayer.position_x, kLowerIdPlayer.position_y));
  CHECK(snapshot.players()[0].velocity() ==
        simulation::Vector2::create(kLowerIdPlayer.velocity_x, kLowerIdPlayer.velocity_y));
  CHECK(snapshot.players()[0].acceleration() ==
        simulation::Vector2::create(kLowerIdPlayer.acceleration_x, kLowerIdPlayer.acceleration_y));
  CHECK(snapshot.players()[1].entity_id().value() == kHigherIdPlayer.entity_id);
  CHECK(snapshot.players()[1].position() ==
        simulation::Vector2::create(kHigherIdPlayer.position_x, kHigherIdPlayer.position_y));
  CHECK(snapshot.players()[1].velocity() ==
        simulation::Vector2::create(kHigherIdPlayer.velocity_x, kHigherIdPlayer.velocity_y));
  CHECK(
      snapshot.players()[1].acceleration() ==
      simulation::Vector2::create(kHigherIdPlayer.acceleration_x, kHigherIdPlayer.acceleration_y));
}

TEST_CASE("WorldSnapshot supports empty and maximum-sized validated worlds",
          "[unit][simulation][snapshot]") {
  const simulation::WorldSnapshot empty_snapshot =
      simulation_from_world(simulation::GameWorld::create({})).snapshot();

  std::vector<simulation::Player> maximum_players;
  maximum_players.reserve(simulation::kMaximumPlayerCount);
  for (std::size_t index = 0; index < simulation::kMaximumPlayerCount; ++index) {
    const double x = 1.0 + (2.0 * static_cast<double>(index % 64));
    const double y = 1.0 + (2.0 * static_cast<double>(index / 64));
    maximum_players.push_back(
        positioned_player(static_cast<simulation::EntityId::Value>(index + 1), x, y));
  }
  const simulation::WorldSnapshot maximum_snapshot =
      simulation_from_world(simulation::GameWorld::create(std::move(maximum_players)),
                            snapshot_configuration(130.0, 130.0, 0.25, 64, 64))
          .snapshot();

  CHECK(empty_snapshot.players().empty());
  CHECK(empty_snapshot.tick_sequence() == simulation::TickSequence::zero());
  REQUIRE(maximum_snapshot.players().size() == simulation::kMaximumPlayerCount);
  CHECK(maximum_snapshot.players().front().entity_id().value() == simulation::kMinimumEntityId);
  CHECK(maximum_snapshot.players().back().entity_id().value() == simulation::kMaximumPlayerCount);
  CHECK(maximum_snapshot.tick_sequence() == simulation::TickSequence::zero());
}

TEST_CASE("WorldSnapshot remains unchanged after the owning simulation commits a later tick",
          "[unit][simulation][snapshot]") {
  simulation::GameSimulation game =
      simulation_from_world(simulation::GameWorld::create({player_from_fixture(kLowerIdPlayer)}));
  const simulation::WorldSnapshot older_snapshot = game.snapshot();
  game.step(simulation::FixedDelta::canonical());
  const simulation::WorldSnapshot newer_snapshot = game.snapshot();

  REQUIRE(older_snapshot.players().size() == 1);
  REQUIRE(newer_snapshot.players().size() == 1);
  CHECK(older_snapshot.players()[0].position() ==
        simulation::Vector2::create(kLowerIdPlayer.position_x, kLowerIdPlayer.position_y));
  CHECK(older_snapshot.players()[0].velocity() ==
        simulation::Vector2::create(kLowerIdPlayer.velocity_x, kLowerIdPlayer.velocity_y));
  CHECK(newer_snapshot.tick_sequence().value() == 1);
  CHECK(newer_snapshot.players()[0].position() != older_snapshot.players()[0].position());
}

TEST_CASE("PlayerSnapshot retains signed-zero canonicality for copied motion vectors",
          "[unit][simulation][snapshot]") {
  const simulation::Vector2 canonical_zero = simulation::Vector2::create(-0.0, -0.0);
  const simulation::GameWorld world = simulation::GameWorld::create({simulation::Player::create(
      simulation::EntityId::create(1),
      simulation::PhysicsBody::create(simulation::Vector2::create(1.0, 1.0), canonical_zero,
                                      canonical_zero))});
  const simulation::WorldSnapshot snapshot = simulation_from_world(world).snapshot();

  REQUIRE(snapshot.players().size() == 1);
  CHECK(snapshot.players()[0].position() == simulation::Vector2::create(1.0, 1.0));
  CHECK_FALSE(std::signbit(snapshot.players()[0].velocity().x()));
  CHECK_FALSE(std::signbit(snapshot.players()[0].velocity().y()));
  CHECK_FALSE(std::signbit(snapshot.players()[0].acceleration().x()));
  CHECK_FALSE(std::signbit(snapshot.players()[0].acceleration().y()));
}
