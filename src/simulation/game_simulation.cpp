#include "game_simulation.hpp"

#include "candidate_pair.hpp"
#include "command_registry.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "physics.hpp"
#include "physics_body.hpp"
#include "simulation_validation_error.hpp"
#include "tick_context.hpp"
#include "world_event_registry.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
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

// The entity a command is recorded against. A spawn addresses a ControllerId and asks the engine
// to create an entity, so it is recorded against none; every other kind names the EntityId it
// addresses. Adding a kind adds its arm here.
[[nodiscard]] std::optional<EntityId> recorded_entity_of(const Command& command) noexcept {
  return std::visit(
      []<typename CommandType>(const CommandType& value) -> std::optional<EntityId> {
        if constexpr (std::is_same_v<CommandType, SpawnCommand>) {
          return std::nullopt;
        } else {
          return value.entity;
        }
      },
      command);
}

// Phase 0. The batch arrives canonical -- despawns, then spawns, then every remaining kind, each
// group ascending by the identity it addresses -- so this is one forward pass that neither sorts,
// de-duplicates, nor range-checks (`input_batch.hpp`).
//
// A despawn destroys its entity before any pair is built. A spawn is deliberately a no-op here:
// seating an unseated entity needs the map's spawn markers and the mode's SpawnPolicy, which
// arrive in Step 19; until then a spawn command changes nothing at all rather than seating an
// entity at a position no declaration chose. A command of any remaining kind is recorded into its
// entity's Controllable and is **not interpreted**, because command meaning is a system's job
// (`docs/architecture/0004-gameplay-architecture.md` § "Commands").
//
// A command that disagrees with committed world state -- a despawn for an entity that does not
// exist, a command recorded for an entity that is not live -- is ignored rather than failing the
// tick, because the command source is a network session and a hard failure would let one client
// stop the match (`docs/architecture/0003-deterministic-simulation-contract.md`
// § "Accepted simulation input").
void apply_input_batch(GameWorld& world, const InputBatch& input_batch) {
  // Last tick's recorded commands are cleared in place rather than by reconstructing the
  // component, so each entity's vector keeps its capacity across ticks.
  for (ComponentStore<Controllable>::Entry& entry :
       world.mutable_store<Controllable>().mutable_entries()) {
    entry.value.commands_this_tick.clear();
  }
  if (input_batch.commands().empty()) {
    return;
  }

  for (const Command& command : input_batch.commands()) {
    if (const auto* despawn = std::get_if<DespawnCommand>(&command); despawn != nullptr) {
      world.destroy_entity(despawn->entity);
      continue;
    }
    // A spawn command is a no-op in this step and changes nothing at all. Seating an unseated
    // entity needs the map's spawn markers and the mode's SpawnPolicy, which arrive in Step 19;
    // until then seating it anywhere would be a position no declaration chose.
    const std::optional<EntityId> recorded_entity = recorded_entity_of(command);
    if (!recorded_entity.has_value()) {
      continue;
    }
    if (Controllable* controllable =
            world.mutable_store<Controllable>().mutable_find(*recorded_entity);
        controllable != nullptr) {
      controllable->commands_this_tick.push_back(command);
    }
  }

  // The batch groups kinds by phase 0 application rank -- despawn, spawn, then ascending
  // enumerator -- while a Controllable records them in ascending CommandKind. The two orders agree
  // while exactly one kind is recordable, so this sort is a no-op today; writing it is what makes
  // the recorded order a stated contract rather than a coincidence of the rank table.
  for (ComponentStore<Controllable>::Entry& entry :
       world.mutable_store<Controllable>().mutable_entries()) {
    if (entry.value.commands_this_tick.size() < 2) {
      continue;
    }
    std::sort(entry.value.commands_this_tick.begin(), entry.value.commands_this_tick.end(),
              [](const Command& left, const Command& right) {
                return command_kind_of(left) < command_kind_of(right);
              });
  }
}

void apply_stage(const SystemPipeline& system_pipeline, const SystemStage stage, GameWorld& world,
                 const TickContext& context) {
  for (const SystemPipeline::StagedSystem& staged : system_pipeline.systems_at(stage)) {
    staged.system->apply(world, context);
  }
}

// Phase 1. Semi-implicit Euler on the stored acceleration, then the drag factor
// `max(0, 1 - drag_per_second * dt)` on the accelerated velocity, both in ascending EntityId
// order because a ComponentStore's entries are ascending by construction. At
// `drag_per_second = 0` the factor is exactly 1.0 and multiplication by 1.0 is the identity on
// every finite binary64 value, so this reproduces the accepted baseline bit-for-bit.
[[nodiscard]] std::vector<BodyEntry>
apply_stored_acceleration_and_drag(const GameWorld& world, const double drag_per_second,
                                   const FixedDelta fixed_delta) {
  const std::span<const BodyEntry> committed_bodies = world.store<PhysicsBody>().entries();
  std::vector<BodyEntry> accelerated_bodies;
  accelerated_bodies.reserve(committed_bodies.size());
  for (const BodyEntry& entry : committed_bodies) {
    const PhysicsBody& body = entry.value;
    const Vector2 accelerated_velocity =
        integrate_accelerated_velocity(body.velocity(), body.acceleration(), fixed_delta);
    const Vector2 dragged_velocity =
        apply_velocity_drag(accelerated_velocity, drag_per_second, fixed_delta);
    accelerated_bodies.push_back(BodyEntry{entry.entity, body.with_velocity(dragged_velocity)});
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

// Phase 5. Replaces only the working world's bodies, so every other registered component survives
// the tick without this function naming a single component kind beyond PhysicsBody.
void integrate_bodies_into(GameWorld& world, std::vector<BodyEntry> bodies,
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

  world.mutable_store<PhysicsBody>() = ComponentStore<PhysicsBody>::create(std::move(bodies));
}

// Phase 10 validation. Vector2 makes a non-finite component unconstructible, and phases 4 and 5
// fold every integrated center into the legal interval, so what remains to reject is a body a
// kPostKernel or kLifecycle system wrote outside the world after phase 6 already indexed it.
void require_committed_bodies_in_bounds(const GameWorld& world,
                                        const SimulationConfig& configuration) {
  for (const BodyEntry& entry : world.store<PhysicsBody>().entries()) {
    if (configuration.contains_player_center(entry.value.position())) {
      continue;
    }
    throw SimulationValidationError(
        SimulationValidationCode::kGameSimulationBodyOutOfBounds,
        "game_simulation.commit.bodies[entity_id=" + std::to_string(entry.entity.value()) +
            "].position",
        "committed center must keep the complete closed disc inside the world bounds");
  }
}

// Phase 10 roster removal. Returns whether the roster changed, because the derived spatial index
// has to be rebuilt from the survivors exactly when it did.
[[nodiscard]] bool apply_despawn_events(GameWorld& world) {
  // The named ids are collected before any destruction so the walk never reads the event list
  // while the world it belongs to is being mutated.
  std::vector<EntityId> removed_entities;
  for (const WorldEvent& event : world.events()) {
    if (const auto* despawn = std::get_if<DespawnEvent>(&event);
        despawn != nullptr && world.contains(despawn->entity)) {
      removed_entities.push_back(despawn->entity);
    }
  }
  for (const EntityId entity : removed_entities) {
    world.destroy_entity(entity);
  }
  return !removed_entities.empty();
}

[[nodiscard]] bool roster_equals(const GameWorld& first, const GameWorld& second) noexcept {
  return std::equal(first.entities().begin(), first.entities().end(), second.entities().begin(),
                    second.entities().end());
}

} // namespace

GameSimulation GameSimulation::create(SimulationConfig configuration, GameWorld initial_world) {
  return create(std::move(configuration), std::move(initial_world), SystemPipeline::empty());
}

GameSimulation GameSimulation::create(SimulationConfig configuration, GameWorld initial_world,
                                      SystemPipeline system_pipeline) {
  SpatialGrid initial_grid = SpatialGrid::create(configuration, initial_world);
  return GameSimulation(configuration, std::move(initial_world), std::move(initial_grid),
                        std::move(system_pipeline), TickSequence::zero());
}

void GameSimulation::step(const FixedDelta fixed_delta, const InputBatch& input_batch) {
  const TickSequence next_tick_sequence = tick_sequence_.next();
  const TickContext context = TickContext::create(next_tick_sequence, fixed_delta, configuration_);

  // Phase 0. Every phase and stage below reads the working world, so a failure anywhere leaves the
  // committed world, grid, and sequence exactly as the previous commit left them.
  GameWorld next_world = world_;
  apply_input_batch(next_world, input_batch);

  apply_stage(system_pipeline_, SystemStage::kPreKernel, next_world, context);

  // Phase 2 queries the index of the roster phase 0 left, at this tick's start-of-tick positions.
  // The committed index already is that value whenever the roster did not change, so rebuilding
  // exactly when it did is both the contract and what keeps an empty batch bit-identical to the
  // accepted baseline (`docs/architecture/0003-deterministic-simulation-contract.md`
  // § "Canonical tick").
  std::optional<SpatialGrid> reindexed_intake_grid;
  if (!roster_equals(next_world, world_)) {
    reindexed_intake_grid = grid_.rebuilt(next_world);
  }
  const SpatialGrid& intake_grid = reindexed_intake_grid ? *reindexed_intake_grid : grid_;

  // Phases 1 through 6, unchanged in content and in number.
  std::vector<BodyEntry> next_bodies =
      apply_stored_acceleration_and_drag(next_world, configuration_.drag_per_second(), fixed_delta);
  const std::span<const CandidatePair> candidate_pairs = intake_grid.candidate_pairs();
  resolve_player_pairs(next_bodies, candidate_pairs, configuration_.player_radius());
  const std::vector<WallMotionResult> wall_motions =
      resolve_walls(next_bodies, configuration_, fixed_delta);
  integrate_bodies_into(next_world, std::move(next_bodies), wall_motions);
  SpatialGrid next_grid = intake_grid.rebuilt(next_world);

  apply_stage(system_pipeline_, SystemStage::kPostKernel, next_world, context);
  apply_stage(system_pipeline_, SystemStage::kLifecycle, next_world, context);

  // Phase 10, in the contract's fixed order: validate, apply this tick's DespawnEvent removals,
  // rebuild the index from the survivors, clear the event list, replace the committed state, and
  // increment the sequence once. Because validation precedes removal, removal precedes the
  // rebuild, and the rebuild precedes publication, no committed grid holds a non-live EntityId and
  // no snapshot observes a half-applied removal. The contract's signed-zero canonicalization has
  // no step here because Vector2 performs it at every construction, so no negative zero can reach
  // a body in the first place.
  require_committed_bodies_in_bounds(next_world, configuration_);
  if (apply_despawn_events(next_world)) {
    next_grid = next_grid.rebuilt(next_world);
  }
  next_world.clear_events();

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
                               SystemPipeline system_pipeline,
                               const TickSequence tick_sequence) noexcept
    : configuration_(configuration), world_(std::move(world)), grid_(std::move(grid)),
      system_pipeline_(std::move(system_pipeline)), tick_sequence_(tick_sequence) {}

} // namespace blob_royale::simulation
