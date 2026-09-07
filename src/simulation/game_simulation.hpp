#ifndef BLOB_ROYALE_SIMULATION_GAME_SIMULATION_HPP
#define BLOB_ROYALE_SIMULATION_GAME_SIMULATION_HPP

#include "contact_rule_table.hpp"
#include "fixed_delta.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "map_definition.hpp"
#include "simulation_config.hpp"
#include "spatial_grid.hpp"
#include "system_pipeline.hpp"
#include "tick_sequence.hpp"
#include "world_snapshot.hpp"

namespace blob_royale::simulation {

// canonical: game_simulation -- the sole mutable owner of one deterministic world timeline.
//
// A tick is one fixed kernel with three named hook stages cut into it. The numbered phases are
// kernel mechanism -- no mode may reorder, skip, replace, or add one -- and the stages hold the
// mode's declared systems in the order the mode wrote them
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick";
// `docs/architecture/0004-gameplay-architecture.md` § "The tick: one fixed kernel, three named
// stages"):
//
//   phase 0            despawns, then this tick's remaining commands recorded per entity
//   ---- kPreKernel -- the mode's systems, declared order
//   phase 1            stored acceleration, then drag
//   phase 2            canonical candidate pairs
//   phase 3            contact resolution through the ContactRuleTable
//   phase 4            world bounds
//   phase 5            position integration
//   phase 6            spatial reindex
//   ---- kPostKernel - the mode's systems, declared order
//   ---- kLifecycle -- the mode's systems, declared order
//   phase 10           validate, apply DespawnEvent removals, reindex survivors, clear events,
//                      publish
//
// A `GameWorld&` exists only inside `step`, so ADR 0002's single-writer property is unchanged:
// systems receive the mutable reference the tick already holds and nothing outside a tick can
// obtain one.
// The kernel has exactly two policy sockets, both evaluated at a fixed point against declared
// data: the mode's SpawnPolicy in phase 0, which arrives in Step 19, and the mode's
// ContactRuleTable in phase 3. There is no third; a mode that wants to change anything else
// changes it with a system at a stage.
//
// The map is the arena source. Phase 4's fold, the commit-time bounds validation, and the spatial
// index all read `MapDefinition::bounds()`; SimulationConfig's world scalars stay the published
// geometry protocol v1 serves and are no longer read by any phase.
// related: system_pipeline.hpp -- the ordered, staged systems this kernel runs.
// related: contact_rule_table.hpp -- the ordered chain phase 3 walks per admitted contact.
// related: map_definition.hpp -- the arena and static content every tick reads.
// related: input_batch.hpp -- the one validated input value a tick reads.
class GameSimulation final {
public:
  // Validates the complete loaded state, builds its initial spatial index, and commits tick zero
  // over the bare rectangular arena the configuration publishes, with a pipeline that declares no
  // system and the built-in contact rules. This is the accepted seven-phase baseline: with an
  // empty pipeline, zero drag, an empty batch, and a map with no static bodies, the staged kernel
  // commits exactly the bodies the baseline committed, at the same tick horizons.
  [[nodiscard]] static GameSimulation create(SimulationConfig configuration,
                                             GameWorld initial_world);

  // The same, with the mode's declared systems. The pipeline is injected once, at construction,
  // and is never copied per tick.
  [[nodiscard]] static GameSimulation
  create(SimulationConfig configuration, GameWorld initial_world, SystemPipeline system_pipeline);

  // The same over a declared map. The map is the arena every phase reads and the static content a
  // mode's systems consult.
  [[nodiscard]] static GameSimulation create(SimulationConfig configuration, MapDefinition map,
                                             GameWorld initial_world,
                                             SystemPipeline system_pipeline);

  // The complete form: the mode's map, systems, and contact rules, each injected once at
  // construction. The `create(configuration, map, mode, world)` form that reads all three from one
  // GameMode arrives in Step 19; the mode declares its table there and this overload is what it
  // hands the kernel.
  [[nodiscard]] static GameSimulation create(SimulationConfig configuration, MapDefinition map,
                                             GameWorld initial_world,
                                             SystemPipeline system_pipeline,
                                             ContactRuleTable contact_rules);

  GameSimulation(const GameSimulation&) = delete;
  GameSimulation(GameSimulation&&) noexcept = default;
  GameSimulation& operator=(const GameSimulation&) = delete;
  GameSimulation& operator=(GameSimulation&&) noexcept = default;
  ~GameSimulation() = default;

  [[nodiscard]] const SimulationConfig& configuration() const& noexcept { return configuration_; }
  [[nodiscard]] const SimulationConfig& configuration() const&& = delete;
  [[nodiscard]] const MapDefinition& map() const& noexcept { return map_; }
  [[nodiscard]] const MapDefinition& map() const&& = delete;
  [[nodiscard]] const ContactRuleTable& contact_rules() const& noexcept { return contact_rules_; }
  [[nodiscard]] const ContactRuleTable& contact_rules() const&& = delete;
  [[nodiscard]] TickSequence tick_sequence() const noexcept { return tick_sequence_; }

  // Advances exactly one canonical fixed step against exactly one input value. Every phase and
  // every stage is evaluated against transaction-local values; a failure leaves the previously
  // committed world, grid, and sequence unchanged. A tick with no commands is this same call with
  // `InputBatch::empty()`, not a different code path.
  void step(FixedDelta fixed_delta, const InputBatch& input_batch);

  // Copies one complete committed state. The returned value never aliases the mutable world.
  [[nodiscard]] WorldSnapshot snapshot() const;

private:
  GameSimulation(SimulationConfig configuration, MapDefinition map, GameWorld world,
                 SpatialGrid grid, SystemPipeline system_pipeline, ContactRuleTable contact_rules,
                 TickSequence tick_sequence) noexcept;

  SimulationConfig configuration_;
  MapDefinition map_;
  GameWorld world_;
  SpatialGrid grid_;
  SystemPipeline system_pipeline_;
  ContactRuleTable contact_rules_;
  TickSequence tick_sequence_;
};

} // namespace blob_royale::simulation

#endif
