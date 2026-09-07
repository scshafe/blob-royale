#ifndef BLOB_ROYALE_SIMULATION_TICK_CONTEXT_HPP
#define BLOB_ROYALE_SIMULATION_TICK_CONTEXT_HPP

#include "fixed_delta.hpp"
#include "map_definition.hpp"
#include "simulation_config.hpp"
#include "spatial_grid.hpp"
#include "tick_sequence.hpp"

#include <utility>

namespace blob_royale::simulation {

// canonical: tick_context -- everything a system may read that is not game-world state.
//
// The context is mode-agnostic and tiny by construction. It deliberately exposes **no clock** and
// **no InputBatch**: commands reach a system only as `Controllable::commands_this_tick` and as
// world events, so no system can observe a half-applied batch or reinterpret the intake order, and
// no system can read a wall time
// (`docs/architecture/0004-gameplay-architecture.md` § "The tick: one fixed kernel, three named
// stages").
//
// `tick_sequence()` is the sequence **this tick commits** -- one greater than the last committed
// sequence -- so a duration comparison or a recorded tick names the tick that publishes its result
// and the snapshot for tick N is internally consistent
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick").
//
// `map()` is the arena the kernel folds against and the static content a mode reads: bounds,
// static bodies, and markers, including the derived `spawn_points()` projection.
//
// **`spatial_index()` has a per-stage meaning, and it is a promise about *which world* the index
// describes** rather than about freshness in general:
//
//   kPreKernel   the index of the bodies phase 0 left, at this tick's start-of-tick positions.
//                That is the previous tick's committed index whenever the batch changed no indexed
//                body, which is every tick of every accepted fixture, and a rebuild over the
//                post-batch bodies when a despawn changed one. Nothing has moved yet.
//                Phase 2 queries this same index, re-derived first when a kPreKernel system
//                changed a body.
//   kPostKernel  this tick's phase 6 rebuild, over the positions phase 5 integrated.
//   kLifecycle   the same phase 6 rebuild. The commit's own rebuild over the survivors of this
//                tick's DespawnEvents happens after every stage has run, so no stage observes it.
//
// **The index a stage reads does not reflect that stage's own writes.** It is the value the kernel
// derived before the stage ran, so a system that creates a body, destroys an entity, or moves one
// and then queries `spatial_index()` in the same `apply` is querying the world as it was on entry.
// The kernel re-derives the index after every stage before it commits, so nothing stale is ever
// published, but within one `apply` the accessor is a snapshot: a system that needs to see its own
// writes must read the component stores, which are live.
//
// The index is read-only here on purpose: it is a derived value the kernel owns
// (`docs/architecture/0003-deterministic-simulation-contract.md`
// § "Spatial-grid policy and partition boundaries"), and a system that could write it could make
// the broad phase disagree with the positions it indexes.
//
// The configuration is held by value rather than by reference: it is seven scalars, a tick
// constructs one context per stage, and a value member cannot dangle behind a system that outlives
// a simulation. The map and the index are held by reference because both are owned by the
// simulation for the whole match and copying either per stage would allocate inside the tick.
// related: simulation_system.hpp -- the interface that receives this value.
class TickContext final {
public:
  [[nodiscard]] static TickContext create(const TickSequence tick_sequence,
                                          const FixedDelta fixed_delta,
                                          SimulationConfig simulation_config,
                                          const MapDefinition& map,
                                          const SpatialGrid& spatial_index) noexcept {
    return TickContext(tick_sequence, fixed_delta, std::move(simulation_config), map,
                       spatial_index);
  }

  TickContext(const TickContext&) = default;
  TickContext(TickContext&&) noexcept = default;
  TickContext& operator=(const TickContext&) = delete;
  TickContext& operator=(TickContext&&) = delete;
  ~TickContext() = default;

  // The sequence this tick commits at phase 10.
  [[nodiscard]] TickSequence tick_sequence() const noexcept { return tick_sequence_; }

  [[nodiscard]] FixedDelta fixed_delta() const noexcept { return fixed_delta_; }

  [[nodiscard]] const SimulationConfig& simulation_config() const& noexcept {
    return simulation_config_;
  }
  [[nodiscard]] const SimulationConfig& simulation_config() const&& = delete;

  [[nodiscard]] const MapDefinition& map() const& noexcept { return *map_; }
  [[nodiscard]] const MapDefinition& map() const&& = delete;

  [[nodiscard]] const SpatialGrid& spatial_index() const& noexcept { return *spatial_index_; }
  [[nodiscard]] const SpatialGrid& spatial_index() const&& = delete;

  // Value equality over everything the context publishes, including the referenced map and index.
  // Comparing the referents rather than the addresses keeps the context a value: two contexts that
  // publish equal worlds are equal, whichever simulation owns them.
  [[nodiscard]] friend bool operator==(const TickContext& left, const TickContext& right) {
    return left.tick_sequence_ == right.tick_sequence_ && left.fixed_delta_ == right.fixed_delta_ &&
           left.simulation_config_ == right.simulation_config_ && *left.map_ == *right.map_ &&
           *left.spatial_index_ == *right.spatial_index_;
  }

private:
  TickContext(const TickSequence tick_sequence, const FixedDelta fixed_delta,
              SimulationConfig simulation_config, const MapDefinition& map,
              const SpatialGrid& spatial_index) noexcept
      : tick_sequence_(tick_sequence), fixed_delta_(fixed_delta),
        simulation_config_(std::move(simulation_config)), map_(&map),
        spatial_index_(&spatial_index) {}

  TickSequence tick_sequence_;
  FixedDelta fixed_delta_;
  SimulationConfig simulation_config_;
  // Never null: both are bound from a reference at construction and the context is a tick-local
  // value that cannot outlive the simulation that built it.
  const MapDefinition* map_;
  const SpatialGrid* spatial_index_;
};

} // namespace blob_royale::simulation

#endif
