#ifndef BLOB_ROYALE_SIMULATION_GAME_SIMULATION_HPP
#define BLOB_ROYALE_SIMULATION_GAME_SIMULATION_HPP

#include "fixed_delta.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
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
//   phase 3            contact resolution
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
// related: system_pipeline.hpp -- the ordered, staged systems this kernel runs.
// related: input_batch.hpp -- the one validated input value a tick reads.
class GameSimulation final {
public:
  // Validates the complete loaded state, builds its initial spatial index, and commits tick zero
  // with a pipeline that declares no system. This is the accepted seven-phase baseline: with an
  // empty pipeline, zero drag, and an empty batch the staged kernel commits exactly the bodies the
  // baseline committed, at the same tick horizons.
  [[nodiscard]] static GameSimulation create(SimulationConfig configuration,
                                             GameWorld initial_world);

  // The same, with the mode's declared systems. The pipeline is injected once, at construction,
  // and is never copied per tick. The `create(configuration, map, mode)` form that also injects
  // the map and the mode arrives in Step 19.
  [[nodiscard]] static GameSimulation
  create(SimulationConfig configuration, GameWorld initial_world, SystemPipeline system_pipeline);

  GameSimulation(const GameSimulation&) = delete;
  GameSimulation(GameSimulation&&) noexcept = default;
  GameSimulation& operator=(const GameSimulation&) = delete;
  GameSimulation& operator=(GameSimulation&&) noexcept = default;
  ~GameSimulation() = default;

  [[nodiscard]] const SimulationConfig& configuration() const& noexcept { return configuration_; }
  [[nodiscard]] const SimulationConfig& configuration() const&& = delete;
  [[nodiscard]] TickSequence tick_sequence() const noexcept { return tick_sequence_; }

  // Advances exactly one canonical fixed step against exactly one input value. Every phase and
  // every stage is evaluated against transaction-local values; a failure leaves the previously
  // committed world, grid, and sequence unchanged. A tick with no commands is this same call with
  // `InputBatch::empty()`, not a different code path.
  void step(FixedDelta fixed_delta, const InputBatch& input_batch);

  // Copies one complete committed state. The returned value never aliases the mutable world.
  [[nodiscard]] WorldSnapshot snapshot() const;

private:
  GameSimulation(SimulationConfig configuration, GameWorld world, SpatialGrid grid,
                 SystemPipeline system_pipeline, TickSequence tick_sequence) noexcept;

  SimulationConfig configuration_;
  GameWorld world_;
  SpatialGrid grid_;
  SystemPipeline system_pipeline_;
  TickSequence tick_sequence_;
};

} // namespace blob_royale::simulation

#endif
