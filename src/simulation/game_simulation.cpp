#include "game_simulation.hpp"

#include "candidate_pair.hpp"
#include "physics.hpp"
#include "physics_body.hpp"
#include "player.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace blob_royale::simulation {
namespace {

[[nodiscard]] std::size_t player_index(const std::vector<Player>& players, const EntityId id) {
  const auto match = std::lower_bound(
      players.cbegin(), players.cend(), id,
      [](const Player& player, const EntityId searched_id) { return player.id() < searched_id; });
  if (match == players.cend() || match->id() != id) {
    throw std::logic_error("GameSimulation spatial-grid invariant references an unknown EntityId");
  }
  return static_cast<std::size_t>(std::distance(players.cbegin(), match));
}

[[nodiscard]] std::vector<Player> apply_stored_acceleration(const GameWorld& world,
                                                            const FixedDelta fixed_delta) {
  std::vector<Player> accelerated_players;
  accelerated_players.reserve(world.players().size());
  for (const Player& player : world.players()) {
    const PhysicsBody& body = player.body();
    const Vector2 accelerated_velocity =
        integrate_accelerated_velocity(body.velocity(), body.acceleration(), fixed_delta);
    accelerated_players.push_back(player.with_body(body.with_velocity(accelerated_velocity)));
  }
  return accelerated_players;
}

void resolve_player_pairs(std::vector<Player>& players,
                          const std::span<const CandidatePair> candidate_pairs,
                          const double player_radius) {
  for (const CandidatePair& pair : candidate_pairs) {
    const std::size_t lower_index = player_index(players, pair.lower_id());
    const std::size_t higher_index = player_index(players, pair.higher_id());
    const PhysicsBody& lower_body = players[lower_index].body();
    const PhysicsBody& higher_body = players[higher_index].body();
    const PlayerPairCollisionResult collision =
        resolve_player_pair_collision(lower_body, higher_body, player_radius);

    players[lower_index] =
        players[lower_index].with_body(lower_body.with_velocity(collision.first_velocity()));
    players[higher_index] =
        players[higher_index].with_body(higher_body.with_velocity(collision.second_velocity()));
  }
}

[[nodiscard]] std::vector<WallMotionResult> resolve_walls(const std::vector<Player>& players,
                                                          const SimulationConfig& configuration,
                                                          const FixedDelta fixed_delta) {
  std::vector<WallMotionResult> wall_motions;
  wall_motions.reserve(players.size());
  for (const Player& player : players) {
    wall_motions.push_back(resolve_player_wall_motion(
        player.body().position(), player.body().velocity(), configuration.world_width(),
        configuration.world_height(), configuration.player_radius(), fixed_delta));
  }
  return wall_motions;
}

[[nodiscard]] GameWorld integrate_world(const std::vector<Player>& players,
                                        const std::vector<WallMotionResult>& wall_motions) {
  if (players.size() != wall_motions.size()) {
    throw std::logic_error("GameSimulation wall-motion invariant has an incoherent player count");
  }

  std::vector<Player> integrated_players;
  integrated_players.reserve(players.size());
  for (std::size_t index = 0; index < players.size(); ++index) {
    const Player& player = players[index];
    const PhysicsBody& body = player.body();
    const WallMotionResult& wall_motion = wall_motions[index];
    const Vector2 integrated_position =
        integrate_position(body.position(), wall_motion.displacement());
    const PhysicsBody terminal_body = body.with_velocity(wall_motion.terminal_velocity());
    integrated_players.push_back(
        player.with_body(terminal_body.with_position(integrated_position)));
  }
  return GameWorld::create(std::move(integrated_players));
}

} // namespace

GameSimulation GameSimulation::create(SimulationConfig configuration, GameWorld initial_world) {
  SpatialGrid initial_grid = SpatialGrid::create(configuration, initial_world);
  return GameSimulation(configuration, std::move(initial_world), std::move(initial_grid),
                        TickSequence::zero());
}

void GameSimulation::step(const FixedDelta fixed_delta) {
  const TickSequence next_tick_sequence = tick_sequence_.next();

  std::vector<Player> next_players = apply_stored_acceleration(world_, fixed_delta);
  const std::span<const CandidatePair> candidate_pairs = grid_.candidate_pairs();
  resolve_player_pairs(next_players, candidate_pairs, configuration_.player_radius());
  const std::vector<WallMotionResult> wall_motions =
      resolve_walls(next_players, configuration_, fixed_delta);
  GameWorld next_world = integrate_world(next_players, wall_motions);
  SpatialGrid next_grid = grid_.rebuilt(next_world);

  // All calculations and allocations are complete. These value moves are noexcept, so the three
  // assignments form one non-throwing commit from the caller's perspective.
  world_ = std::move(next_world);
  grid_ = std::move(next_grid);
  tick_sequence_ = next_tick_sequence;
}

WorldSnapshot GameSimulation::snapshot() const {
  return WorldSnapshot::from_world(tick_sequence_, world_);
}

GameSimulation::GameSimulation(SimulationConfig configuration, GameWorld world, SpatialGrid grid,
                               const TickSequence tick_sequence) noexcept
    : configuration_(configuration), world_(std::move(world)), grid_(std::move(grid)),
      tick_sequence_(tick_sequence) {}

} // namespace blob_royale::simulation
