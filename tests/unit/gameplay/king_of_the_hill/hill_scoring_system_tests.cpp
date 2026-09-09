#include "king_of_the_hill/hill_scoring_system.hpp"

#include "gameplay_test_fixture.hpp"

#include "components/controllable_component.hpp"
#include "components/hill_component.hpp"
#include "components/hill_presence_component.hpp"
#include "components/score_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "simulation_system.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] simulation::EntityId entity(const std::uint64_t id) {
  return simulation::EntityId::create(id);
}

[[nodiscard]] simulation::PhysicsBody body_at(const double x, const double y) {
  return simulation::PhysicsBody::create(simulation::Vector2::create(x, y),
                                         simulation::Vector2::create(0.0, 0.0),
                                         simulation::Vector2::create(0.0, 0.0));
}

// A running world with a hill of radius 100 at (480, 320) on entity 90, and players at the
// positions given, numbered from 1.
[[nodiscard]] simulation::GameWorld
running_world(const std::vector<simulation::Vector2>& positions) {
  std::vector<simulation::GameWorld::EntitySeed> seeds;
  for (std::size_t index = 0; index < positions.size(); ++index) {
    seeds.push_back(simulation::GameWorld::EntitySeed::create(
        entity(index + 1), body_at(positions[index].x(), positions[index].y()),
        simulation::ControllerId::create(index + 1)));
  }
  simulation::GameWorld world = simulation::GameWorld::create(std::move(seeds));
  world.mutable_store<simulation::Hill>().insert_or_assign(
      entity(90), simulation::Hill{simulation::Vector2::create(480.0, 320.0), 100.0});
  world.mutable_match().phase = simulation::MatchPhase::kRunning;
  return world;
}

[[nodiscard]] gameplay::KingOfTheHillConfiguration configuration(const double interval_seconds,
                                                                 const bool contested) {
  gameplay::KingOfTheHillConfiguration::Section section =
      gameplay::KingOfTheHillConfiguration::default_section();
  section.point_interval_seconds = interval_seconds;
  section.contested_hill_scores = contested;
  return gameplay::KingOfTheHillConfiguration::create(section);
}

void score(simulation::GameWorld& world, const gameplay::KingOfTheHillConfiguration& configuration,
           const std::size_t ticks = 1) {
  const std::unique_ptr<const simulation::SimulationSystem> scoring =
      gameplay::HillScoringSystem::create(configuration);
  const testing::TickHarness harness{simulation::TickSequence::create(50)};
  for (std::size_t tick = 0; tick < ticks; ++tick) {
    scoring->apply(world, harness.context());
  }
}

[[nodiscard]] std::uint64_t presence_of(const simulation::GameWorld& world,
                                        const std::uint64_t id) {
  const simulation::HillPresence* presence =
      world.store<simulation::HillPresence>().find(entity(id));
  return presence == nullptr ? 0 : presence->inside_ticks;
}

[[nodiscard]] std::int64_t score_of(const simulation::GameWorld& world, const std::uint64_t id) {
  const simulation::Score* score = world.store<simulation::Score>().find(entity(id));
  return score == nullptr ? 0 : score->points;
}

const simulation::Vector2 kInside = simulation::Vector2::create(500.0, 320.0);
const simulation::Vector2 kOutside = simulation::Vector2::create(700.0, 320.0);
// Exactly on the rim, which is inside, consistent with the zone and the baseline's inclusive rule.
const simulation::Vector2 kRim = simulation::Vector2::create(580.0, 320.0);

} // namespace

TEST_CASE("presence counts up inside the hill and rolls over into one point at the interval",
          "[unit][gameplay][king_of_the_hill][scoring]") {
  // I = 4 ticks at 0.01 s.
  simulation::GameWorld world = running_world({kInside});
  const gameplay::KingOfTheHillConfiguration four_ticks = configuration(0.01, false);

  score(world, four_ticks);
  CHECK(presence_of(world, 1) == 1);
  CHECK(score_of(world, 1) == 0);
  score(world, four_ticks, 2);
  CHECK(presence_of(world, 1) == 3);
  score(world, four_ticks);
  // The fourth tick reaches the interval: one point, and the counter is erased rather than reset.
  CHECK(score_of(world, 1) == 1);
  CHECK(world.store<simulation::HillPresence>().find(entity(1)) == nullptr);
  score(world, four_ticks, 4);
  CHECK(score_of(world, 1) == 2);
}

TEST_CASE("a zero interval scores on the first inside tick, because the increment precedes the "
          "test",
          "[unit][gameplay][king_of_the_hill][scoring]") {
  simulation::GameWorld world = running_world({kInside});
  score(world, configuration(0.0, false), 3);
  CHECK(score_of(world, 1) == 3);
  CHECK(world.store<simulation::HillPresence>().entries().empty());
}

TEST_CASE("a centre on the rim is inside and one beyond it is not; leaving loses the partial point",
          "[unit][gameplay][king_of_the_hill][scoring]") {
  simulation::GameWorld world = running_world({kRim, kOutside});
  const gameplay::KingOfTheHillConfiguration slow = configuration(1.0, false);

  score(world, slow, 3);
  CHECK(presence_of(world, 1) == 3);
  CHECK(presence_of(world, 2) == 0);
  CHECK(world.store<simulation::HillPresence>().find(entity(2)) == nullptr);

  // Player 1 steps off the rim: the three ticks of progress are gone, not banked.
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity(1), body_at(700.0, 320.0));
  score(world, slow);
  CHECK(world.store<simulation::HillPresence>().find(entity(1)) == nullptr);
  CHECK(score_of(world, 1) == 0);
}

TEST_CASE("a contested hill scores nobody and freezes progress; the free-for-all variant scores "
          "everyone",
          "[unit][gameplay][king_of_the_hill][scoring]") {
  simulation::GameWorld world = running_world({kInside, kRim});
  const gameplay::KingOfTheHillConfiguration exclusive = configuration(0.01, false);

  // Two inside: nothing moves.
  score(world, exclusive, 3);
  CHECK(presence_of(world, 1) == 0);
  CHECK(presence_of(world, 2) == 0);

  // Player 2 leaves; player 1 alone holds the hill and counts.
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity(2), body_at(700.0, 320.0));
  score(world, exclusive, 2);
  CHECK(presence_of(world, 1) == 2);

  // Player 2 returns: player 1's two ticks are frozen, neither gained nor lost.
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity(2), body_at(500.0, 300.0));
  score(world, exclusive, 5);
  CHECK(presence_of(world, 1) == 2);
  CHECK(presence_of(world, 2) == 0);

  // With contested scoring on, both count.
  score(world, configuration(0.01, true), 2);
  CHECK(score_of(world, 1) == 1);
  CHECK(presence_of(world, 2) == 2);
}

TEST_CASE("being knocked out forgets the partial point but keeps the score",
          "[unit][gameplay][king_of_the_hill][scoring]") {
  simulation::GameWorld world = running_world({kInside});
  const gameplay::KingOfTheHillConfiguration four_ticks = configuration(0.01, false);
  score(world, four_ticks, 6);
  CHECK(score_of(world, 1) == 1);
  CHECK(presence_of(world, 1) == 2);

  // The shape the shared respawn leaves behind: the controller stays, the body is gone.
  world.mutable_store<simulation::PhysicsBody>().erase(entity(1));
  score(world, four_ticks);
  CHECK(world.store<simulation::HillPresence>().find(entity(1)) == nullptr);
  CHECK(score_of(world, 1) == 1);
}

TEST_CASE("scoring is evaluated only while the committed phase is running",
          "[unit][gameplay][king_of_the_hill][scoring]") {
  for (const simulation::MatchPhase phase :
       {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
        simulation::MatchPhase::kEnded}) {
    INFO("phase " << simulation::match_phase_name(phase));
    simulation::GameWorld world = running_world({kInside});
    world.mutable_match().phase = phase;
    score(world, configuration(0.0, false), 3);
    CHECK(score_of(world, 1) == 0);
    CHECK(world.store<simulation::HillPresence>().entries().empty());
  }
}

TEST_CASE("a running world with no hill is a rejection naming the missing declaration",
          "[unit][gameplay][king_of_the_hill][scoring][validation]") {
  simulation::GameWorld world = running_world({kInside});
  world.mutable_store<simulation::Hill>().erase(entity(90));
  try {
    score(world, configuration(1.0, false));
    FAIL("a running match with no hill scored");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() == gameplay::GameplayValidationCode::kKingOfTheHillHillAbsent);
    CHECK(error.code() == std::string_view{"GAMEPLAY.KING_OF_THE_HILL_HILL_ABSENT"});
    CHECK(error.context() == "hill_scoring.hill");
  }
}
