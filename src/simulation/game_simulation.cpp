#include "game_simulation.hpp"

#include "candidate_pair.hpp"
#include "command_registry.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "contact_rule.hpp"
#include "idle_match_objective.hpp"
#include "idle_spawn_policy.hpp"
#include "match_lifecycle_system.hpp"
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
#include <memory>
#include <optional>
#include <span>
#include <string>
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
    throw SimulationValidationError(
        SimulationValidationCode::kGameSimulationSpatialIndexUnknownEntityId,
        "game_simulation.contacts.pairs[entity_id=" + std::to_string(id.value()) + "]",
        "the spatial index named an EntityId the committed body store does not hold");
  }
  return static_cast<std::size_t>(std::distance(bodies.cbegin(), match));
}

// Phase 0. The batch arrives canonical -- despawns, then spawns, then every remaining kind, each
// group ascending by the identity it addresses -- so this is one forward pass that neither sorts,
// de-duplicates, nor range-checks (`input_batch.hpp`).
//
// A despawn destroys its entity before any pair is built. **A spawn draws an EntityId from this
// tick's reservation and writes the Controllable that links it to the asking controller**, which
// brings the entity into existence carrying no body: it is *unseated*, and the SpawnSystem that
// runs immediately after this pass offers it to the mode's SpawnPolicy. Drawing from the tick's
// reservation is what makes a replayed command log reproduce simulation-created ids exactly, and
// exhausting the reservation is a hard failure rather than a silent skip
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick").
//
// A command of any remaining kind is recorded into its entity's Controllable and is **not
// interpreted**, because command meaning is a system's job
// (`docs/architecture/0004-gameplay-architecture.md` § "Commands").
//
// A command that disagrees with committed world state -- a despawn for an entity that does not
// exist, a command recorded for an entity that is not live -- is ignored rather than failing the
// tick, because the command source is a network session and a hard failure would let one client
// stop the match (`docs/architecture/0003-deterministic-simulation-contract.md`
// § "Accepted simulation input").
// True when some live entity's Controllable already names this controller. A controller drives at
// most one body at a time, so a spawn for one that already has a body is a duplicate.
[[nodiscard]] bool controller_holds_a_body(const GameWorld& world, const ControllerId controller) {
  for (const auto& entry : world.store<Controllable>().entries()) {
    if (entry.value.controller_id == controller) {
      return true;
    }
  }
  return false;
}

void apply_input_batch(GameWorld& world, const InputBatch& input_batch) {
  // Last tick's recorded commands are cleared in place rather than by reconstructing the
  // component, so each entity's vector keeps its capacity across ticks.
  for (Controllable& controllable : world.mutable_store<Controllable>().mutable_values()) {
    controllable.commands_this_tick.clear();
  }
  if (input_batch.commands().empty()) {
    return;
  }

  for (const Command& command : input_batch.commands()) {
    if (const auto* despawn = std::get_if<DespawnCommand>(&command); despawn != nullptr) {
      world.destroy_entity(despawn->entity);
      continue;
    }
    if (const auto* spawn = std::get_if<SpawnCommand>(&command); spawn != nullptr) {
      // A controller drives at most one body. `InputBatch` collapses repeated spawns *within* one
      // tick, but nothing stopped a source from spawning again in a later tick, so a session or a
      // bot that asked twice got two blobs -- steering one and abandoning the other in the arena.
      // Ignored rather than rejected, exactly as ADR 0005 § "Roster edge rules" says a duplicate
      // spawn is, because the source is an untrusted session and a hard failure would let one
      // client stop the match.
      if (controller_holds_a_body(world, spawn->controller)) {
        continue;
      }
      const EntityId created = world.create_entity();
      world.mutable_store<Controllable>().insert_or_assign(created,
                                                           Controllable{spawn->controller});
      continue;
    }
    // Total over the closed variant: the only kind that addresses no entity is the spawn handled
    // above, so this guard is unreachable today and is what keeps the pass correct the day a kind
    // that addresses something other than an EntityId is registered
    // (`command_registry.hpp`, AddressedIdentity).
    const std::optional<EntityId> recorded_entity = addressed_identity_of(command).entity();
    if (!recorded_entity.has_value()) {
      continue;
    }
    if (Controllable* controllable =
            world.mutable_store<Controllable>().mutable_find(*recorded_entity);
        controllable != nullptr) {
      // Appended in batch order, and that is the whole ordering rule. The batch already arrives in
      // phase 0's application order, so **one canonical order governs one command list** and this
      // pass neither sorts nor regroups. A second convention -- re-sorting each entity's recorded
      // list by ascending CommandKind -- used to live here; it was a no-op over the rank table it
      // claimed to be independent of, and two orderings over one closed vocabulary is a
      // disagreement waiting for a fourth kind (engine review finding 6).
      controllable->commands_this_tick.push_back(command);
    }
  }
}

void apply_stage(const SystemPipeline& system_pipeline, const SystemStage stage, GameWorld& world,
                 const TickContext& context) {
  for (const SystemPipeline::StagedSystem& staged : system_pipeline.systems_at(stage)) {
    staged.system->apply(world, context);
  }
}

// Phase 1. Semi-implicit Euler on the stored acceleration, then the drag factor
// `max(0, 1 - drag_per_second * drag_scale * dt)` on the accelerated velocity, both in ascending
// EntityId order because a ComponentStore's entries are ascending by construction. At
// `drag_per_second = 0` the factor is exactly 1.0 and multiplication by 1.0 is the identity on
// every finite binary64 value, so this reproduces the accepted baseline bit-for-bit.
//
// **The body's scale enters before the equation and changes nothing else about it.** The kernel
// still owns drag, still applies it here, and no system reproduces or bypasses it; it reads a
// coefficient from the body the way phase 3 already reads a mass
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick" phase 1, as
// amended 2026-09-07). The product is formed first and the rest of the operation order --
// `* dt`, then `1.0 -`, then `max(0, ...)`, then the componentwise scale -- is untouched, which is
// what makes this an addition rather than a versioned change: every body that exists carries
// `kDefaultDragScale`, multiplication by `1.0` is exact in binary64 for every finite value, so
// `drag_per_second * 1.0` **is** `drag_per_second` bit for bit and every committed velocity is the
// value it always was. A body declaring `0.0` coasts, which is what an object crossing the arena
// needs: the factor is geometric, so total travel under drag is exactly `speed / drag_per_second`
// and a hazard would otherwise stall a fraction of the way in.
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
        apply_velocity_drag(accelerated_velocity, drag_per_second * body.drag_scale(), fixed_delta);
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
//
// **The gate measures each pair at its own contact distance, `r_a + r_b`.** This is an addition and
// not an amendment, and the proof is arithmetic rather than argument: every body that exists today
// has an effective radius equal to the configured one -- either it declares that radius or it
// declares none and `effective_radius` defers to it -- so `pair_contact_distance` returns
// `configured + configured`, and `x + x` is exactly `2 * x` in binary64 for every finite `x`,
// with no rounding at any magnitude. The gate therefore admits and rejects exactly the pairs it
// always did, and the accepted fixtures and the baseline oracle are untouched. What changes is only
// a body that declares a *different* radius, which nothing did before this.
//
// **Why the narrow phase moved and the wall did not.** This gate and the broad phase both answer
// "can these two discs touch", a question about the pair's own geometry, so both take the pair's
// own radii. Phase 4's arena fold and phase 10's disc-centre interval answer "where may this body's
// centre be committed", which is the accepted `[r, extent - r]` interval of
// `docs/architecture/0003-deterministic-simulation-contract.md` § "Wall policy" -- moving *that* to
// a per-body radius changes where every existing body may stand, which is the growing-blob change
// and a versioned physics amendment. The asymmetry is deliberate: contact is per-pair, the arena is
// per-configuration.
void resolve_contacts(GameWorld& world, std::vector<BodyEntry>& bodies,
                      const std::span<const CandidatePair> candidate_pairs,
                      const ContactRuleTable& contact_rules, const TickContext& context) {
  const double configured_radius = context.simulation_config().player_radius();
  for (const CandidatePair& pair : candidate_pairs) {
    const std::size_t lower_index = body_index(bodies, pair.lower_id());
    const std::size_t higher_index = body_index(bodies, pair.higher_id());
    const PhysicsBody lower_body = bodies[lower_index].value;
    const PhysicsBody higher_body = bodies[higher_index].value;

    if (!collision_masks_admit(lower_body, higher_body)) {
      continue;
    }

    // One contact distance for both orientations. Addition is commutative in binary64, so
    // `r_a + r_b` and `r_b + r_a` are the same value, but computing it once says so structurally.
    const double contact_distance =
        pair_contact_distance(lower_body, higher_body, configured_radius);
    const PlayerPairContact canonical_contact =
        detect_pair_contact(lower_body, higher_body, contact_distance);
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
        swapped ? detect_pair_contact(row_first.body, row_second.body, contact_distance)
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
// The fold measures with `SimulationConfig::player_radius()` and keeps doing so, unlike the narrow
// phase and the broad phase: see the note on `require_committed_bodies_in_bounds` below for why
// contact moved to a per-body radius while the arena interval did not.
//
// A static body has no wall motion at all: its centre may sit on or past the disc-centre interval
// the fold is defined over, so folding it would be both meaningless and a validation failure. The
// absent motion is nullopt rather than a zero displacement, so phase 5 cannot confuse "did not
// move" with "was not moved".
//
// A **crossing** body -- one whose `BoundsBehavior` is `kCross` -- has a motion but no walls: it
// takes `resolve_unbounded_motion`, which is the proposed `velocity * dt` unfolded and the velocity
// unchanged. That is the whole of "phase 4 must not fold it", and it is a different answer from the
// static body's nullopt because a hazard that crossed the screen without moving would not be a
// hazard. Every body that does not say otherwise still folds by exactly the accepted equation.
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
    if (entry.value.crosses_bounds()) {
      wall_motions.push_back(resolve_unbounded_motion(entry.value.velocity(), fixed_delta));
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
    throw SimulationValidationError(SimulationValidationCode::kGameSimulationWallMotionIncoherent,
                                    "game_simulation.integrate.wall_motions",
                                    "the wall-motion list holds " +
                                        std::to_string(wall_motions.size()) + " results for " +
                                        std::to_string(bodies.size()) + " bodies");
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
// The body kinds obey different rules, which is the subtle part. A **dynamic** centre must
// keep its complete closed disc inside the arena, because that is the interval phase 4 folds into
// and the interval the broad phase indexes. A **static** centre must lie in the closed arena
// rectangle and nothing more: a wall legitimately sits on or past the disc-centre interval, and
// requiring otherwise would make the obvious boundary obstacle unrepresentable. A **crossing**
// centre is unconstrained: phase 4 applied its proposed motion unfolded, so rejecting it here for
// being outside would reject exactly the motion the body declares. It is not unchecked -- Vector2
// makes a non-finite component unconstructible and phase 4 already rejected a non-finite endpoint
// -- it is simply not held to an interval it opted out of.
//
// **This interval stays on the configured radius even though the narrow phase and the broad phase
// now measure contact per body, and that is deliberate rather than an oversight.** The interval is
// `[r, extent - r]` from ADR 0003 § "Wall policy", the same one phase 4 folds into; giving it a
// per-body radius would change where every existing body may stand and would regenerate every
// accepted wall fixture, which is the growing-blob change and a versioned physics amendment.
// Contact is a question about a pair's own geometry; the arena is a question about the
// configuration.
void require_committed_bodies_in_bounds(const GameWorld& world, const MapDefinition& map,
                                        const SimulationConfig& configuration) {
  for (const BodyEntry& entry : world.store<PhysicsBody>().entries()) {
    if (entry.value.crosses_bounds()) {
      continue;
    }
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

// Not noexcept, because `systems_at` rejects a stage outside the closed enumeration rather than
// reading past its offsets array (engine review finding 15). Every call below names a literal
// enumerator, so the rejection is unreachable from the kernel and is there for the caller that
// manufactures a stage value.
[[nodiscard]] bool stage_is_declared(const SystemPipeline& system_pipeline,
                                     const SystemStage stage) {
  return !system_pipeline.systems_at(stage).empty();
}

} // namespace

GameSimulation GameSimulation::create(SimulationConfig configuration, GameWorld initial_world,
                                      GameSimulationSetup setup) {
  if (setup.has_mode() && (setup.has_systems() || setup.has_contact_rules())) {
    throw SimulationValidationError(
        SimulationValidationCode::kGameSimulationSetupConflict, "game_simulation.setup",
        "a setup that declares a GameMode may not also declare a system pipeline or a contact "
        "rule table; the mode declares both");
  }

  // The bare rectangular arena the configuration still publishes. Every caller written before maps
  // existed lands here, which is why no accepted fixture had to change to gain a map.
  MapDefinition map = setup.has_map()
                          ? std::move(*setup.map_)
                          : MapDefinition::bare_arena(ArenaBounds::create(
                                configuration.world_width(), configuration.world_height()));

  // Every declaration is read exactly once, here, and the mode is destroyed with `setup` when this
  // function returns. "Nothing calls into the mode during a tick" is therefore structural: the
  // kernel holds declarations and never a mode
  // (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle").
  std::string mode_name{GameSimulation::kEngineDefaultModeName};
  SystemPipeline declared_systems = SystemPipeline::empty();
  ContactRuleTable contact_rules = ContactRuleTable::built_in();
  CommandKindMask accepted_command_kinds = CommandKindMask::all();
  std::unique_ptr<const SpawnPolicy> spawn_policy = std::make_unique<const IdleSpawnPolicy>();
  std::unique_ptr<const MatchObjective> objective = std::make_unique<const IdleMatchObjective>();
  if (setup.has_mode()) {
    const GameMode& mode = *setup.mode_;
    mode.validate_map(map);
    mode_name = std::string(mode.name());
    declared_systems = mode.systems();
    contact_rules = mode.contact_rules();
    accepted_command_kinds = mode.accepted_command_kinds();
    spawn_policy = mode.spawn_policy();
    objective = mode.objective();
  } else {
    if (setup.has_systems()) {
      declared_systems = std::move(*setup.systems_);
    }
    if (setup.has_contact_rules()) {
      contact_rules = std::move(*setup.contact_rules_);
    }
  }

  // A spawn point that cannot seat a disc of the configured radius is a startup rejection rather
  // than a bounds failure on the tick that first seated an entity there. The production path has
  // already run this inside `GameWorld::create(configuration, map, seed)`; running it again here
  // is what covers every other way a map reaches a simulation.
  require_spawn_points_are_seatable(configuration, map);

  // The engine's lifecycle system is appended last at kLifecycle and is not removable, so a mode's
  // own lifecycle systems always run before this tick's phase transition is evaluated.
  SystemPipeline system_pipeline =
      std::move(declared_systems)
          .with_appended(SystemPipeline::StagedSystem{
              SystemStage::kLifecycle,
              std::make_unique<const MatchLifecycleSystem>(std::move(objective))});

  SpatialGrid initial_grid = SpatialGrid::create(configuration, map.bounds(), initial_world);
  return GameSimulation(configuration, std::move(map), std::move(initial_world),
                        std::move(initial_grid), std::move(system_pipeline),
                        std::move(contact_rules), SpawnSystem(std::move(spawn_policy)),
                        std::move(mode_name), accepted_command_kinds, TickSequence::zero());
}

void GameSimulation::step(const FixedDelta fixed_delta, const InputBatch& input_batch) {
  const TickSequence next_tick_sequence = tick_sequence_.next();

  // Phase 0. Every phase and stage below reads the working world, so a failure anywhere leaves the
  // committed world, grid, and sequence exactly as the previous commit left them.
  //
  // Opening the tick installs the batch's EntityIdReservation on the working world, which is what
  // makes `GameWorld::create_entity()` succeed for the duration of this tick and nowhere else
  // (`docs/architecture/0004-gameplay-architecture.md`
  // § "Determinism obligations for framework code").
  GameWorld next_world = world_;
  next_world.open_tick(input_batch.entity_id_reservation());
  apply_input_batch(next_world, input_batch);

  // The batch's own index: the bodies the despawns of this batch left, at this tick's
  // start-of-tick positions. It is derived before seating because the SpawnSystem's policy socket
  // reads a TickContext, and the index that context must publish is the world as the tick found
  // it -- a seating cannot observe a seating.
  std::optional<SpatialGrid> reindexed_batch_grid;
  if (!indexed_bodies_equal(next_world, world_)) {
    reindexed_batch_grid = grid_.rebuilt(next_world);
  }
  const SpatialGrid& batch_grid = reindexed_batch_grid ? *reindexed_batch_grid : grid_;
  const TickContext seating_context =
      TickContext::create(next_tick_sequence, fixed_delta, configuration_, map_, batch_grid);

  // Still phase 0: the engine's SpawnSystem offers every entity awaiting a body to the mode's
  // SpawnPolicy and performs the seatings it chose. A seated entity is indexed at its marker, so
  // the intake index is re-derived exactly when something was seated.
  const std::size_t seated_count = spawn_system_.seat_pending_entities(next_world, seating_context);

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
  if (seated_count != 0) {
    reindexed_intake_grid = batch_grid.rebuilt(next_world);
  }
  const SpatialGrid& intake_grid = reindexed_intake_grid ? *reindexed_intake_grid : batch_grid;

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

  // Phase 10: apply this tick's DespawnEvent removals, validate what survives them, rebuild the
  // index from the survivors, clear the tick-local state, replace the committed state, and
  // increment the sequence once. The contract's signed-zero canonicalization has no step here
  // because Vector2 performs it at every construction, so no negative zero can reach a body in the
  // first place.
  //
  // **Removal precedes validation, which is a deliberate deviation from the written order of
  // `docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick" phase 10**
  // and closes engine review finding 14. Validating first makes an entity that a system both
  // pushed out of bounds and marked for despawn stop the match, and royale's elimination pairs
  // exactly those two: `zone_elimination` names an entity that left the safe zone, which is
  // frequently an entity a contact has just pushed past the arena edge. Validating what survives
  // asks the only question that matters -- is the world this tick is about to *publish* legal --
  // and an entity that is about to be removed has no committed position to be illegal. Everything
  // the original order guaranteed still holds: removal precedes the rebuild, the rebuild precedes
  // publication, no committed grid holds a non-live EntityId, and no snapshot observes a
  // half-applied removal. ADR 0003 owes this reordering an amendment.
  const bool roster_removed = apply_despawn_events(next_world);
  require_committed_bodies_in_bounds(next_world, map_, configuration_);
  if (roster_removed ||
      (late_stage_declared && !still_indexes(committed_indexed_bodies, next_world))) {
    next_grid = next_grid.rebuilt(next_world);
  }
  next_world.close_tick();

#ifndef NDEBUG
  // The invariant the predicates above exist to maintain, checked rather than asserted in prose.
  // It costs a second full rebuild, so it is debug-only -- which is every unit-test, fixture, and
  // sanitizer run, and therefore every run that could catch a stale index.
  if (!(next_grid == next_grid.rebuilt(next_world))) {
    throw SimulationValidationError(
        SimulationValidationCode::kGameSimulationSpatialIndexStale, "game_simulation.commit.grid",
        "the committed spatial index is not a rebuild of the committed world");
  }
#endif

  // All calculations and allocations are complete. These value moves are noexcept, so the three
  // assignments form one non-throwing commit from the caller's perspective.
  world_ = std::move(next_world);
  grid_ = std::move(next_grid);
  tick_sequence_ = next_tick_sequence;
}

WorldSnapshot GameSimulation::snapshot() const {
  return WorldSnapshot::from_world(tick_sequence_, world_, mode_name_);
}

GameSimulation::GameSimulation(SimulationConfig configuration, MapDefinition map, GameWorld world,
                               SpatialGrid grid, SystemPipeline system_pipeline,
                               ContactRuleTable contact_rules, SpawnSystem spawn_system,
                               std::string mode_name, const CommandKindMask accepted_command_kinds,
                               const TickSequence tick_sequence) noexcept
    : configuration_(configuration), map_(std::move(map)), world_(std::move(world)),
      grid_(std::move(grid)), system_pipeline_(std::move(system_pipeline)),
      contact_rules_(std::move(contact_rules)), spawn_system_(std::move(spawn_system)),
      mode_name_(std::move(mode_name)), accepted_command_kinds_(accepted_command_kinds),
      tick_sequence_(tick_sequence) {}

} // namespace blob_royale::simulation
