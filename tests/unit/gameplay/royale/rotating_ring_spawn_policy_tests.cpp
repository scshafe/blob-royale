#include "royale/rotating_ring_spawn_policy.hpp"

#include "gameplay_test_fixture.hpp"

#include "entity_id.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "mode_states/royale_placements_mode_state.hpp"
#include "simulation_config.hpp"
#include "spatial_grid.hpp"
#include "tick_context.hpp"
#include "tick_sequence.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

// A world whose committed phase and `previous_phase` are set directly, because the policy's whole
// contract is a pure function of those two plus the free-point vector.
[[nodiscard]] simulation::GameWorld world_in(const simulation::MatchPhase phase,
                                             const simulation::MatchPhase previous_phase) {
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_match().phase = phase;
  // The engine field is what the policy reads. Royale's block is deliberately left at its default
  // so a policy that still read the block's mirror would see `lobby` there and fail the
  // post-`ended` case below.
  world.mutable_match().previous_phase = previous_phase;
  world.mutable_match().mode_state = simulation::RoyalePlacementsModeState{};
  return world;
}

[[nodiscard]] std::optional<std::size_t> chosen_point(const simulation::GameWorld& world,
                                                      const std::size_t rotation_counter,
                                                      const std::span<const bool> free_points) {
  const gameplay::RotatingRingSpawnPolicy policy;
  const simulation::MapDefinition map = testing::gameplay_map(free_points.size());
  const simulation::SimulationConfig configuration = testing::gameplay_configuration();
  const simulation::GameWorld empty = simulation::GameWorld::create({});
  const simulation::SpatialGrid grid =
      simulation::SpatialGrid::create(configuration, map.bounds(), empty);
  const simulation::TickContext context = simulation::TickContext::create(
      simulation::TickSequence::zero(), testing::kGameplayFixedDelta, configuration, map, grid);
  return policy.choose_spawn_point(world, context, simulation::EntityId::create(1),
                                   rotation_counter, free_points);
}

constexpr std::array<bool, 4> kAllFree{true, true, true, true};
constexpr std::array<bool, 4> kAllTaken{false, false, false, false};

} // namespace

TEST_CASE("the ring probes forward from the rotation counter and takes the first free point",
          "[unit][gameplay][royale][spawn]") {
  const simulation::GameWorld lobby =
      world_in(simulation::MatchPhase::kLobby, simulation::MatchPhase::kLobby);

  CHECK(chosen_point(lobby, 0, kAllFree) == 0);
  CHECK(chosen_point(lobby, 2, kAllFree) == 2);
  // The probe wraps, which is what spreads consecutive joiners rather than queueing them at zero.
  CHECK(chosen_point(lobby, 3, kAllFree) == 3);
  const std::array<bool, 4> only_first_free{true, false, false, false};
  CHECK(chosen_point(lobby, 2, only_first_free) == 0);
}

TEST_CASE("a full ring defers, and deferral is the only way this policy declines",
          "[unit][gameplay][royale][spawn]") {
  const simulation::GameWorld lobby =
      world_in(simulation::MatchPhase::kLobby, simulation::MatchPhase::kLobby);
  CHECK(chosen_point(lobby, 0, kAllTaken) == std::nullopt);
  CHECK(chosen_point(lobby, 3, kAllTaken) == std::nullopt);
}

TEST_CASE("a joiner is seated between matches and deferred during one",
          "[unit][gameplay][royale][spawn]") {
  // Deferring during `running` and `ended` is the mode's rule, and it is what makes a match a
  // closed field: a joiner who arrives mid-match waits for the next `lobby` rather than appearing
  // inside a shrinking circle with no chance of placing.
  CHECK(chosen_point(world_in(simulation::MatchPhase::kLobby, simulation::MatchPhase::kLobby), 0,
                     kAllFree) == 0);
  CHECK(chosen_point(world_in(simulation::MatchPhase::kCountdown, simulation::MatchPhase::kLobby),
                     0, kAllFree) == 0);
  CHECK(chosen_point(world_in(simulation::MatchPhase::kRunning, simulation::MatchPhase::kCountdown),
                     0, kAllFree) == std::nullopt);
  CHECK(chosen_point(world_in(simulation::MatchPhase::kEnded, simulation::MatchPhase::kRunning), 0,
                     kAllFree) == std::nullopt);
}

TEST_CASE("the single lobby tick that clears the arena seats nobody",
          "[unit][gameplay][royale][spawn]") {
  // The second deferral covers the one `lobby` tick on which `placement_recorder` destroys every
  // surviving entity, so the wipe cannot delete an entity the same tick just seated. Both rules
  // read the same `previous_phase`, which is why they cannot disagree about which tick that is.
  CHECK(chosen_point(world_in(simulation::MatchPhase::kLobby, simulation::MatchPhase::kEnded), 0,
                     kAllFree) == std::nullopt);
  // Every other way of reaching `lobby` -- including `countdown -> lobby`, which changes nothing
  // but the phase -- seats normally.
  CHECK(chosen_point(world_in(simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown),
                     0, kAllFree) == 0);
  CHECK(chosen_point(world_in(simulation::MatchPhase::kLobby, simulation::MatchPhase::kRunning), 0,
                     kAllFree) == 0);
}

TEST_CASE("a world holding another mode's state reads as a match that has never run",
          "[unit][gameplay][royale][spawn][mode_state]") {
  // Total rather than a fallback: the default block is exactly the state a match that has never run
  // holds, so the first tick of a royale simulation behaves identically whether the world was
  // default-constructed or arrived carrying `NoModeState`.
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_match().phase = simulation::MatchPhase::kLobby;
  CHECK(chosen_point(world, 0, kAllFree) == 0);
}
