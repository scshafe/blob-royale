#include "gameplay_test_fixture.hpp"

#include "components/hill_presence_component.hpp"
#include "components/lethal_on_contact_component.hpp"
#include "components/respawn_timer_component.hpp"
#include "components/score_component.hpp"
#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "king_of_the_hill/king_of_the_hill_mode.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

const simulation::Vector2 kRest = simulation::Vector2::create(0.0, 0.0);
const simulation::Vector2 kHillCenter = simulation::Vector2::create(480.0, 320.0);
const simulation::Vector2 kReturnPoint = simulation::Vector2::create(440.0, 320.0);
const simulation::Vector2 kOutsidePlayer = simulation::Vector2::create(800.0, 320.0);
constexpr std::uint64_t kPlayer = 1;
constexpr std::uint64_t kHazard = 3;
constexpr std::uint64_t kHill = 4;
constexpr std::int64_t kEarnedScore = 7;

// Real hill mode, contact rules, scoring, lifecycle respawn, and phase-0 seating. The hazard
// begins 19 wu from the player, inside the 20 wu contact radius at tick one's pre-integration
// contact pass. An approaching velocity keeps the overlap a physical contact, not separation.
// The free return point is 40 wu left of the hill centre and clear of the continuing hazard.
// A second participant outside the hill keeps the match running without contesting presence.
[[nodiscard]] simulation::GameSimulation knockout_return_game() {
  const simulation::SimulationConfig configuration = testing::gameplay_configuration();
  simulation::MapDefinition map = simulation::MapDefinition::create(
      "hill_zero_delay_return", simulation::ArenaBounds::create(960.0, 640.0), {},
      {simulation::MapDefinition::Marker::spawn(kReturnPoint),
       simulation::MapDefinition::Marker::spawn(kOutsidePlayer),
       simulation::MapDefinition::Marker::create("hill", kHillCenter, std::nullopt,
                                                 simulation::MapMetadata::none())},
      simulation::MapMetadata::none());
  std::vector<simulation::GameWorld::EntitySeed> seeds{
      simulation::GameWorld::EntitySeed::create(
          simulation::EntityId::create(kPlayer),
          simulation::PhysicsBody::create(kHillCenter, kRest, kRest),
          simulation::ControllerId::create(kPlayer)),
      simulation::GameWorld::EntitySeed::create(
          simulation::EntityId::create(2),
          simulation::PhysicsBody::create(kOutsidePlayer, kRest, kRest),
          simulation::ControllerId::create(2)),
      simulation::GameWorld::EntitySeed{
          simulation::EntityId::create(kHazard),
          simulation::PhysicsBody::create(simulation::Vector2::create(499.0, 320.0),
                                          simulation::Vector2::create(-1'000.0, 0.0), kRest),
          std::nullopt}};
  simulation::GameWorld world =
      simulation::GameWorld::create(configuration, map, 0, std::move(seeds));
  world.mutable_match().phase = simulation::MatchPhase::kRunning;
  world.mutable_match().previous_phase = simulation::MatchPhase::kRunning;
  world.mutable_store<simulation::LethalOnContact>().insert_or_assign(
      simulation::EntityId::create(kHazard), simulation::LethalOnContact{});
  world.mutable_store<simulation::HillPresence>().insert_or_assign(
      simulation::EntityId::create(kPlayer), simulation::HillPresence{2});
  world.mutable_store<simulation::Score>().insert_or_assign(simulation::EntityId::create(kPlayer),
                                                            simulation::Score{kEarnedScore});
  auto section = gameplay::KingOfTheHillConfiguration::default_section();
  section.hill_radius_world_units = 100.0;
  section.point_interval_seconds = 0.01; // Four ticks; the knockout tick reaches only three.
  section.points_to_win = 100;
  section.respawn_delay_seconds = 0.0;
  return simulation::GameSimulation::create(
      configuration, std::move(world),
      simulation::GameSimulationSetup::of_mode(
          std::move(map), gameplay::KingOfTheHillMode::create(
                              gameplay::KingOfTheHillConfiguration::create(section))));
}

template <typename Component>
[[nodiscard]] std::optional<Component> own_component(const simulation::WorldSnapshot& snapshot) {
  for (const auto& entry : snapshot.components<Component>()) {
    if (entry.entity.value() == kPlayer) {
      return entry.value;
    }
  }
  return std::nullopt;
}

} // namespace

TEST_CASE("hill zero-delay knockout clears partial presence before next-tick seating",
          "[unit][gameplay][king_of_the_hill][return_timing]") {
  simulation::GameSimulation game = knockout_return_game();
  game.step(testing::kGameplayFixedDelta, testing::gameplay_batch(game, {}, kHill, 1));
  const simulation::WorldSnapshot knocked_out = game.snapshot();
  REQUIRE_FALSE(testing::published_body(knocked_out, kPlayer).has_value());
  CHECK_FALSE(own_component<simulation::RespawnTimer>(knocked_out).has_value());
  CHECK_FALSE(own_component<simulation::HillPresence>(knocked_out).has_value());

  game.step(testing::kGameplayFixedDelta, testing::gameplay_batch(game, {}, kHill + 1, 1));
  const simulation::WorldSnapshot returned = game.snapshot();
  const auto body = testing::published_body(returned, kPlayer);
  REQUIRE(body.has_value());
  CHECK(body->position() == kReturnPoint);
  CHECK(body->velocity() == kRest);
  CHECK(returned.match().phase() == simulation::MatchPhase::kRunning);
  const auto presence = own_component<simulation::HillPresence>(returned);
  REQUIRE(presence.has_value());
  CHECK(presence->inside_ticks == 1);
  const auto score = own_component<simulation::Score>(returned);
  REQUIRE(score.has_value());
  CHECK(score->points == kEarnedScore);
}
