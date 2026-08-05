#include "entity_id.hpp"
#include "game_simulation.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "player.hpp"
#include "simulation_config.hpp"
#include "snapshot_publication.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

namespace runtime = blob_royale::runtime;
namespace simulation = blob_royale::simulation;

namespace {

constexpr simulation::EntityId::Value kPublishedPlayerId = 7;
constexpr double kPublishedPositionX = 12.0;
constexpr double kPublishedPositionY = 18.0;
constexpr double kPublishedWorldWidth = 100.0;
constexpr double kPublishedWorldHeight = 80.0;
constexpr double kPublishedPlayerRadius = 1.0;
constexpr std::uint64_t kPublishedGridColumns = 10;
constexpr std::uint64_t kPublishedGridRows = 8;

[[nodiscard]] simulation::WorldSnapshot initial_snapshot_fixture() {
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  const simulation::Player player = simulation::Player::create(
      simulation::EntityId::create(kPublishedPlayerId),
      simulation::PhysicsBody::create(
          simulation::Vector2::create(kPublishedPositionX, kPublishedPositionY), zero, zero));
  simulation::GameSimulation game_simulation = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(kPublishedWorldWidth, kPublishedWorldHeight,
                                           kPublishedPlayerRadius,
                                           simulation::SimulationConfig::kRequiredTicksPerSecond,
                                           kPublishedGridColumns, kPublishedGridRows),
      simulation::GameWorld::create({player}));
  return game_simulation.snapshot();
}

} // namespace

TEST_CASE("SnapshotPublication exposes a non-null retained immutable initial snapshot",
          "[unit][runtime][publication]") {
  const runtime::SnapshotPublication publication(initial_snapshot_fixture());

  const std::shared_ptr<const simulation::WorldSnapshot> first_read = publication.latest();
  const std::shared_ptr<const simulation::WorldSnapshot> second_read = publication.latest();

  REQUIRE(first_read);
  REQUIRE(second_read);
  CHECK_FALSE(publication.is_ready());
  CHECK(first_read == second_read);
  CHECK(first_read->tick_sequence() == simulation::TickSequence::zero());
  REQUIRE(first_read->players().size() == 1);
  CHECK(first_read->players().front().entity_id().value() == kPublishedPlayerId);
  CHECK(first_read->players().front().position() ==
        simulation::Vector2::create(kPublishedPositionX, kPublishedPositionY));
}
