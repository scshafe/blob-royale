#include "game_simulation.hpp"

#include "candidate_pair.hpp"
#include "command_registry.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "contact_rule.hpp"
#include "physics.hpp"
#include "physics_body.hpp"
#include "simulation_limits.hpp"
#include "simulation_tolerance.hpp"
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
//
// A static body is copied through untouched: a wall is never accelerated and never dragged, so
// neither equation is evaluated for it at all rather than being evaluated and happening to be an
// identity (`docs/architecture/0004-gameplay-architecture.md`
// § "Entities, components, and stores").
[[nodiscard]] std::vector<BodyEntry>
apply_stored_acceleration_and_drag(const GameWorld& world, const double drag_per_second,
                                   const FixedDelta fixed_delta) {
  const std::span<const BodyEntry> committed_bodies = world.store<PhysicsBody>().entries();
  std::vector<BodyEntry> accelerated_bodies;
  accelerated_bodies.reserve(committed_bodies.size());
  for (const BodyEntry& entry : committed_bodies) {
    const PhysicsBody& body = entry.value;
    if (body.is_static()) {
      accelerated_bodies.push_back(entry);
      continue;
    }
    const Vector2 accelerated_velocity =
        integrate_accelerated_velocity(body.velocity(), body.acceleration(), fixed_delta);
    const Vector2 dragged_velocity =
        apply_velocity_drag(accelerated_velocity, drag_per_second, fixed_delta);
    accelerated_bodies.push_back(BodyEntry{entry.entity, body.with_velocity(dragged_velocity)});
  }
  return accelerated_bodies;
}

// Phase 3. Traverses the frozen canonical pair list once, in lexicographic order, and resolves
// each admitted contact through the mode's ContactRuleTable. A resolved pair writes both bodies
// before the next pair is evaluated, so a later pair observes an earlier pair's result -- the
// sequential result ADR 0003 § "Player-pair policy" specifies rather than a simultaneous
// constraint solution.
//
// Three gates, in this fixed order:
//
//   1. the collision admission predicate, a pure integer test over the two masks;
//   2. the narrow phase, which rejects non-contacts and separating contacts. This is exactly the
//      test `resolve_player_pair_collision` applies internally, so a pair the baseline resolved to
//      "no impulse" is now skipped and left with the identical velocities it already had;
//   3. the table, walked in declared row order, canonical orientation before swapped, first match
//      wins. A pair matching no row is unchanged, which makes the table total without a default
//      row.
//
// A matched swapped row receives its arguments and its contact in row orientation, and the
// returned bodies are mapped back onto the canonical pair here.
void resolve_contacts(GameWorld& world, std::vector<BodyEntry>& bodies,
                      const std::span<const CandidatePair> candidate_pairs,
                      const ContactRuleTable& contact_rules, const TickContext& context) {
  const double player_radius = context.simulation_config().player_radius();
  for (const CandidatePair& pair : candidate_pairs) {
    const std::size_t lower_index = body_index(bodies, pair.lower_id());
    const std::size_t higher_index = body_index(bodies, pair.higher_id());
    const PhysicsBody lower_body = bodies[lower_index].value;
    const PhysicsBody higher_body = bodies[higher_index].value;

    if (!collision_masks_admit(lower_body, higher_body)) {
      continue;
    }

    const PlayerPairContact canonical_contact =
        detect_player_pair_contact(lower_body, higher_body, player_radius);
    if (!canonical_contact.is_contact() ||
        greater_than_or_approximately_equal(canonical_contact.relative_normal_speed(), 0.0,
                                            kVelocityTolerance)) {
      continue;
    }

    const std::optional<ContactRuleTable::Match> match =
        contact_rules.first_match(world, pair.lower_id(), pair.higher_id());
    if (!match.has_value()) {
      continue;
    }

    const bool swapped = match->orientation == ContactOrientation::kSwapped;
    const ContactRule::Subject row_first{swapped ? pair.higher_id() : pair.lower_id(),
                                         swapped ? higher_body : lower_body};
    const ContactRule::Subject row_second{swapped ? pair.lower_id() : pair.higher_id(),
                                          swapped ? lower_body : higher_body};
    // Detection is orientation-symmetric -- reversing the argument order negates the normal and
    // reverses two signs in each relative-velocity product -- so re-detecting in row orientation
    // costs a pure recomputation and never a different number.
    const PlayerPairContact row_contact =
        swapped ? detect_player_pair_contact(row_first.body, row_second.body, player_radius)
                : canonical_contact;

    const ContactRule& row = contact_rules.rows()[match->row_index];
    const ContactResponse response = row.response()(row_first, row_second, row_contact, context);
    if (response.replaces_bodies()) {
      bodies[swapped ? higher_index : lower_index].value = response.first_body();
      bodies[swapped ? lower_index : higher_index].value = response.second_body();
    }
    for (const WorldEvent& event : response.events()) {
      world.emit(event);
    }
  }
}

// Phase 4. The arena comes from the map, which is the single authoring home for arena size.
//
// A static body has no wall motion at all: its centre may sit on or past the disc-centre interval
// the fold is defined over, so folding it would be both meaningless and a validation failure. The
// absent motion is nullopt rather than a zero displacement, so phase 5 cannot confuse "did not
// move" with "was not moved".
[[nodiscard]] std::vector<std::optional<WallMotionResult>>
resolve_walls(const std::vector<BodyEntry>& bodies, const ArenaBounds& bounds,
              const SimulationConfig& configuration, const FixedDelta fixed_delta) {
  std::vector<std::optional<WallMotionResult>> wall_motions;
  wall_motions.reserve(bodies.size());
  for (const BodyEntry& entry : bodies) {
    if (entry.value.is_static()) {
      wall_motions.emplace_back();
      continue;
    }
    wall_motions.push_back(
        resolve_player_wall_motion(entry.value.position(), entry.value.velocity(), bounds.width(),
                                   bounds.height(), configuration.player_radius(), fixed_delta));
  }
  return wall_motions;
}

// Phase 5. Replaces only the working world's bodies, so every other registered component survives
// the tick without this function naming a single component kind beyond PhysicsBody.
void integrate_bodies_into(GameWorld& world, std::vector<BodyEntry> bodies,
                           const std::vector<std::optional<WallMotionResult>>& wall_motions) {
  if (bodies.size() != wall_motions.size()) {
    throw std::logic_error("GameSimulation wall-motion invariant has an incoherent player count");
  }

  for (std::size_t index = 0; index < bodies.size(); ++index) {
    const std::optional<WallMotionResult>& wall_motion = wall_motions[index];
    // A static body is never integrated. It keeps the position the map placed it at and the
    // velocity it was constructed with, whatever a contact rule did to the body it met.
    if (!wall_motion.has_value()) {
      continue;
    }
    const PhysicsBody& body = bodies[index].value;
    const Vector2 integrated_position =
        integrate_position(body.position(), wall_motion->displacement());
    const PhysicsBody terminal_body = body.with_velocity(wall_motion->terminal_velocity());
    bodies[index].value = terminal_body.with_position(integrated_position);
  }

  world.mutable_store<PhysicsBody>() = ComponentStore<PhysicsBody>::create(std::move(bodies));
}

// Phase 10 validation. Vector2 makes a non-finite component unconstructible, and phases 4 and 5
// fold every integrated center into the legal interval, so what remains to reject is a body a
// kPostKernel or kLifecycle system wrote outside the world after phase 6 already indexed it.
//
// The two body kinds obey different rules, which is the subtle part. A **dynamic** centre must
// keep its complete closed disc inside the arena, because that is the interval phase 4 folds into
// and the interval the broad phase indexes. A **static** centre must lie in the closed arena
// rectangle and nothing more: a wall legitimately sits on or past the disc-centre interval, and
// requiring otherwise would make the obvious boundary obstacle unrepresentable.
void require_committed_bodies_in_bounds(const GameWorld& world, const MapDefinition& map,
                                        const SimulationConfig& configuration) {
  for (const BodyEntry& entry : world.store<PhysicsBody>().entries()) {
    if (entry.value.is_static()) {
      if (map.bounds().contains(entry.value.position())) {
        continue;
      }
      throw SimulationValidationError(
          SimulationValidationCode::kGameSimulationBodyOutOfBounds,
          "game_simulation.commit.bodies[entity_id=" + std::to_string(entry.entity.value()) +
              "].position",
          "committed static body center must lie inside the closed arena rectangle");
    }
    if (map.bounds().contains_disc_center(entry.value.position(), configuration.player_radius())) {
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

// canonical: indexed_body -- the part of a body the spatial index is a function of.
//
// The index partitions the arena by **body position**, so an id plus a position is exactly what
// determines it. Comparing this and not the entity roster is what makes the rebuild decision
// correct: a stage may insert a body, destroy an entity directly rather than by emitting a
// DespawnEvent, or move one, and the first two change the roster while the third does not -- yet
// all three invalidate the index. A stage that writes only velocity or stored acceleration, which
// is what a steering system does, leaves the index correct and pays no rebuild.
struct IndexedBody final {
  EntityId entity;
  Vector2 position;

  friend bool operator==(const IndexedBody&, const IndexedBody&) = default;
};

[[nodiscard]] bool same_indexed_body(const IndexedBody& indexed, const BodyEntry& entry) noexcept {
  return indexed.entity == entry.entity && indexed.position == entry.value.position();
}

[[nodiscard]] std::vector<IndexedBody> indexed_bodies_of(const GameWorld& world) {
  const std::span<const BodyEntry> bodies = world.store<PhysicsBody>().entries();
  std::vector<IndexedBody> indexed;
  indexed.reserve(bodies.size());
  for (const BodyEntry& entry : bodies) {
    indexed.push_back(IndexedBody{entry.entity, entry.value.position()});
  }
  return indexed;
}

// Whether an index built over `indexed` is still current for this world.
[[nodiscard]] bool still_indexes(const std::vector<IndexedBody>& indexed,
                                 const GameWorld& world) noexcept {
  const std::span<const BodyEntry> bodies = world.store<PhysicsBody>().entries();
  return indexed.size() == bodies.size() &&
         std::equal(indexed.cbegin(), indexed.cend(), bodies.begin(), bodies.end(),
                    same_indexed_body);
}

// The same question between two worlds, which needs no copy because the committed world is still
// intact while the working world is built beside it.
[[nodiscard]] bool indexed_bodies_equal(const GameWorld& first, const GameWorld& second) noexcept {
  const std::span<const BodyEntry> left = first.store<PhysicsBody>().entries();
  const std::span<const BodyEntry> right = second.store<PhysicsBody>().entries();
  return std::equal(left.begin(), left.end(), right.begin(), right.end(),
                    [](const BodyEntry& first_entry, const BodyEntry& second_entry) {
                      return first_entry.entity == second_entry.entity &&
                             first_entry.value.position() == second_entry.value.position();
                    });
}

[[nodiscard]] bool stage_is_declared(const SystemPipeline& system_pipeline,
                                     const SystemStage stage) noexcept {
  return !system_pipeline.systems_at(stage).empty();
}

} // namespace

GameSimulation GameSimulation::create(SimulationConfig configuration, GameWorld initial_world) {
  return create(std::move(configuration), std::move(initial_world), SystemPipeline::empty());
}

GameSimulation GameSimulation::create(SimulationConfig configuration, GameWorld initial_world,
                                      SystemPipeline system_pipeline) {
  // The bare rectangular arena the configuration still publishes. Every caller written before maps
  // existed lands here, which is why no accepted fixture had to change to gain a map.
  MapDefinition map = MapDefinition::bare_arena(
      ArenaBounds::create(configuration.world_width(), configuration.world_height()));
  return create(std::move(configuration), std::move(map), std::move(initial_world),
                std::move(system_pipeline));
}

GameSimulation GameSimulation::create(SimulationConfig configuration, MapDefinition map,
                                      GameWorld initial_world, SystemPipeline system_pipeline) {
  return create(std::move(configuration), std::move(map), std::move(initial_world),
                std::move(system_pipeline), ContactRuleTable::built_in());
}

GameSimulation GameSimulation::create(SimulationConfig configuration, MapDefinition map,
                                      GameWorld initial_world, SystemPipeline system_pipeline,
                                      ContactRuleTable contact_rules) {
  SpatialGrid initial_grid = SpatialGrid::create(configuration, map.bounds(), initial_world);
  return GameSimulation(configuration, std::move(map), std::move(initial_world),
                        std::move(initial_grid), std::move(system_pipeline),
                        std::move(contact_rules), TickSequence::zero());
}

void GameSimulation::step(const FixedDelta fixed_delta, const InputBatch& input_batch) {
  const TickSequence next_tick_sequence = tick_sequence_.next();

  // Phase 0. Every phase and stage below reads the working world, so a failure anywhere leaves the
  // committed world, grid, and sequence exactly as the previous commit left them.
  GameWorld next_world = world_;
  apply_input_batch(next_world, input_batch);

  // The intake index: the index of the bodies phase 0 left, at this tick's start-of-tick
  // positions. The committed index already is that value whenever phase 0 changed no indexed body,
  // so rebuilding exactly when it did is both the contract and what keeps an empty batch
  // bit-identical to the accepted baseline
  // (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick").
  //
  // It is resolved before the pre-kernel stage rather than after it because this is the index
  // `TickContext::spatial_index()` promises a kPreKernel system: the bodies phase 0 left, at
  // positions nothing has moved yet.
  std::optional<SpatialGrid> reindexed_intake_grid;
  if (!indexed_bodies_equal(next_world, world_)) {
    reindexed_intake_grid = grid_.rebuilt(next_world);
  }
  const SpatialGrid& intake_grid = reindexed_intake_grid ? *reindexed_intake_grid : grid_;

  const TickContext intake_context =
      TickContext::create(next_tick_sequence, fixed_delta, configuration_, map_, intake_grid);

  // A kPreKernel system may create an entity carrying a body -- a projectile or a zone -- so the
  // index phase 2 queries has to be re-derived when the stage changed one. The snapshot is taken
  // only when the mode declared such a system at all, so a mode with an empty stage, which is
  // every accepted fixture, allocates nothing here.
  std::vector<IndexedBody> intake_indexed_bodies;
  const bool pre_kernel_declared = stage_is_declared(system_pipeline_, SystemStage::kPreKernel);
  if (pre_kernel_declared) {
    intake_indexed_bodies = indexed_bodies_of(next_world);
  }
  apply_stage(system_pipeline_, SystemStage::kPreKernel, next_world, intake_context);

  std::optional<SpatialGrid> reindexed_pair_grid;
  if (pre_kernel_declared && !still_indexes(intake_indexed_bodies, next_world)) {
    reindexed_pair_grid = intake_grid.rebuilt(next_world);
  }
  const SpatialGrid& pair_grid = reindexed_pair_grid ? *reindexed_pair_grid : intake_grid;
  const TickContext kernel_context =
      TickContext::create(next_tick_sequence, fixed_delta, configuration_, map_, pair_grid);

  // Phases 1 through 6, unchanged in content and in number.
  std::vector<BodyEntry> next_bodies =
      apply_stored_acceleration_and_drag(next_world, configuration_.drag_per_second(), fixed_delta);
  const std::span<const CandidatePair> candidate_pairs = pair_grid.candidate_pairs();
  resolve_contacts(next_world, next_bodies, candidate_pairs, contact_rules_, kernel_context);
  const std::vector<std::optional<WallMotionResult>> wall_motions =
      resolve_walls(next_bodies, map_.bounds(), configuration_, fixed_delta);
  integrate_bodies_into(next_world, std::move(next_bodies), wall_motions);
  SpatialGrid next_grid = pair_grid.rebuilt(next_world);

  // From here the index a system reads is this tick's phase 6 rebuild, over the positions phase 5
  // integrated. A stage that writes a body invalidates the very index it was handed, which is why
  // the commit re-derives it below rather than trusting that no stage wrote one.
  std::vector<IndexedBody> committed_indexed_bodies;
  const bool late_stage_declared = stage_is_declared(system_pipeline_, SystemStage::kPostKernel) ||
                                   stage_is_declared(system_pipeline_, SystemStage::kLifecycle);
  if (late_stage_declared) {
    committed_indexed_bodies = indexed_bodies_of(next_world);
  }
  const TickContext committed_context =
      TickContext::create(next_tick_sequence, fixed_delta, configuration_, map_, next_grid);
  apply_stage(system_pipeline_, SystemStage::kPostKernel, next_world, committed_context);
  apply_stage(system_pipeline_, SystemStage::kLifecycle, next_world, committed_context);

  // Phase 10, in the contract's fixed order: validate, apply this tick's DespawnEvent removals,
  // rebuild the index from the survivors, clear the event list, replace the committed state, and
  // increment the sequence once. Because validation precedes removal, removal precedes the
  // rebuild, and the rebuild precedes publication, no committed grid holds a non-live EntityId and
  // no snapshot observes a half-applied removal. The contract's signed-zero canonicalization has
  // no step here because Vector2 performs it at every construction, so no negative zero can reach
  // a body in the first place.
  require_committed_bodies_in_bounds(next_world, map_, configuration_);
  const bool roster_removed = apply_despawn_events(next_world);
  if (roster_removed ||
      (late_stage_declared && !still_indexes(committed_indexed_bodies, next_world))) {
    next_grid = next_grid.rebuilt(next_world);
  }
  next_world.clear_events();

#ifndef NDEBUG
  // The invariant the predicates above exist to maintain, checked rather than asserted in prose.
  // It costs a second full rebuild, so it is debug-only -- which is every unit-test, fixture, and
  // sanitizer run, and therefore every run that could catch a stale index.
  if (!(next_grid == next_grid.rebuilt(next_world))) {
    throw std::logic_error(
        "GameSimulation committed a spatial index that is not a rebuild of the committed world");
  }
#endif

  // All calculations and allocations are complete. These value moves are noexcept, so the three
  // assignments form one non-throwing commit from the caller's perspective.
  world_ = std::move(next_world);
  grid_ = std::move(next_grid);
  tick_sequence_ = next_tick_sequence;
}

WorldSnapshot GameSimulation::snapshot() const {
  return WorldSnapshot::from_world(tick_sequence_, world_);
}

GameSimulation::GameSimulation(SimulationConfig configuration, MapDefinition map, GameWorld world,
                               SpatialGrid grid, SystemPipeline system_pipeline,
                               ContactRuleTable contact_rules,
                               const TickSequence tick_sequence) noexcept
    : configuration_(configuration), map_(std::move(map)), world_(std::move(world)),
      grid_(std::move(grid)), system_pipeline_(std::move(system_pipeline)),
      contact_rules_(std::move(contact_rules)), tick_sequence_(tick_sequence) {}

} // namespace blob_royale::simulation
