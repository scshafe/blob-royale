#include "shared/next_free_spawn_point_policy.hpp"

#include "gameplay_test_fixture.hpp"

#include "components/controllable_component.hpp"
#include "components/respawn_timer_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
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

[[nodiscard]] simulation::GameWorld world_in(const simulation::MatchPhase phase,
                                             const simulation::MatchPhase previous_phase) {
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_match().phase = phase;
  world.mutable_match().previous_phase = previous_phase;
  return world;
}

[[nodiscard]] std::optional<std::size_t> chosen_point(const simulation::GameWorld& world,
                                                      const std::size_t rotation_counter,
                                                      const std::span<const bool> free_points) {
  const gameplay::NextFreeSpawnPointPolicy policy;
  const testing::TickHarness harness{simulation::TickSequence::create(1),
                                     testing::gameplay_map(free_points.size())};
  return policy.choose_spawn_point(world, harness.context(), simulation::EntityId::create(1),
                                   rotation_counter, free_points);
}

constexpr std::array<bool, 4> kAllFree{true, true, true, true};
constexpr std::array<bool, 4> kAllTaken{false, false, false, false};

} // namespace

TEST_CASE("the open-field policy seats in every phase, probing forward from the counter",
          "[unit][gameplay][shared][spawn]") {
  for (const simulation::MatchPhase phase : simulation::kMatchPhases) {
    INFO("phase " << simulation::match_phase_name(phase));
    const simulation::GameWorld world = world_in(phase, simulation::MatchPhase::kLobby);
    CHECK(chosen_point(world, 0, kAllFree) == 0);
    CHECK(chosen_point(world, 2, kAllFree) == 2);
    CHECK(chosen_point(world, 3, kAllTaken) == std::nullopt);
  }
}

TEST_CASE("the single lobby tick after ended seats nobody, so a wipe never meets a seating",
          "[unit][gameplay][shared][spawn]") {
  CHECK(chosen_point(world_in(simulation::MatchPhase::kLobby, simulation::MatchPhase::kEnded), 0,
                     kAllFree) == std::nullopt);
  // Every other way of reaching `lobby` seats normally, including `countdown -> lobby`.
  CHECK(chosen_point(world_in(simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown),
                     0, kAllFree) == 0);
  CHECK(chosen_point(world_in(simulation::MatchPhase::kLobby, simulation::MatchPhase::kRunning), 0,
                     kAllFree) == 0);
  // And `ended` itself, or a match that just ended and is still in `ended`, is not the wipe tick.
  CHECK(chosen_point(world_in(simulation::MatchPhase::kEnded, simulation::MatchPhase::kRunning), 0,
                     kAllFree) == 0);
}

TEST_CASE("an entity still counting down its respawn is deferred whatever the phase",
          "[unit][gameplay][shared][spawn]") {
  // The engine offers it because it has a controller and no body; this is the one predicate that
  // says "not yet". The moment the timer is erased, the same entity is seated.
  for (const simulation::MatchPhase phase : simulation::kMatchPhases) {
    INFO("phase " << simulation::match_phase_name(phase));
    simulation::GameWorld world = world_in(phase, simulation::MatchPhase::kLobby);
    world.mutable_store<simulation::Controllable>().insert_or_assign(
        simulation::EntityId::create(1),
        simulation::Controllable{simulation::ControllerId::create(1)});
    world.mutable_store<simulation::RespawnTimer>().insert_or_assign(
        simulation::EntityId::create(1), simulation::RespawnTimer{5});
    CHECK(chosen_point(world, 0, kAllFree) == std::nullopt);

    world.mutable_store<simulation::RespawnTimer>().erase(simulation::EntityId::create(1));
    CHECK(chosen_point(world, 0, kAllFree) == 0);
  }
}
