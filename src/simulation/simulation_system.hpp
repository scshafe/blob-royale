#ifndef BLOB_ROYALE_SIMULATION_SIMULATION_SYSTEM_HPP
#define BLOB_ROYALE_SIMULATION_SIMULATION_SYSTEM_HPP

#include <string_view>

namespace blob_royale::simulation {

class GameWorld;
class TickContext;

// canonical: simulation_system -- one named unit of game rule executed inside one tick.
// @extension-point simulation_system
//
// The tick is a fixed kernel with three declared seams cut into it. The kernel is mechanism and no
// mode may reorder, skip, or replace it; the stages are policy and hold the mode's ordered systems
// (`docs/architecture/0004-gameplay-architecture.md` § "The tick: one fixed kernel, three named
// stages"). A mechanic is therefore a new file plus one line in a mode's declared system list.
//
// **`apply` is `const` on purpose: a system may hold immutable configuration and nothing else.**
// Every value a system mutates is world-owned, so a tick's result stays a function of the
// committed world and the tick's InputBatch alone, which is what makes ADR 0003's
// 100-fresh-run bit-identity rule survive an arbitrary number of registered systems. A system that
// wants per-entity state across ticks registers a component; mode configuration is constructor
// state of the system the mode builds, held as a `const` member, so there is no configuration
// lookup, no opaque blob in the context, and no downcast.
//
// Two implementations of this seam: `zone_shrink` for royale, `hill_scoring` for king of the hill.
// related: system_pipeline.hpp -- the ordered, staged list one mode declares.
// related: tick_context.hpp -- everything a system may read that is not game-world state.
class SimulationSystem {
public:
  virtual ~SimulationSystem() = default;
  SimulationSystem(const SimulationSystem&) = delete;
  SimulationSystem& operator=(const SimulationSystem&) = delete;

  // Stable, unique, snake_case identity used by the pipeline, diagnostics, and fixtures.
  [[nodiscard]] virtual std::string_view name() const noexcept = 0;

  // The system's whole effect. Must be a pure function of (world, context) onto world.
  virtual void apply(GameWorld& world, const TickContext& context) const = 0;

protected:
  SimulationSystem() = default;
};

} // namespace blob_royale::simulation

#endif
