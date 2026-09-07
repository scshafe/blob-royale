#include "game_simulation.hpp"

#include "candidate_pair.hpp"
#include "component_store.hpp"
#include "physics.hpp"
#include "physics_body.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace blob_royale::simulation {
namespace {

using BodyEntry = ComponentStore<PhysicsBody>::Entry;

[[nodiscard]] std::size_t body_index(const std::vector<BodyEntry>& bodies, const EntityId id) {
  const auto match = std::lower_bound(bodies.cbegin(), bodies.cend(), id,
                                      [](const BodyEntry& entry, const EntityId searched_id) {
                                        return entry.entity < searched_id;
                                      });
  if (match == bodies.cend() || match->entity != id) {
    throw std::logic_error("GameSimulation spatial-grid invariant references an unknown EntityId");
  }
  return static_cast<std::size_t>(std::distance(bodies.cbegin(), match));
}

[[nodiscard]] std::vector<BodyEntry> apply_stored_acceleration(const GameWorld& world,
                                                               const FixedDelta fixed_delta) {
  const std::span<const BodyEntry> committed_bodies = world.store<PhysicsBody>().entries();
  std::vector<BodyEntry> accelerated_bodies;
  accelerated_bodies.reserve(committed_bodies.size());
  for (const BodyEntry& entry : committed_bodies) {
    const PhysicsBody& body = entry.value;
    const Vector2 accelerated_velocity =
        integrate_accelerated_velocity(body.velocity(), body.acceleration(), fixed_delta);
    accelerated_bodies.push_back(BodyEntry{entry.entity, body.with_velocity(accelerated_velocity)});
  }
  return accelerated_bodies;
}

void resolve_player_pairs(std::vector<BodyEntry>& bodies,
                          const std::span<const CandidatePair> candidate_pairs,
                          const double player_radius) {
  for (const CandidatePair& pair : candidate_pairs) {
    const std::size_t lower_index = body_index(bodies, pair.lower_id());
    const std::size_t higher_index = body_index(bodies, pair.higher_id());
    const PhysicsBody& lower_body = bodies[lower_index].value;
    const PhysicsBody& higher_body = bodies[higher_index].value;
    const PlayerPairCollisionResult collision =
        resolve_player_pair_collision(lower_body, higher_body, player_radius);

    bodies[lower_index].value = lower_body.with_velocity(collision.first_velocity());
    bodies[higher_index].value = higher_body.with_velocity(collision.second_velocity());
  }
}

[[nodiscard]] std::vector<WallMotionResult> resolve_walls(const std::vector<BodyEntry>& bodies,
                                                          const SimulationConfig& configuration,
                                                          const FixedDelta fixed_delta) {
  std::vector<WallMotionResult> wall_motions;
  wall_motions.reserve(bodies.size());
  for (const BodyEntry& entry : bodies) {
    wall_motions.push_back(resolve_player_wall_motion(
        entry.value.position(), entry.value.velocity(), configuration.world_width(),
        configuration.world_height(), configuration.player_radius(), fixed_delta));
  }
  return wall_motions;
}

// Copies the committed world and replaces only its bodies, so every other registered component
// survives the tick without this function naming a single component kind beyond PhysicsBody.
[[nodiscard]] GameWorld integrate_world(const GameWorld& world, std::vector<BodyEntry> bodies,
                                        const std::vector<WallMotionResult>& wall_motions) {
  if (bodies.size() != wall_motions.size()) {
    throw std::logic_error("GameSimulation wall-motion invariant has an incoherent player count");
  }

  for (std::size_t index = 0; index < bodies.size(); ++index) {
    const PhysicsBody& body = bodies[index].value;
    const WallMotionResult& wall_motion = wall_motions[index];
    const Vector2 integrated_position =
        integrate_position(body.position(), wall_motion.displacement());
    const PhysicsBody terminal_body = body.with_velocity(wall_motion.terminal_velocity());
    bodies[index].value = terminal_body.with_position(integrated_position);
  }

  GameWorld integrated_world = world;
  integrated_world.mutable_store<PhysicsBody>() =
      ComponentStore<PhysicsBody>::create(std::move(bodies));
  return integrated_world;
}

} // namespace

GameSimulation GameSimulation::create(SimulationConfig configuration, GameWorld initial_world) {
  SpatialGrid initial_grid = SpatialGrid::create(configuration, initial_world);
  return GameSimulation(configuration, std::move(initial_world), std::move(initial_grid),
                        TickSequence::zero());
}

void GameSimulation::step(const FixedDelta fixed_delta) {
  const TickSequence next_tick_sequence = tick_sequence_.next();

  std::vector<BodyEntry> next_bodies = apply_stored_acceleration(world_, fixed_delta);
  const std::span<const CandidatePair> candidate_pairs = grid_.candidate_pairs();
  resolve_player_pairs(next_bodies, candidate_pairs, configuration_.player_radius());
  const std::vector<WallMotionResult> wall_motions =
      resolve_walls(next_bodies, configuration_, fixed_delta);
  GameWorld next_world = integrate_world(world_, std::move(next_bodies), wall_motions);
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
