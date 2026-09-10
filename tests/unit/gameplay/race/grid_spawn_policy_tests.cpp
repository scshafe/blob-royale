#include "race/grid_spawn_policy.hpp"

#include "gameplay_test_fixture.hpp"

#include "components/race_progress_component.hpp"
#include "components/respawn_timer_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "tick_sequence.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

const simulation::EntityId kRacer = simulation::EntityId::create(1);
constexpr std::array<bool, 4> kAllFree{true, true, true, true};
constexpr std::array<bool, 4> kAllTaken{false, false, false, false};
constexpr std::array<bool, 4> kOnlyFirstFree{true, false, false, false};

[[nodiscard]] simulation::GameWorld world_in(const simulation::MatchPhase phase,
                                             const simulation::MatchPhase previous_phase) {
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_match().phase = phase;
  world.mutable_match().previous_phase = previous_phase;
  return world;
}

[[nodiscard]] std::optional<std::size_t>
chosen_point(const simulation::GameWorld& world, const std::size_t rotation_counter = 0,
             const std::span<const bool> free_points = kAllFree) {
  const testing::TickHarness harness{simulation::TickSequence::zero()};
  return gameplay::GridSpawnPolicy{}.choose_spawn_point(world, harness.context(), kRacer,
                                                        rotation_counter, free_points);
}

} // namespace

TEST_CASE("race grid seats between matches with the shared forward probe and full-grid deferral",
          "[unit][gameplay][race][spawn]") {
  const simulation::GameWorld lobby =
      world_in(simulation::MatchPhase::kLobby, simulation::MatchPhase::kLobby);
  CHECK(chosen_point(lobby) == 0);
  CHECK(chosen_point(lobby, 2) == 2);
  CHECK(chosen_point(lobby, 3, kOnlyFirstFree) == 0);
  CHECK(chosen_point(lobby, 2, kAllTaken) == std::nullopt);
  CHECK(chosen_point(
            world_in(simulation::MatchPhase::kCountdown, simulation::MatchPhase::kLobby)) == 0);
}

TEST_CASE("race grid defers mid-race newcomers and admits only progress-zero returning racers",
          "[unit][gameplay][race][spawn]") {
  simulation::GameWorld running =
      world_in(simulation::MatchPhase::kRunning, simulation::MatchPhase::kCountdown);
  CHECK(chosen_point(running) == std::nullopt);
  running.mutable_store<simulation::RaceProgress>().insert_or_assign(kRacer,
                                                                     simulation::RaceProgress{0});
  CHECK(chosen_point(running, 2) == 2);
  CHECK(chosen_point(running, 2, kOnlyFirstFree) == 0);
  CHECK(chosen_point(running, 2, kAllTaken) == std::nullopt);
  running.mutable_store<simulation::RaceProgress>().insert_or_assign(kRacer,
                                                                     simulation::RaceProgress{1});
  CHECK(chosen_point(running) == std::nullopt);
}

TEST_CASE("race grid defers a returning racer's timer in every otherwise seatable phase",
          "[unit][gameplay][race][spawn]") {
  for (const simulation::MatchPhase phase :
       {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
        simulation::MatchPhase::kRunning}) {
    simulation::GameWorld world = world_in(phase, phase);
    world.mutable_store<simulation::RaceProgress>().insert_or_assign(kRacer,
                                                                     simulation::RaceProgress{0});
    world.mutable_store<simulation::RespawnTimer>().insert_or_assign(kRacer,
                                                                     simulation::RespawnTimer{1});
    CHECK(chosen_point(world) == std::nullopt);
    world.mutable_store<simulation::RespawnTimer>().erase(kRacer);
    CHECK(chosen_point(world) == 0);
  }
}

TEST_CASE("race grid seats nobody during ended or the next lobby's wipe tick",
          "[unit][gameplay][race][spawn]") {
  for (const simulation::MatchPhase phase :
       {simulation::MatchPhase::kEnded, simulation::MatchPhase::kLobby}) {
    simulation::GameWorld world = world_in(phase, simulation::MatchPhase::kEnded);
    world.mutable_store<simulation::RaceProgress>().insert_or_assign(kRacer,
                                                                     simulation::RaceProgress{0});
    CHECK(chosen_point(world) == std::nullopt);
  }
  CHECK(chosen_point(
            world_in(simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown)) == 0);
}
